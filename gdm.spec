%global _changelog_trimtime %(date +%s -d "1 year ago")
%global _hardened_build 1

%define libauditver 1.0.6
%define pango_version 1.2.0
%define gtk3_version 2.99.2
%define pam_version 0.99.8.1-11
%define desktop_file_utils_version 0.2.90
%define nss_version 3.11.1
%define fontconfig_version 2.6.0

Summary: The GNOME Display Manager
Name: gdm
Version: 3.11.90
Release: 1%{?dist}
Epoch: 1
License: GPLv2+
Group: User Interface/X
URL: http://download.gnome.org/sources/gdm
#VCS: git:git://git.gnome.org/gdm
Source: http://download.gnome.org/sources/gdm/3.11/gdm-%{version}.tar.xz
Source1: org.gnome.login-screen.gschema.override

Requires(pre): /usr/sbin/useradd

Requires: pam >= 0:%{pam_version}
Requires: /sbin/nologin
Requires: system-logos
Requires: xorg-x11-server-utils
Requires: setxkbmap
Requires: xorg-x11-xinit
Requires: systemd >= 186
Requires: accountsservice
Requires: gnome-settings-daemon >= 2.21.92
Requires: gnome-icon-theme-symbolic
Requires: iso-codes
Requires: gnome-session
Requires: gnome-shell
# since we use it, and pam spams the log if the module is missing
Requires: gnome-keyring-pam
Requires: pulseaudio-gdm-hooks
# We need 1.0.4-5 since it lets us use "localhost" in auth cookies
Requires: libXau >= 1.0.4-4
BuildRequires: pkgconfig(libcanberra-gtk)
BuildRequires: pango-devel >= 0:%{pango_version}
BuildRequires: gtk3-devel >= 0:%{gtk3_version}
BuildRequires: pam-devel >= 0:%{pam_version}
BuildRequires: fontconfig >= 0:%{fontconfig_version}
BuildRequires: desktop-file-utils >= %{desktop_file_utils_version}
BuildRequires: libtool automake autoconf
BuildRequires: libattr-devel
BuildRequires: gettext
BuildRequires: libdmx-devel
BuildRequires: audit-libs-devel >= %{libauditver}
BuildRequires: gobject-introspection-devel
BuildRequires: autoconf automake libtool
BuildRequires: intltool
%ifnarch s390 s390x ppc ppc64
BuildRequires: xorg-x11-server-Xorg
%endif
BuildRequires: nss-devel >= %{nss_version}
BuildRequires: libselinux-devel
BuildRequires: check-devel
BuildRequires: iso-codes-devel
BuildRequires: libxklavier-devel >= 4.0
BuildRequires: upower-devel >= 0.9.7
BuildRequires: libXdmcp-devel
BuildRequires: dbus-glib-devel
BuildRequires: GConf2-devel
BuildRequires: pkgconfig(accountsservice) >= 0.6.3
BuildRequires: pkgconfig(libsystemd-login)
BuildRequires: pkgconfig(libsystemd-daemon)
BuildRequires: pkgconfig(ply-boot-client)
BuildRequires: systemd
BuildRequires: dconf

Requires(post):   systemd
Requires(preun):  systemd
Requires(postun): systemd

# these are all just for rewriting gdm.d/00-upstream-settings
Requires(posttrans): dconf

Provides: service(graphical-login) = %{name}

Requires: audit-libs >= %{libauditver}

# Swallow up old fingerprint/smartcard plugins
Obsoletes: gdm-plugin-smartcard < 1:3.2.1
Provides: gdm-plugin-smartcard = %{epoch}:%{version}-%{release}

Obsoletes: gdm-plugin-fingerprint < 1:3.2.1
Provides: gdm-plugin-fingerprint = %{epoch}:%{version}-%{release}

%package libs
Summary: Client-side library to talk to gdm
Group: Development/Libraries
Requires: %{name} = %{epoch}:%{version}-%{release}

%description libs
The gdm-libs package contains libraries that can
be used for writing custom greeters.

%package devel
Summary: Development files for gdm-libs
Group: Development/Libraries
Requires: %{name}-libs = %{epoch}:%{version}-%{release}

%description devel
The gdm-devel package contains headers and other
files needed to build custom greeters.

%description
GDM provides the graphical login screen, shown shortly after boot up,
log out, and when user-switching.

%description devel
Development files and headers for writing GDM greeters.

%prep
%setup -q

autoreconf -i -f
intltoolize -f

%build

%configure --with-pam-prefix=%{_sysconfdir} \
           --with-run-dir=/run/gdm \
           --enable-split-authentication \
           --enable-profiling      \
           --enable-console-helper \
           --with-plymouth \
           --with-selinux

# drop unneeded direct library deps with --as-needed
# libtool doesn't make this easy, so we do it the hard way
sed -i -e 's/ -shared / -Wl,-O1,--as-needed\0 /g' -e 's/    if test "$export_dynamic" = yes && test -n "$export_dynamic_flag_spec"; then/      func_append compile_command " -Wl,-O1,--as-needed"\n      func_append finalize_command " -Wl,-O1,--as-needed"\n\0/' libtool

make %{?_smp_mflags}


%install
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/gdm/Init
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/gdm/PreSession
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/gdm/PostSession

make install DESTDIR=$RPM_BUILD_ROOT

rm -f $RPM_BUILD_ROOT%{_sysconfdir}/pam.d/gdm

# add logo to shell greeter
cp %{SOURCE1} $RPM_BUILD_ROOT%{_datadir}/glib-2.0/schemas

# gets rebuilt in posttrans
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/dconf/db/gdm

# docs go elsewhere
rm -rf $RPM_BUILD_ROOT/%{_prefix}/doc

# create log dir
mkdir -p $RPM_BUILD_ROOT/var/log/gdm

# remove the gdm Xsession as we're using the xdm one
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/gdm/Xsession
(cd $RPM_BUILD_ROOT%{_sysconfdir}/gdm; ln -sf ../X11/xinit/Xsession .)

rm -f $RPM_BUILD_ROOT%{_libdir}/gtk-2.0/modules/*.a
rm -f $RPM_BUILD_ROOT%{_libdir}/gtk-2.0/modules/*.la

mkdir -p $RPM_BUILD_ROOT%{_datadir}/gdm/autostart/LoginWindow

mkdir -p $RPM_BUILD_ROOT/run/gdm

find $RPM_BUILD_ROOT -name '*.a' -delete
find $RPM_BUILD_ROOT -name '*.la' -delete

# don't install fallback greeter
rm $RPM_BUILD_ROOT%{_datadir}/gdm/greeter/applications/gdm-simple-greeter.desktop
rm $RPM_BUILD_ROOT%{_datadir}/gdm/greeter/applications/polkit-gnome-authentication-agent-1.desktop

%find_lang gdm --with-gnome

%pre
/usr/sbin/useradd -M -u 42 -d /var/lib/gdm -s /sbin/nologin -r gdm > /dev/null 2>&1
/usr/sbin/usermod -d /var/lib/gdm -s /sbin/nologin gdm >/dev/null 2>&1
# ignore errors, as we can't disambiguate between gdm already existed
# and couldn't create account with the current adduser.
exit 0

%post
/sbin/ldconfig
touch --no-create /usr/share/icons/hicolor >&/dev/null || :

# if the user already has a config file, then migrate it to the new
# location; rpm will ensure that old file will be renamed

custom=/etc/gdm/custom.conf

if [ $1 -ge 2 ] ; then
    if [ -f /usr/share/gdm/config/gdm.conf-custom ]; then
        oldconffile=/usr/share/gdm/config/gdm.conf-custom
    elif [ -f /etc/X11/gdm/gdm.conf ]; then
        oldconffile=/etc/X11/gdm/gdm.conf
    fi

    # Comment out some entries from the custom config file that may
    # have changed locations in the update.  Also move various
    # elements to their new locations.

    [ -n "$oldconffile" ] && sed \
    -e 's@^command=/usr/X11R6/bin/X@#command=/usr/bin/Xorg@' \
    -e 's@^Xnest=/usr/X11R6/bin/Xnest@#Xnest=/usr/X11R6/bin/Xnest@' \
    -e 's@^BaseXsession=/etc/X11/xdm/Xsession@#BaseXsession=/etc/X11/xinit/Xsession@' \
    -e 's@^BaseXsession=/etc/X11/gdm/Xsession@#&@' \
    -e 's@^BaseXsession=/etc/gdm/Xsession@#&@' \
    -e 's@^Greeter=/usr/bin/gdmgreeter@#Greeter=/usr/libexec/gdmgreeter@' \
    -e 's@^RemoteGreeter=/usr/bin/gdmlogin@#RemoteGreeter=/usr/libexec/gdmlogin@' \
    -e 's@^GraphicalTheme=Bluecurve@#&@' \
    -e 's@^BackgroundColor=#20305a@#&@' \
    -e 's@^DefaultPath=/usr/local/bin:/usr/bin:/bin:/usr/X11R6/bin@#&@' \
    -e 's@^RootPath=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin:/usr/X11R6/bin@#&@' \
    -e 's@^HostImageDir=/usr/share/hosts/@#HostImageDir=/usr/share/pixmaps/faces/@' \
    -e 's@^LogDir=/var/log/gdm@#&@' \
    -e 's@^PostLoginScriptDir=/etc/X11/gdm/PostLogin@#&@' \
    -e 's@^PreLoginScriptDir=/etc/X11/gdm/PreLogin@#&@' \
    -e 's@^PreSessionScriptDir=/etc/X11/gdm/PreSession@#&@' \
    -e 's@^PostSessionScriptDir=/etc/X11/gdm/PostSession@#&@' \
    -e 's@^DisplayInitDir=/var/run/gdm.pid@#&@' \
    -e 's@^RebootCommand=/sbin/reboot;/sbin/shutdown -r now;/usr/sbin/shutdown -r now;/usr/bin/reboot@#&@' \
    -e 's@^HaltCommand=/sbin/poweroff;/sbin/shutdown -h now;/usr/sbin/shutdown -h now;/usr/bin/poweroff@#&@' \
    -e 's@^ServAuthDir=/var/gdm@#&@' \
    -e 's@^Greeter=/usr/bin/gdmlogin@Greeter=/usr/libexec/gdmlogin@' \
    -e 's@^RemoteGreeter=/usr/bin/gdmgreeter@RemoteGreeter=/usr/libexec/gdmgreeter@' \
    $oldconffile > $custom
fi

if [ $1 -ge 2 -a -f $custom ] && grep -q /etc/X11/gdm $custom ; then
   sed -i -e 's@/etc/X11/gdm@/etc/gdm@g' $custom
fi

%systemd_post gdm.service

%preun
%gconf_schema_remove gdm-simple-greeter
%systemd_preun gdm.service

%postun
/sbin/ldconfig
if [ $1 -eq 0 ]; then
  touch --no-create %{_datadir}/icons/hicolor >&/dev/null || :
  gtk-update-icon-cache %{_datadir}/icons/hicolor >&/dev/null || :
fi

if [ $1 -eq 0 ] ; then
    /usr/bin/glib-compile-schemas %{_datadir}/glib-2.0/schemas &> /dev/null || :
fi
%systemd_postun

%posttrans
dconf update
gtk-update-icon-cache %{_datadir}/icons/hicolor >&/dev/null || :
/usr/bin/glib-compile-schemas %{_datadir}/glib-2.0/schemas &> /dev/null || :

%files -f gdm.lang
%doc AUTHORS COPYING NEWS README

%dir %{_sysconfdir}/gdm
%config(noreplace) %{_sysconfdir}/gdm/custom.conf
%config %{_sysconfdir}/gdm/Init/*
%config %{_sysconfdir}/gdm/PostLogin/*
%config %{_sysconfdir}/gdm/PreSession/*
%config %{_sysconfdir}/gdm/PostSession/*
%config %{_sysconfdir}/pam.d/gdm-autologin
%config %{_sysconfdir}/pam.d/gdm-password
# not config files
%{_sysconfdir}/gdm/Xsession
%{_datadir}/gdm/gdm.schemas
%{_sysconfdir}/dbus-1/system.d/gdm.conf
%dir %{_sysconfdir}/gdm/Init
%dir %{_sysconfdir}/gdm/PreSession
%dir %{_sysconfdir}/gdm/PostSession
%dir %{_sysconfdir}/gdm/PostLogin
%{_datadir}/gnome-session/sessions/gdm-shell.session
%{_datadir}/pixmaps/*.png
%{_datadir}/glib-2.0/schemas/org.gnome.login-screen.gschema.xml
%{_datadir}/glib-2.0/schemas/org.gnome.login-screen.gschema.override
%{_libexecdir}/gdm-host-chooser
%{_libexecdir}/gdm-session-worker
%{_libexecdir}/gdm-simple-chooser
%{_libexecdir}/gdm-simple-slave
%{_libexecdir}/gdm-xdmcp-chooser-slave
%{_sbindir}/gdm
%{_bindir}/gdmflexiserver
%{_bindir}/gdm-screenshot
%{_datadir}/gdm/greeter/applications/*
%{_datadir}/gdm/greeter/autostart/*
%{_datadir}/gdm/locale.alias
%{_datadir}/gdm/gdb-cmd
%{_libdir}/libgdm*.so*
%dir %{_localstatedir}/log/gdm
%attr(1770, gdm, gdm) %dir %{_localstatedir}/lib/gdm
%attr(0711, root, gdm) %dir /run/gdm
%attr(1755, root, gdm) %dir %{_localstatedir}/cache/gdm
%dir %{_sysconfdir}/dconf/db/gdm.d/locks
%dir %{_sysconfdir}/dconf/db/gdm.d
%{_sysconfdir}/dconf/db/gdm.d/00-upstream-settings
%{_sysconfdir}/dconf/db/gdm.d/locks/00-upstream-settings-locks
%{_sysconfdir}/dconf/profile/gdm
%{_datadir}/icons/hicolor/*/*/*.png
%config %{_sysconfdir}/pam.d/gdm-pin
%config %{_sysconfdir}/pam.d/gdm-smartcard
%config %{_sysconfdir}/pam.d/gdm-fingerprint
%{_sysconfdir}/pam.d/gdm-launch-environment
%{_unitdir}/gdm.service

%files devel
%dir %{_includedir}/gdm
%{_includedir}/gdm/*.h
%{_datadir}/gir-1.0/Gdm-1.0.gir
%{_libdir}/pkgconfig/gdm.pc

%files libs
%{_libdir}/girepository-1.0/Gdm-1.0.typelib

%changelog
* Fri Feb 21 2014 Richard Hughes <rhughes@redhat.com> - 1:3.11.90-1
- Update to 3.11.90

* Wed Jan 15 2014 Richard Hughes <rhughes@redhat.com> - 1:3.11.4-1
- Update to 3.11.4

* Sun Dec 22 2013 Ville Skyttä <ville.skytta@iki.fi> - 1:3.11.3-2
- Drop empty TODO from docs, trivial rpmlint fixes.

* Tue Dec 17 2013 Richard Hughes <rhughes@redhat.com> - 1:3.11.3-1
- Update to 3.11.3

* Tue Nov 19 2013 Richard Hughes <rhughes@redhat.com> - 1:3.11.2-1
- Update to 3.11.2

* Mon Oct 28 2013 Richard Hughes <rhughes@redhat.com> - 1:3.10.0.1-1
- Update to 3.10.0.1

* Wed Sep 25 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.10.0-1
- Update to 3.10.0

* Thu Aug 22 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.9.90-1
- Update to 3.9.90

* Sat Aug 10 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.9.5-1
- Update to 3.9.5

* Tue Jul 16 2013 Richard Hughes <rhughes@redhat.com> - 1:3.8.3.1-1
- Update to 3.8.3.1

* Sat Jun 22 2013 Matthias Clasen <mclasen@redhat.com> - 3.8.3-2
- Trim %%changelog

* Fri Jun 14 2013 Ray Strode <rstrode@redhat.com> 3.8.3-1
- Update to 3.8.3

* Tue May 21 2013 Matthias Clasen <mclasen@redhat.com> 1:3.8.1.1-6
- Don't include the fallback greeter

* Mon May 20 2013 Ray Strode <rstrode@redhat.com> 1:3.8.1.1-5
- Fix permissions on /run/gdm
  Resolves: #fudge
  (http://lists.fedoraproject.org/pipermail/devel/2013-May/182906.html)

* Mon May 20 2013 Ray Strode <rstrode@redhat.com> 1:3.8.1.1-4
- Require gnome-shell. We no longer use the fallback greeter.
  (Since gdm 3.7.92).

* Fri May 17 2013 Ray Strode <rstrode@redhat.com> - 1:3.8.1.1-3
- Build with -fpie
  Resolves: #955154

* Thu May 16 2013 Florian Müllner <fmuellner@redhat.com> - 1:3.8.1.1-2
- Update branding

* Wed Apr 17 2013 Richard Hughes <rhughes@redhat.com> - 1:3.8.1.1-1
- Update to 3.8.1.1

* Mon Apr 15 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.8.1-1
- Update to 3.8.1

* Mon Apr 01 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.8.0-2
- Drop the metacity dep now that the fallback greeter is gone

* Tue Mar 26 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.8.0-1
- Update to 3.8.0

* Thu Mar 21 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.7.92-2
- Drop the polkit-gnome dep now that the fallback greeter is gone

* Wed Mar 20 2013 Richard Hughes <rhughes@redhat.com> - 1:3.7.92-1
- Update to 3.7.92

* Wed Mar  6 2013 Matthias Clasen <mclasen@redhat.com> 3.7.91-1
- Update to 3.7.91

* Wed Feb 27 2013 Ray Strode <rstrode@redhat.com> 3.7.90-3
- Fix up runtime dir for real

* Tue Feb 26 2013 Ray Strode <rstrode@redhat.com> 3.7.90-2
- Fix up runtime dir path (spotted by dwalsh)

* Fri Feb 22 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.7.90-1
- Update to 3.7.90

* Wed Feb 06 2013 Kalev Lember <kalevlember@gmail.com> - 1:3.7.5-1
- Update to 3.7.5

* Wed Jan 09 2013 Richard Hughes <hughsient@gmail.com> - 1:3.7.3.1-1
- Update to 3.7.3.1

* Tue Nov 20 2012 Richard Hughes <hughsient@gmail.com> - 1:3.7.2-1
- Update to 3.7.2

* Tue Nov 20 2012 Matthias Clasen <mclasen@redhat.com> - 1:3.6.2-2
- Remove patch fuzz of 999

* Wed Nov 14 2012 Kalev Lember <kalevlember@gmail.com> - 1:3.6.2-1
- Update to 3.6.2

* Mon Nov 05 2012 Ray Strode <rstrode@redhat.com> - 1:3.6.1-4
- Fix GDM auth cookie problem
  Related: #870695

* Mon Oct 29 2012 Matthias Clasen <mclasen@redhat.com> - 1:3.6.1-3
- Add ppc to %%ExcludeArch

* Thu Oct 18 2012 Matthias Clasen <mclasen@redhat.com> - 1:3.6.1-2
- Require gnome-icon-theme-symbolic (#867718)

* Tue Oct 16 2012 Kalev Lember <kalevlember@gmail.com> - 1:3.6.1-1
- Update to 3.6.1

* Tue Sep 25 2012 Richard Hughes <hughsient@gmail.com> - 1:3.6.0-1
- Update to 3.6.0

* Wed Sep 19 2012 Matthias Clasen <mclasen@redhat.com> 1:3.5.92.1-1
- Update to 3.5.92.1

* Fri Sep 07 2012 Ray Strode <rstrode@redhat.com> 1:3.5.91-2
- Fix autologin
- Fix selinux context after forking session

* Thu Sep 06 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.91-1
- Update to 3.5.91

* Tue Aug 21 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.90-1
- Update to 3.5.90

* Tue Aug  7 2012 Lennart Poettering <lpoetter@redhat.com> - 1:3.5.5-2
- https://fedoraproject.org/wiki/Features/DisplayManagerRework
- https://bugzilla.redhat.com/show_bug.cgi?id=846135
- Ship and use gdm.service
- Force gdm onto VT1

* Tue Aug 07 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.5-1
- Update to 3.5.5

* Fri Jul 27 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1:3.5.4.2-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_18_Mass_Rebuild

* Thu Jul 19 2012 Ray Strode <rstrode@redhat.com> 3.5.4.2-1
- Update to 3.5.4.2
- Fixes non-autologin

* Thu Jul 19 2012 Ray Strode <rstrode@redhat.com> 3.5.4.1-1
- Update to 3.5.4.1
- Fixes autologin
- Fixes logind integration
- Fixes dconf incompatibility

* Thu Jul 19 2012 Matthias Clasen <mclasen@redhat.com> - 1:3.5.4-3
- Fix dconf profile syntax

* Thu Jul 19 2012 Kalev Lember <kalevlember@gmail.com> - 1:3.5.4-2
- Require systemd >= 186 for libsystemd-login

* Tue Jul 17 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.4-1
- Update to 3.5.4

* Thu Jun 28 2012 Ray Strode <rstrode@redhat.com> 3.5.2-4
- Build with plymouth support (woops).

* Wed Jun 13 2012 Ray Strode <rstrode@redhat.com> 3.5.2-3
- Drop unused spool dir
  Related: #819254

* Sat Jun  9 2012 Matthias Clasen <mclasen@redhat.com> - 1:3.5.2-2
- Fix gnome-shell detection

* Thu Jun 07 2012 Richard Hughes <hughsient@gmail.com> - 1:3.5.2-1
- Update to 3.5.2

* Sat Apr 14 2012 Matthias Clasen <mclasen@redhat.com> - 3.4.1-1
- Update to 3.4.1

* Mon Apr 09 2012 Ray Strode <rstrode@redhat.com> 3.4.0.1-5
- One more try at fixing crash
  Resolves: #810451

* Mon Apr 09 2012 Ray Strode <rstrode@redhat.com> 3.4.0.1-4
- Fix crash
  Resolves: #810451

* Thu Apr  5 2012 Matthias Clasen <mclasen@redhat.com> 3.4.0.1-3
- Make session unlocking after user switching work

* Mon Apr 02 2012 Ray Strode <rstrode@redhat.com> 3.4.0.1-2
- Move pam_gnome_keyring after XDG_RUNTIME_DIR is setup
  Resolves: #809152

* Tue Mar 27 2012 Ray Strode <rstrode@redhat.com> 3.4.0.1-1
- Update to 3.4.0.1
- fixes autologin

* Tue Mar 27 2012 Richard Hughes <hughsient@gmail.com> - 1:3.4.0-1
- Update to 3.4.0

* Tue Mar 20 2012 Ray Strode <rstrode@redhat.com> 3.3.92.1-1
- Update to 3.3.92.1

* Wed Feb 15 2012 Ray Strode <rstrode@redhat.com> 3.2.1.1-14
- More consolekit registration fixes

* Mon Feb 13 2012 Ray Strode <rstrode@redhat.com> 3.2.1.1-12
- Restore ConsoleKit registration if ConsoleKit is installed

* Tue Feb  7 2012 Lennart Poettering <lpoetter@redhat.com> - 1:3.2.1.1-11
- Add multi-seat patch from gdm git master

* Thu Jan 26 2012 Ray Strode <rstrode@redhat.com> 3.2.1.1-10
- Drop system-icon-theme requirement since we don't depend
  on it anymore

* Fri Jan 13 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1:3.2.1.1-9
- Rebuilt for https://fedoraproject.org/wiki/Fedora_17_Mass_Rebuild

* Wed Nov 09 2011 Adam Williamson <awilliam@redhat.com> 1:3.2.1.1-8
- sync with recent changes on f16 branch:
  + update to 3.2.1.1
  + properly set up PAM files
  + auth fixes
  + put fallback plugin development files in -devel
  + require metacity to fix #746693
  + fix logo in fallback mode - just set it to a Fedora file

* Thu Nov 03 2011 Ray Strode <rstrode@redhat.com> 3.2.1-3
- Drop fprintd-pam dependency and make Harald's laptop
  more lean and streamlined.

* Wed Oct 26 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1:3.2.1-2
- Rebuilt for glibc bug#747377

* Tue Oct 18 2011 Ray Strode <rstrode@redhat.com> 3.2.1-1
- Update to 3.2.1
- Move plugins into main package

* Wed Oct  5 2011 Adam Williamson <awilliam@redhat.com> - 1:3.2.0-2
- shell_check.patch (upstream): re-add check for gnome-shell presence
  before using it to handle login (RH #743596)

* Wed Sep 28 2011 Ray <rstrode@redhat.com> - 1:3.2.0-1
- Update to 3.2.0

* Tue Jun 28 2011 Ray Strode <rstrode@redhat.com> 3.1.2-3
- Disable fatal critcals
  Resolves: #717324

* Tue Jun 21 2011 Michael Schwendt <mschwendt@fedoraproject.org> - 3.1.2-2
- Fix /dev/ull typo in scriptlets (#693046).

* Mon Jun 13 2011 Ray Strode <rstrode@redhat.com> 3.1.2-1
- Update for release

* Mon Jun 06 2011 Ray Strode <rstrode@redhat.com> 3.0.4-1
- Update to latest version
  Resolves CVE-2011-1709

* Fri Apr 15 2011 Matthias Clasen <mclasen@redhat.com> - 3.0.0-2
- Put the Fedora logo back in the greeter

* Mon Apr  4 2011 Matthias Clasen <mclasen@redhat.com> - 3.0.0-1
- Update to 3.0.0

* Tue Mar 22 2011 Ray Strode <rstrode@redhat.com> 2.91.94-1
- Update to 2.91.94

* Wed Mar 09 2011 Ray Strode <rstrode@redhat.com> 2.91.93-2
- Fix autologin crash

* Tue Mar 08 2011 Ray Strode <rstrode@redhat.com> 2.91.93-1
- Update to 2.91.93

* Tue Feb 22 2011 Ray Strode <rstrode@redhat.com> 2.91.6-11
- Dropping async code didn't work.  The bug was still
  around.  This commit should fix it.

* Fri Feb 18 2011 Ray Strode <rstrode@redhat.com> 2.91.6-10
- Fix user list async bugs by dropping async code and
  moving to accounts service library
  Resolves: #678236
- Add requires for accounts service to spec since it isn't
  optional (and hasn't been for a while)

* Thu Feb 17 2011 Ray Strode <rstrode@redhat.com> 2.91.6-9
- Add back session chooser
  Resolves: #539638

* Mon Feb 14 2011 Ray Strode <rstrode@redhat.com> 2.91.6-8
- Do build with pam stack changes need to get ecryptfs
  working.
  Resolves: #665061

* Mon Feb 14 2011 Ray Strode <rstrode@redhat.com> 2.91.6-7
- Fix crasher and rendering glitches
  Resolves: #674978

* Fri Feb 11 2011 Matthias Clasen <mclasen@redhat.com> - 2.91.6-6
- Rebuild against newer gtk

* Wed Feb 09 2011 Christopher Aillon <caillon@redhat.com> - 2.91.6-5
- Drop the requires on plymouth-gdm-hooks since it no longer exists

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1:2.91.6-4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Mon Feb 07 2011 Bastien Nocera <bnocera@redhat.com> 2.91.6-3
- Really disable gnome-settings-daemon plugins in the greeter

* Fri Feb 04 2011 Ray Strode <rstrode@redhat.com> 2.91.6-2
- Drop some unimportant patches
- Attempt to fix bug 674978 (theme related crash)

* Wed Feb 02 2011 Ray Strode <rstrode@redhat.com> 2.91.6-1
- Update to 2.91.6

* Sat Jan 29 2011 Ville Skyttä <ville.skytta@iki.fi> - 1:2.91.4-6
- Dir ownership fixes.

* Wed Jan 19 2011 Ray Strode <rstrode@redhat.com> 2.91.4-5
- Fix swapped LHS and RHS in more-aggressive-about-loading-icons
  patch

* Wed Jan 19 2011 Ray Strode <rstrode@redhat.com> 2.91.4-4
- Update previous patch to handle NULL better

* Wed Jan 19 2011 Ray Strode <rstrode@redhat.com> 2.91.4-3
- Fix icon ref counting issue

* Wed Jan 19 2011 Ray Strode <rstrode@redhat.com> 2.91.4-2
- Be more aggresive about loading icons
  (right now we fail, which combined with fatal criticals
  gives us crashes)

* Fri Dec 17 2010 Ray Strode <rstrode@redhat.com> 2.91.4-1
- Update to 2.91.4

* Wed Dec 15 2010 Christopher Aillon <caillon@redhat.com> 2.32.0-4
- Add maybe-set-is-loaded.patch to ensure we end up with a loaded user

* Wed Dec 01 2010 Peter Hutterer <peter.hutterer@redhat.com> 1:2.32.0-3
- plymouth.patch: xserver 1.10 takes "-background none" root argument
  instead of the fedora-specific "-nr".
- Add missing BuildRequires for dbus-glib-devel

* Mon Nov 15 2010 Dan Williams <dcbw@redhat.com> 2.32.0-2
- Fix upower build requirement

* Wed Sep 29 2010 Ray Strode <rstrode@redhat.com> 2.32.0-1
- Update to 2.32.0

* Tue Aug 17 2010 Ray Strode <rstrode@redhat.com> 2.31.90-1
- Update to 2.31.90

* Wed Jun 16 2010 Matthias Clasen <mclasen@redhat.com> 2.30.2-3
- Kill explicit library deps

* Tue Apr 27 2010 Ray Strode <rstrode@redhat.com> 2.30.2-2
- Update multistack patch
- Add accounts service patch
- Update plymouth patch

* Tue Apr 27 2010 Matthias Clasen <mclasen@redhat.com> 2.30.2-1
- Update to 2.30.2
- Spec file cleanups

* Tue Apr 06 2010 Ray Strode <rstrode@redhat.com> 2.30.0-2
- Update plymouth patch to work with 0.8.1

* Mon Mar 29 2010 Matthias Clasen <mclasen@redhat.com> 2.30.0-1
- Update to 2.30.0

* Wed Mar 24 2010 Matthias Clasen <mclasen@redhat.com> 2.29.92-4
- Drop hal dependency

* Tue Mar 09 2010 Ray Strode <rstrode@redhat.com> 2.29.92-3
- Drop Prereq in favor of Requires(pre)

* Tue Mar 09 2010 Ray Strode <rstrode@redhat.com> 2.29.92-2
- Rebase multistack patch

* Tue Mar 09 2010 Bastien Nocera <bnocera@redhat.com> 2.29.92-1
- Update to 2.29.92

* Thu Feb 11 2010 Matthias Clasen <mclasen@redhat.com> 2.29.6-1
- Update to 2.29.6

* Thu Jan 28 2010 Ray Strode <rstrode@redhat.com> 2.29.5-2
- name graphical-login vprovides (bug 559268)

* Tue Jan 26 2010 Ray Strode <rstrode@redhat.com> 2.29.5-1
- Update to 2.29.5

* Sun Jan 17 2010 Matthias Clasen <mclasen@redhat.com> - 2.29.4-3
- Rebuild

* Thu Jan 14 2010 Ray Strode <rstrode@redhat.com> 2.29.4-2
- Fix boot

* Tue Dec 22 2009 Matthias Clasen <mclasen@redhat.com> - 2.29.4-1
- Update to 2.29.4

* Wed Dec 09 2009 Ray Strode <rstrode@redhat.com> 2.29.1-3
- Update to work better with latest plymouth

* Thu Dec 03 2009 Ray Strode <rstrode@redhat.com> 2.29.1-2
- Drop upstreamed patches
- rebase multi-stack patch

* Tue Dec 01 2009 Bastien Nocera <bnocera@redhat.com> 2.29.1-1
- Update to 2.29.1

* Sat Oct 31 2009 Matthias Clasen <mclasen@redhat.com> 2.28.1-20
- Don't show 'Lock Screen' in the user switcher if locked down

* Sat Oct 31 2009 Matthias Clasen <mclasen@redhat.com> 2.28.1-18
- Actually set up statusicon padding

* Fri Oct 30 2009 Ray Strode <rstrode@redhat.com> 2.28.1-17
- Make the user list slide animation smoother

* Thu Oct 29 2009 Ray Strode <rstrode@redhat.com> 2.28.1-16
- Shrink autologin timer
- Make language dialog not double spaced

* Thu Oct 29 2009 Ray Strode <rstrode@redhat.com> 2.28.1-15
- Don't show fingerprint task button unless fingerprint is
  enabled
- Don't show smartcard task button and list item unless
  pcscd is running.

* Wed Oct 28 2009 Ray Strode <rstrode@redhat.com> 2.28.1-14
- Don't show image on login button

* Wed Oct 28 2009 Ray Strode <rstrode@redhat.com> 2.28.1-13
- Fix double free during user switching (might address
  bug 512944)

* Tue Oct 27 2009 Ray Strode <rstrode@redhat.com> 2.28.1-12
- One more go at bug 527920

* Tue Oct 27 2009 Ray Strode <rstrode@redhat.com> 2.28.1-11
- Tighten permissions on /var/run/gdm (bug 531063)

* Mon Oct 26 2009 Ray Strode <rstrode@redhat.com> 2.28.1-10
- Position shutdown menu properly on multihead machines

* Fri Oct 23 2009 Ray Strode <rstrode@redhat.com> 2.28.1-9
- Don't show hostname by default if it's localhost

* Fri Oct 23 2009 Ray Strode <rstrode@redhat.com> 2.28.1-8
- Attempt to fix crash some users see.
- Clean up rebase

* Fri Oct 23 2009 Ray Strode <rstrode@redhat.com> 2.28.1-7
- Show Other user even when there are no other users
  (bug 527920)

* Fri Oct 23 2009 Ray Strode <rstrode@redhat.com> 2.28.1-6
- Properly read default keyboard layout (bug 530452)

* Fri Oct 23 2009 Ray Strode <rstrode@redhat.com> 2.28.1-5
- Remove tool tip from login button

* Thu Oct 22 2009 Ray Strode <rstrode@redhat.com> 2.28.1-4
- Fix autologin window spasms
- Fix autologin timer animation
- Make autologin and multistack play better together
- Add padding to notification tray

* Wed Oct 21 2009 Ray Strode <rstrode@redhat.com> 2.28.1-3
- Move date from panel to clock tooltip

* Tue Oct 20 2009 Ray Strode <rstrode@redhat.com> 2.28.1-2
- Move shutdown functions to panel from login window

* Tue Oct 20 2009 Ray Strode <rstrode@redhat.com> 2.28.1-1
- Update to 2.28.1

* Fri Oct 09 2009 Ray Strode <rstrode@redhat.com> 2.28.0-9
- Fix Other... user.

* Fri Oct  9 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.28.0-8
- Move bubbles to the lower right on the login screen

* Wed Oct 07 2009 Ray Strode <rstrode@redhat.com> - 1:2.28.0-7
- Fix gdm-password / xguest interaction (bug 524421)

* Mon Oct  5 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.28.4-6
- Fix the autostart file for at-spi-registryd

* Thu Oct  1 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.28.4-5
- Handle keyboard layout variants

* Mon Sep 28 2009 Ray Strode <rstrode@redhat.com> - 1:2.28.0-4
- Add cache dir to package manifest

* Mon Sep 28 2009 Richard Hughes  <rhughes@redhat.com> - 1:2.28.0-3
- Add a patch to use DeviceKit-power rather than the removed methods in
  gnome-power-manager.

* Fri Sep 25 2009 Ray Strode <rstrode@redhat.com> 1:2.28.0-2
- Fix autologin

* Wed Sep 23 2009 Matthias Clasen <mclasen@redhat.com> 1:2.28.0-1
- Update to 2.28.0

* Sat Aug 29 2009 Caolán McNamara <caolanm@redhat.com> 1:2.27.90-2
- rebuild with new audit

* Mon Aug 24 2009 Ray Strode <rstrode@redhat.com> 1:2.27.90-1
- update to 2.27.90

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 1:2.27.4-7
- rebuilt with new audit

* Wed Aug 19 2009 Lennart Poettering <lpoetter@redhat.com> 1:2.27.4-6
- Add pulseaudio-gdm-hooks to dependencies

* Thu Aug 06 2009 Ray Strode <rstrode@redhat.com> 1:2.27.4-5
- rebuild

* Sat Aug  1 2009 Matthias Clasen <mclasen@redhat.com> 1:2.27.4-4
- Drop unneeded direct deps

* Fri Jul 24 2009 Ray Strode <rstrode@redhat.com> 1:2.27.4-3
- Fix delay during login

* Mon Jul 20 2009 Ray Strode <rstrode@redhat.com> 1:2.27.4-2
- Use correct multi-stack patch

* Mon Jul 20 2009 Ray Strode <rstrode@redhat.com> 1:2.27.4-1
- Update to 2.27.4

* Thu Jul 02 2009 Adam Jackson <ajax@redhat.com> 1:2.26.1-13
- Requires: xorg-x11-xkb-utils -> Requires: setxkbmap

* Wed Jul 01 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.1-12
- Drop defunct arch conditional buildrequires

* Tue Jun 30 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.26.1-11
- Rebuild against new libxklavier

* Fri Jun 12 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.26.1-10
- Bump rev to fix upgrade path

* Tue Jun  9 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.26.1-8
- Port to PolicyKit 1

* Wed Jun 03 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.1-5
- Fix language parsing code (bug 502778)

* Mon Apr 27 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.26.1-4
- Don't drop schemas translations from po files

* Fri Apr 24 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.1-3
- Add Requires for pam modules in plugins

* Tue Apr 21 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.1-2
- Stop inactive pam conversations when one succeeds.
  Should fix bug 496234

* Tue Apr 14 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.1-1
- Update to 2.26.1

* Mon Apr 13 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-8
- Add less boring multistack patch for testing

* Mon Mar 23 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-7
- Load session and language settings when username is read on
  Other user

* Fri Mar 20 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-6
- Fix problem in keyboard layout selector (483195)

* Thu Mar 19 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-5
- Use gethostname() _properly_ instead of g_get_host_name() when writing
  out xauth files, because the hostname may change out from
  under us and glib caches it.

* Thu Mar 19 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-4
- Use gethostname() instead of g_get_host_name() when writing
  out xauth files, because the hostname may change out from
  under us and glib caches it.

* Wed Mar 18 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-3
- emit "user-selected" signal for non-user items in the list
  as well.

* Mon Mar 16 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-2
- Clean up empty auth dirs so they don't hang around forever
  (bug 485974)

* Mon Mar 16 2009 Ray Strode <rstrode@redhat.com> - 1:2.26.0-1
- Update to 2.26.0
- Drop gcc workaround.  it might not be needed now.

* Sat Mar 14 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-20
- Drop the use localhost patch because it broke things.
  Instead add authorization that doesn't depend on a hostname

* Thu Mar 12 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-19
- Add a lame patch in the off chance it might work around a
  gcc bug on ppc:
      unable to find register to spill in class 'LINK_OR_CTR_REGS'
  Probably won't work.

* Thu Mar 12 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-18
- Add Requires: libXau >= 1.0.4-4 to use localhost in xauth cookies
- Use localhost instead of g_get_host_name ()

* Thu Mar 12 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-17
- Don't force X server on active vt more than once

* Tue Mar 10 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-16
- Store greeter's auth cookie under "localhost" instead
  of g_get_host_name() since NetworkManager tries to synchronize
  the internal hostname with the externally resolvable one.

* Mon Mar 9 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-15
- Don't race with PAM modules that ask questions during
  pam_open_session (and don't subsequently go bonkers when
  losing the race).

* Fri Mar 6 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-14
- Reset "start session when ready" state to FALSE when starting
  new greeter from existing slave.  May fix problem Chris Ball
  is seeing with language selection in autologin the second time
  after boot up.

* Thu Mar 5 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-13
- 2.25.2-10 fixes were actually only for timed login.
  Add same fix for auto login

* Thu Mar 5 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-12
- Create settings object early to prevent assertion failures
  when one pam conversation completes before another starts.

* Wed Mar 4 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-11
- Bring back language/session/layout selector for autologin

* Wed Mar 4 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-10
- Add some fixes for autologin

* Tue Mar 3 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-9
- Add limited 'one-stack-only' version of multistack patch
  (See https://fedoraproject.org/wiki/Features/MultiplePAMStacksInGDM)
- Drop 10 second delay in start up because of broken autostart
  file

* Fri Feb 27 2009 Matthias Clasen  <mclasen@redhat.com>
- Require PolicyKit-authentication-agent

* Tue Feb 24 2009 Matthias Clasen  <mclasen@redhat.com>
- Refine the hal patch

* Tue Feb 24 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1:2.25.2-6
- Rebuilt for https://fedoraproject.org/wiki/Fedora_11_Mass_Rebuild

* Fri Feb 20 2009 Matthias Clasen <mclasen@redhat.com> - 1:2.25.2-5
- Get the default keyboard layout out of hal device properties
  instead of /etc/sysconfig/keyboard

* Fri Feb 20 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-4
- add Provides: service(graphical-login) to help anaconda

* Thu Jan 22 2009 Ray Strode <rstrode@redhat.com> - 1:2.25.2-3
- Open log files for append to make selinux lock down easier

* Wed Dec 17 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.25.2-2
- Update to 2.25.2
- Drop the xkb groups workaround to see if the issue disappeared

* Thu Dec  4 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.25.1-2
- Update to 2.25.1

* Mon Oct 20 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.24.0-11
- Respect system keyboard setting

* Wed Oct 15 2008 Ray Strode <rstrode@redhat.com> - 1:2.24.0-10
- Rework "force X on vt1" code to work after the user logs out

* Wed Oct 15 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.24.0-9
- Save some space

* Fri Oct  3 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.24.0-8
- Don't show a non-functional help menuitem

* Tue Sep 30 2008 Ray Strode <rstrode@redhat.com> - 1:2.24.0-7
- Make panel slide in initially like the gnome panel

* Tue Sep 30 2008 Ray Strode <rstrode@redhat.com> - 1:2.24.0-6
- drop background priority change.  Choppyiness in -3 ended up
  being a bug in gnome-settings-daemon.
- pull patch from upstream to scale face icons with fontsize

* Thu Sep 25 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.24.0-5
- Require gnome-session

* Tue Sep 23 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.24.0-4
- Let /var/lib/gdm be owned by gdm, to make pulseaudio happy

* Tue Sep 23 2008 Ray Strode <rstrode@redhat.com> - 1:2.24.0-3
- Load background after everything else, so the crossfade
  isn't choppy.

* Mon Sep 22 2008 Ray Strode <rstrode@redhat.com> - 1:2.24.0-2
- Fix permssions on spool dir

* Mon Sep 22 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.24.0-1
- Update to 2.24.0

* Mon Sep 22 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.92-10
- Flush X event queue after setting _XROOTPMAP_ID so there's
  no race with settings daemon reading the property

* Fri Sep 19 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.92-9
- Fix crash from language dialog

* Wed Sep 17 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.92-8
- canonicalize codeset to match output of locale -m
- filter duplicates from language list

* Tue Sep 16 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.23.92-7
- Plug a few memory leaks

* Tue Sep 16 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.92-6
- Use _XROOTPMAP_ID instead of _XSETROOT_ID

* Tue Sep 16 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.92-5
- Save root window in XSETROOTID property for transition

* Fri Sep 12 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.92-4
- Fix bug in last patch

* Thu Sep 11 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.92-3
- Add hook to allow for plymouth transition

* Tue Sep  9 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.92-2
- Disallow root login

* Mon Sep  8 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.92-1
- Update to 2.23.92-1

* Thu Aug 28 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.91-0.20080828.2
- Update to non-broken snapshot

* Thu Aug 28 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.91-0.20080828.1
- Update to snapshot

* Mon Aug 25 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.90-2
- Add desktop file for metacity

* Mon Aug 25 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.90-1
- Update to 2.23.90

* Thu Aug 14 2008 Behdad Esfahbod <besfahbo@redhat.com> - 1:2.23.2-3
- Add upstreamed patch gdm-2.23.2-unknown-lang.patch

* Wed Aug 13 2008 Ray Strode <rstrode@redhat.com> - 1:2.23.2-2
- Require plymouth-gdm-hooks so plymouth-log-viewer gets pulled
  in on upgrades

* Wed Jul 30 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.2-1
- Update to 2.23.2

* Mon Jul 28 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.1.0.2008.07.28.1
- Update to newer snapshot

* Mon Jul 21 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.1.0.2008.07.21.3
- Update to newer snapshot

* Mon Jul 21 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.1.0.2008.07.21.2
- Update to new snapshot

* Mon Jul 21 2008 Jon McCann <jmccann@redhat.com> - 1:2.23.1.0.2008.07.21.1
- Update to snapshot

* Fri Jul 11 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.22.0-12
- Actually apply the patch

* Thu Jul 10 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.22.0-11
- Fix some broken icons on the login screen

* Thu Jul 10 2008 Matthias Clasen  <mclasen@redhat.com> - 1:2.22.0-10
- Improve rendering of languages

* Thu Jul  3 2008 Jon McCann <jmccann@redhat.com> - 1:2.22.0-9
- Check for a null filesystem type

* Wed Jun 25 2008 Ray Strode <rstrode@redhat.com> - 1:2.22.0-8
- After discussion with X team, turn tcp connections off by default,
  but add back option to toggle on (bug 446672)

* Wed Jun 25 2008 Ray Strode <rstrode@redhat.com> - 1:2.22.0-7
- enable tcp connections by default

* Thu May  8 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.22.0-6
- Add a GConf key to disable the user list

* Mon May  5 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.22.0-5
- Autoreconf
- Bump rev

* Mon May  5 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.22.0-4
- Add a keyboard chooser to the greeter

* Sun May  4 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.22.0-3
- Fix source url

* Fri May  1 2008 Jon McCann <jmccann@redhat.com> - 1:2.22.0-2
- Retry tagging

* Fri May  1 2008 Jon McCann <jmccann@redhat.com> - 1:2.22.0-1
- Update to 2.22.0
- Fix restarting when bus goes away

* Thu May  1 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.05.01.1
- ConsoleKit fixes
- Don't show session selector if only one session installed
- automatically pop up language/session selectors when using mnemonics

* Tue Apr 29 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.10-0.2008.04.29.2
- Fix debugging
- Fix resetting slave after session migration
- Desensitize power buttons briefly after page switch
- Remove Users: label from greeter

* Tue Apr 29 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.10-0.2008.04.29.1
- make transient greeter less transient to workaround spurious vt switch

* Mon Apr 28 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.28.1
- a11y improvements
- make "Suspend" desensitize properly when not-available
- make resize animation faster
- user switcher fixes

* Fri Apr 18 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.18.2
- Get Chinese back in language list

* Fri Apr 18 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.18.1
- start orca without main window
- add missing priorities for plugins
- add more failsafe lockdown

* Wed Apr 16 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.16.1
- Disable typeahead when asking for password so password can't get shown
  in clear text (bug 442300)

* Wed Apr 16 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.11.4
- Use start-here instead of fedora-logo-icon to aid generic-logos

* Fri Apr 11 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.10-0.2008.04.11.3
- Fix up the XKB workaround

* Fri Apr 11 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.11.2
- Fix security issue in last commit

* Fri Apr 11 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.11.1
- Fix focus handling when tabbing from user-chooser to buttons
- don't set real uid to user before setcred
- fix permissions on /var/run/gdm ... again

* Thu Apr 10 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.10-0.2008.04.08.4
- Work around a XKB problem

* Tue Apr  8 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.08.3
- Language list was incomplete (bug 441613)

* Tue Apr  8 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.08.2
- Fix permissions on /var/run/gdm

* Tue Apr  8 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.08.1
- Install X auth cookies in /var/run/gdm instead of /tmp

* Mon Apr  7 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.07.3
- Disable image for automatic login and other user
- Act more sanely if gnome isn't installed

* Mon Apr  7 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.07.2
- Allow double-click to select language from list

* Mon Apr  7 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.07.1
- Make automatic login timer fade in
- No more checkboxes in user-switch applet

* Sun Apr  6 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.06.1
- Focus face browser after failed login attempt
- disable debug messages until 2.22.0 is released

* Sat Apr  5 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.10-0.2008.04.04.2
- Improve handling of CK error messages

* Sat Apr  5 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.04.1
- Fix jump in animation for autologin
- Fix crash if LANG="somethingbogus"

* Sat Apr  5 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.03.3
- Fix crash when canceling autologin

* Fri Apr  4 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.10-0.2008.04.03.2
- Uninstall gconf schemas before the files are gone

* Thu Apr  3 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.04.03.1
- Update to snapshot
- Improves shrink/grow animation of login window

* Wed Apr  2 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.10-0.2008.04.02.1
- Update to snapshot

* Mon Mar 31 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.10-0.2008.03.26.4
- Fix a directory ownership oversight

* Wed Mar 26 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.10-0.2008.03.26.3
- Fix build due to #436349

* Wed Mar 26 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.10-0.2008.03.26.2
- Update to newer snapshot that includes more lockdown

* Wed Mar 26 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.03.26.1
- Update to snapshot
- Turn on profiling

* Fri Mar 21 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.10-0.2008.03.18.3
- Don't require a theme we don't use

* Wed Mar 19 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.10-0.2008.03.18.2
- Fix default path (bug 430187)

* Tue Mar 18 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.10-0.2008.03.18.1
- Update to snapshot

* Mon Mar 17 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.9-5
- Implement tooltips in the language selection dialog

* Mon Mar 17 2008 Matthias Clasen <mclasen@redhat.com> - 1:2.21.9-4
- Stop gvfs from using fuse in the sandbox session

* Tue Mar 11 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.9-3
- remove duplication signal definition from bad patch merge
  which led to crash for "Other" user

* Mon Mar 10 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.9-2
- Fix case where we can't lookup a user.

* Mon Mar 10 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.9-1
- Update to 2.21.9

* Mon Mar 10 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.9-0.2008.03.10.2
- Prevent some spurious wake ups caused by the
  timed login timer animation

* Mon Mar 10 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.9-0.2008.03.10.1
- Update to latest snapshot

* Fri Mar 7 2008 David Woodhouse <dwmw2@redhat.com> - 1:2.21.9-0.2008.02.29.3
- Fix endianness breakage in signal pipes (#436333)

* Mon Mar 3 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.9-0.2008.02.29.2
- Be more explicit in file list; use less globs
- Don't package user-switcher in both packages!

* Fri Feb 29 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.9-0.2008.02.29.1
- Update to snapshot
- Split user-switcher out

* Mon Feb 25 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.8-1
- Update to 2.21.8

* Tue Feb 12 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.7-1
- Update to 2.21.7

* Fri Feb 8 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.7-0.2008.02.08.1
- Update to snapshot

* Wed Jan  30 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.6-1
- Update to 2.21.6

* Thu Jan  24 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.5-2
- add BuildRequires for iso-codes-devel

* Fri Jan  18 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.5-1
- Update to 2.21.5

* Thu Jan  17 2008 Jon McCann <jmccann@redhat.com> - 1:2.21.2-0.2007.11.20.11
- Rebuild

* Mon Jan  15 2008 Dan Walsh <dwalsh@redhat.com> - 1:2.21.2-0.2007.11.20.10
- Fix gdm.pam file so that session include system-auth happens after other session setup

* Mon Jan  7 2008 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.9
- hide guest account since it doesn't work

* Fri Dec 21 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.8
- Fix background (and other settings)

* Wed Dec 19 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.7
- Improve animation to be less jumpy

* Fri Dec 14 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.6
- Fix an uninitialized variable that makes the session list stop
  growing before its finished sometimes

* Thu Dec 13 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.5
- add session chooser to login screen
- add hoaky animations

* Fri Nov 30 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.21.2-0.2007.11.20.4
- Use the new "substack" support in pam to make keyring unlocking work

* Tue Nov 20 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.3
- use metacity for now

* Tue Nov 20 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.2
- Drop dont run profile patch since dwalsh changed /usr/sbin/gdm label

* Tue Nov 20 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.20.1
- Update to today's snapshot

* Mon Nov 19 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.19.3
- fix permissions on homedir

* Mon Nov 19 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.19.2
- move homedir to /var/lib/gdm

* Mon Nov 19 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.19.1
- Update to today's snapshot

* Thu Nov 15 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.14.2
- don't source /etc/profile at startup

* Wed Nov 14 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.14.1
- Update to today's snapshot

* Fri Nov  9 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.2-0.2007.11.09.1
- Update to today's snapshot

* Tue Oct 30 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.1-0.2007.10.30.1
- Update to today's snapshot

* Tue Oct 23 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.1-0.2007.10.23.1
- Update to today's snapshot

* Mon Oct 22 2007 Ray Strode <rstrode@redhat.com> - 1:2.21.1-0.2007.10.22.1
- Add a snapshot gdm trunk, totally different unfinished ui...

* Fri Oct  5 2007 Dan Walsh <dwalsh@redhat.com> - 1:2.20.0-14
- Added pam_selinux_permit and pam_namespace to gdm-pam
  - This pam module allows user without a password to login when selinux is in enforcing mode
- Added pam_namespace to gdm-autologin-pam
- These changes were made to make it easier to setup the xguest user account

* Tue Oct  3 2007 Alexander Larsson <alexl@redhat.com> - 1:2.20.0-14
- Fix up pam keyring integration to be what the latest version
  of the docs says

* Tue Oct  2 2007 Ray Strode <rstrode@redhat.com> - 1:2.20.0-13
- Actually add said escape == cancel behavior back

* Tue Oct  2 2007 Ray Strode <rstrode@redhat.com> - 1:2.20.0-12
- Add escape == cancel behavior back

* Mon Oct  1 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.20.0-11
- Fix a refcounting problem with user faces

* Mon Oct  1 2007 Ray Strode <rstrode@redhat.com> - 1:2.20.0-10
- apply upstream patch from Brady Anderson <brady.anderson@gmail.com>
  to fix writing out .dmrc file when setting default language
  (upstream bug 453916)

* Fri Sep 28 2007 Ray Strode <rstrode@redhat.com> - 1:2.20.0-9
- drop redhat-artwork dep, add fedorainfinity-gdm-theme dep

* Fri Sep 28 2007 Ray Strode <rstrode@redhat.com> - 1:2.20.0-8
- Another crack at 240853

* Fri Sep 28 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.20.0-7
- Fix the stupid bullets again

* Thu Sep 27 2007 Ray Strode <rstrode@redhat.com> - 1:2.20.0-6
- The previously mentioned typo didn't matter before because the
  compiled in default matched what the config file was supposed to
  say.  This commit restores matched default behavior (bug 301031)

* Thu Sep 27 2007 Ray Strode <rstrode@redhat.com> - 1:2.20.0-5
- Fix an apparent typo in the securitytokens.conf config file
  (bug 301031)

* Thu Sep 20 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.20.0-4
- Reenable root login due to popular demand

* Wed Sep 19 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.20.0-3
- Change default theme to FedoraInfinity

* Wed Sep 19 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.20.0-2
- Fix a hang on restart (#240853)

* Tue Sep 18 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.20.0-1
- Update to 2.20.0

* Wed Sep 12 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.8-4
- Change default password character back to circle instead of
  asterisk (bug 287951)

* Fri Sep  7 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.8-3
- rebuild --with-selinux

* Fri Sep  7 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.8-2
- make things work better for xguest users (bug 254164)

* Fri Sep  7 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.8-1
- Update to 2.19.8

* Tue Sep  4 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.7-1
- Update to 2.19.7

* Fri Aug 24 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.6-5
- use pam_selinux instead of home grown selinux code (bug 254164)

* Wed Aug 22 2007 Kristian Høgsberg <krh@redhat.com> - 1:2.19.6-4
- Pass -br to the default X server too.

* Sat Aug 18 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.6-3
- disable root login (see "low-hanging fruit" discussion on
  fedora-desktop-list)

* Thu Aug 16 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.6-2
- disable type ahead in user list (bug 252991)

* Wed Aug 15 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.6-1
- Update to 2.19.6
- Use %%find_lang for help files

* Sun Aug 12 2007 Adam Jackson <ajax@redhat.com> 1:2.19.5-9
- Remove the filereq on /etc/pam.d/system-auth, pam alone is sufficient.
- Bump the pam requirement to 0.99, 0.75 is ancient.

* Sun Aug 12 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.5-8
- Make the previous fix actually work

* Sun Aug 12 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.5-7
- Make gdmsetup work with consolehelper and pam again

* Mon Aug  6 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.5-6
- Require gnome-keyring-pam

* Mon Aug  6 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.5-5
- change previous patch to drop even more code

* Mon Aug  6 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.5-4
- turn off dwellmouselistener if devices don't send core events.
  don't warp pointer to stylus ever (upstream bug 457998)

* Fri Aug  3 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.5-3
- remove dwellmouselistener module from default configuration.
  It's pretty broken (bug 248752)

* Fri Aug  3 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.5-2
- Update license field

* Tue Jul 31 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.5-1
- Update to 2.19.5

* Mon Jul 30 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.4-2
- Add optional gnome-keyring support to the gdm pam stack

* Tue Jul 10 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.4-1
- Update to 2.19.4

* Wed Jun 27 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.3-3
- set Browser=true by default

* Wed Jun 27 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.3-2
- Drop an unnecessary file dependency

* Mon Jun 18 2007 Ray Strode <rstrode@redhat.com> - 1:2.19.3-1
- Update to 2.19.3

* Tue Jun  5 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.2-1
- Update to 2.19.2

* Mon May 21 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.19.1-1
- Update to 2.19.1

* Tue May 15 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-14
- hide users from userlist that have disabled shells
  (bug 240148)

* Thu May 10 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.18.0-13
- Follow packaging guidelines for scrollkeeper dependencies

* Mon May  7 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-12
- reenable utmp logging (bug 209537)

* Tue Apr 17 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-11
- Be more verbose to help isolate the problem in bug 234567

* Thu Apr 12 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-10
- add "Default" session back to the sessions menu (bug 234218)

* Thu Apr  5 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-9
- don't expect utf-8 usernames for plain greeter face browser
  either.

* Thu Apr  5 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-8
- don't expect utf-8 usernames for face browser (bug 235351).

* Thu Mar 29 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-7
- don't strcpy overlapping strings (bug 208181).

* Tue Mar 27 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.18.0-6
- Hide gdmphotosetup by default, since About Me does the same

* Tue Mar 20 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-5
- add fix to allow themes to cope with low resolution modes
  better (bug 232672)

* Mon Mar 19 2007 Ray Strode <rstrode@redhat.com> - 1:2.18.0-4
- update and reenable security token patch

* Mon Mar 19 2007 David Zeuthen <davidz@redhat.com> - 1:2.18.0-3
- Also pass AT's to the session from the plain greeter (#232518)
- New faces including new subpackage gdm-extra-faces

* Tue Mar 13 2007 David Zeuthen <davidz@redhat.com> - 1:2.18.0-2
- Update to upstream release 2.18.0
- Switch default theme to FedoraFlyingHigh and show /etc/passwd users
- Fix accessibility in the themed greeter (GNOME #412576)
- Enable accessible login, make sure gdm can access devices and
  pass activated AT's to the login session (#229912)
- Disable smart card login for now as patch doesn't apply anymore

* Fri Mar  9 2007 Ray Strode <rstrode@redhat.com> - 1:2.17.8-3
- hide langauges that aren't displayable from the list (bug 206048)

* Tue Mar  6 2007 Ray Strode <rstrode@redhat.com> - 1:2.17.8-2
- turn off pam sanity check because it conflicts with audit

* Wed Feb 28 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.8-1
- Update to 2.17.8

* Sat Feb 24 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.7-5
- Fix keynav in the face browser

* Fri Feb 23 2007 David Zeuthen <davidz@redhat.com> - 1:2.17.7-4
- Add some enhancements to the greeter (bgo #411427)

* Fri Feb 23 2007 Ray Strode <rstrode@redhat.com> - 1:2.17.7-3
- Update to 2.17.7

* Fri Feb 23 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.7-2
- Don't own /usr/share/icons/hicolor
- Install all desktop files

* Mon Feb 12 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.7-1
- try to update to 2.17.7
- Drop upstreamed patches

* Mon Feb 12 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.6-4
- Reuse existing sessions without asking
- Don't show failsafe sessions

* Sat Feb 10 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.6-3
- Fix a problem with the ConsoleKit support

* Tue Feb  6 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.6-2
- Apply a patch to improve fast user switching experience

* Tue Jan 23 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.6-1
- Update to 2.17.6

* Sat Jan 13 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.5-2
- Enable ConsoleKit support

* Thu Jan 11 2007 Matthias Clasen <mclasen@redhat.com> - 1:2.17.5-1
- Update to 2.17.5

* Fri Dec 15 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.17.4-1
- Update to 2.17.4, which fixes CVE-2006-6105

* Tue Dec  5 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.17.3-1
- Update to 2.17.3
- Update some patches

* Tue Nov  7 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.17.2-1
- Update to 2.17.2

* Sun Nov  5 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.17.1-1
- Update to 2.17.1

* Thu Oct 26 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.17.0-2
- Fix a crash with launching a11y support

* Sun Oct 22 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.17.0-1
- Update to 2.17.0

* Tue Oct 17 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.16.0-17
- Make photosetup help button work (#198138)

* Sun Oct 15 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-16.fc7
- don't log canceled pam conversations as failed login attempts

* Sun Oct 15 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-15.fc7
- Prefer modules in secmod db over hardcoded coolkey path

* Sat Oct 14 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-14.fc7
- have security token monitor helper process kill itself when
  the communication pipe to the main process goes away (bug 210677).

* Wed Oct 10 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-13.fc6
- desensitize entry fields until pam asks for input, so if pam
  doesn't initially ask for input (like in smart card required mode)
  the user can't type something and confuse gdm (bug 201344)

* Fri Oct 6 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-12.fc6
- invoke standard X server with -br option to ensure we get a
  black root on startup

* Thu Oct 5 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-11.fc6
- make monitoring code more reliable (bug 208018)

* Wed Sep 27 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.16.0-10.fc6
- Fix small issues in gdmsetup (#208225)

* Wed Sep 27 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.16.0-9.fc6
- Fix a problem with the display of the FedoraDNA theme
  in gdmsetup

* Tue Sep 19 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-8.fc6
- Add as_IN, si_LK to language list (bug 203917)

* Mon Sep 18 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-7.fc6
- fix a problem recently introduced in the smart card forking
  code

* Mon Sep 18 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-6.fc6
- fix a problem recently introduced in the smart card driver
  loading code (bug 206882)

* Thu Sep 14 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-5.fc6
- don't leak pipe fds (bug 206709)

* Thu Sep 14 2006 Ray Strode <rstrode@redhat.com> - 1:2.16.0-4.fc6
- update security token patch to not poll

* Fri Sep  8 2006 Jesse Keating <jkeating@redhat.com> - 1:2.16.0-3.fc6
- Apply correct defaults patch

* Thu Sep  7 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.16.0-2.fc6
- Change the default theme to FedoraDNA
- Bump redhat-artwork requirement

* Tue Sep  5 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.16.0-1.fc6
- Update to 2.16.0

* Sat Aug 26 2006 Karsten Hopp <karsten@redhat.com> - 1:2.15.10-2.fc6
- buildrequire inttools as this isn't a requirement of scrollkeeper anymore
  and thus missing from the buildroot

* Mon Aug 21 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.15.10-1.fc6
- Update to 2.15.10
- Drop upstreamed patch

* Fri Aug 4 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.9-1
- update to 2.15.9

* Fri Aug 4 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.7-2
- update gdmsetup pam file to use config-util stacks

* Thu Aug 3 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.7-1
- update to 2.15.7
- drop selinux patch that I don't think was ever finished

* Thu Aug 3 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-14
- fix face browser
  (http://bugzilla.gnome.org/show_bug.cgi?id=349640)
- fix error message reporting
  (http://bugzilla.gnome.org/show_bug.cgi?id=349758)

* Fri Jul 21 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-13
- simply all the security token code by only using one pam stack
- drop lame kill on token removal feature

* Fri Jul 21 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-12
- move authcookies out of home directories to prevent problems
  on nfs/afs mounted home directories (bug 178233).

* Fri Jul 21 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-11
- really fix annoying dialog problem mentioned in 2.15.6-6

* Wed Jul 19 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-10
- center cursor on xinerama head (bug 180085)

* Tue Jul 18 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-9
- add "kill all sessions on token removal" feature

* Tue Jul 18 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-8
- reenable session keyring support in pam module (bug 198629)

* Mon Jul 17 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-7
- make security token support use its own config file in
  preparation for modularizing it.

* Mon Jul 17 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-6
- fix off-by-one in the process-all-ops patch that was causing
  an anoying dialog to pop up on each login

* Sun Jul 16 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-5
- add initial wtmp and btmp logging support

* Fri Jul 14 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-4
- fix bug in security token support

* Fri Jul 14 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-3
- fix hang in gdmsetup

* Fri Jul 14 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-2
- put new pam module at top of stack (bug 198629)

* Wed Jul 12 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.6-1
- Update to 2.15.6

* Wed Jul 12 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.5-4
- add new pam module to pam files to support kernel session keyring

* Wed Jul 12 2006 Jesse Keating <jkeating@redhat.com> - 1:2.15.5-3.1
- rebuild

* Tue Jul 11 2006 Ray Strode <rstrode@redhat.com> 1:2.15.5-3
- add initial support for smart card security tokens

* Fri Jul 7 2006 Ray Strode <rstrode@redhat.com> 1:2.15.5-2
- add patch to process all operations when more than one comes
  in really quickly
- move default "Please enter your username" message to the
  greeter instead of the slave so that it doesn't get stacked if
  a pam module has a non default message
- add new message for reseting the current login operation
  (like the cancel button does, but accessible via the gdm fifo)

* Tue Jun 13 2006 Matthias Clasen <mclasen@redhat.com> 1:2.15.5-1
- Update to 2.15.5

* Mon Jun 12 2006 Bill Nottingham <notting@redhat.com> 1:2.15.3-8
- replace automake14 buildreq with automake

* Thu Jun  8 2006 Ray Strode <rstrode@redhat.com> 1:2.15.3-7
- fix CVE-2006-2452

* Wed Jun  7 2006 Jeremy Katz <katzj@redhat.com> - 1:2.15.3-6
- buildrequire the server so that we get the path right in the config file

* Tue Jun 06 2006 Karsten Hopp <karsten@redhat.de> 1:2.15.3-5
- buildrequire libdmx-devel

* Mon Jun  5 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.15.3-4
- Require system-logos, not fedora-logos

* Tue May 23 2006 Ray Strode <rstrode@redhat.com> - 1:2.15.3-3
- Support xdm -nodaemon option (bug 192461)

* Mon May 22 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.15.3-2
- Add missing BuildRequires (#192494)

* Wed May 17 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.15.3-1
- Update to 2.15.3

* Wed May 10 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.15.0-1
- Update to 2.15.0

* Wed Apr 26 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.14.4-2
- Update to 2.14.4

* Wed Apr 12 2006 Ray Strode <rstrode@redhat.com> - 1:2.14.1-4
- fix libexecdir substitution problem in configuration file

* Tue Apr 11 2006 Ray Strode <rstrode@redhat.com> - 1:2.14.1-3
- Add gdmthemetester.in to the mix (upstream bug 338079)

* Tue Apr 11 2006 Matthias Clasen <mclasen@redhat.com> - 1:2.14.1-2
- Update to 2.14.1

* Mon Mar 13 2006 Ray Strode <rstrode@redhat.com> - 1:2.14.0-1
- Update to 2.14.0

* Tue Mar  7 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.9-4
- Follow Solaris's lead and default to AlwaysRestartServer=True
  (may work around bug 182957)

* Mon Mar  6 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.9-3
- migrate users with baseXsession=/etc/X11/gdm/Xsession to
  /etc/X11/xinit/Xsession

* Mon Mar  6 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.9-2
- disable sounds completely when disabled in configuration file
 (upstream bug 333435)

* Tue Feb 28 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.9-1
- Update to 2.13.0.9
- Use new %%post section, written by
  Michal Jaegermann <michal@harddata.com> (bug 183082)

* Sat Feb 25 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.8-6
- fix a broken link

* Fri Feb 24 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.8-5
- change some /etc/X11 bits in the spec file to /etc

* Sun Feb 19 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.8-3
- add server entry for accel-indirect branch of xorg

* Wed Feb 15 2006 Ray <rstrode@redhat.com> and Matthias <mclasen@redhat.com> - 1:2.13.0.8-2
- malloc memory that is later freed

* Mon Feb 13 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.8-1
- update to 2.13.0.8

* Mon Feb 13 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.7.0.2006.02.12-2
- migrate custom.conf settings with /etc/X11/gdm to /etc/gdm

* Sun Feb 12 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.7.0.2006.02.12-1
- update to cvs snapshot
- move gdm to /etc instead of /etc/X11
- move custom gdm.conf to sysconfdir instead of symlinking from
  datadir (bug 180364)

* Fri Feb 10 2006 Jesse Keating <jkeating@redhat.com> - 1:2.13.0.7-2.1
- bump again for double-long bug on ppc(64)

* Thu Feb  9 2006 Matthias Clasen <mclasen@redhat.com> - 2.13.0.7-2
- Make gdmsetup use consolehelper
- Don't use deprecated pam_stack

* Tue Feb 07 2006 Jesse Keating <jkeating@redhat.com> - 1:2.13.0.7-1.1
- rebuilt for new gcc4.1 snapshot and glibc changes

* Tue Jan 31 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.7-1
- update to 2.13.0.7

* Mon Jan 30 2006 Bill Nottingham <notting@redhat.com>
- silence gdm-safe-restart

* Thu Jan 19 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.5-7
- sed -ie isn't the same as sed -i -e (we want the latter)

* Wed Jan 18 2006 Christopher Aillon <caillon@redhat.com> - 1:2.13.0.5-6
- Add patch to fix clock to default to 24h in locales that expect it (175453)

* Tue Jan 17 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.5-1
- update to 2.13.0.5 (bug 178099)

* Tue Jan 17 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.4-5
- add new theme by Diana Fong, Máirín Duffy, and me

* Mon Jan 16 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.4-4
- improve migration snippet (bug 177443).

* Fri Jan 13 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.4-3
- migrate X server configuration for pre-modular X configurations.
  Problems reported by Dennis Gregorovic <dgregor@redhat.com>

* Mon Jan 9 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.4-2
- use xinit Xsession again.

* Mon Jan 9 2006 Ray Strode <rstrode@redhat.com> - 1:2.13.0.4-1
- update to 2.13.0.4

* Fri Dec 09 2005 Jesse Keating <jkeating@redhat.com>
- rebuilt

* Mon Nov 16 2005 Ray Strode <rstrode@redhat.com> - 1:2.8.0.4-13
- Don't fallback to xsm, try gnome-session instead
- Require xorg-x11-xinit

* Mon Nov 14 2005 Ray Strode <rstrode@redhat.com> - 1:2.8.0.4-12
- Make sure that dbus-launch gets called if available

* Mon Nov 14 2005 Ray Strode <rstrode@redhat.com> - 1:2.8.0.4-11
- Don't use X session / setup files anymore.
- Don't install early login init scripts
- remove xsri dependency
- don't prune language lists anymore

* Sun Nov 13 2005 Jeremy Katz <katzj@redhat.com> - 1:2.8.0.4-10
- also fix default xsession for where its moved in modular X

* Sun Nov 13 2005 Jeremy Katz <katzj@redhat.com> - 1:2.8.0.4-9
- change requirements for modular X
- patch to find x server with modular X

* Thu Oct 20 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.4-8
- redhat-artwork was busted, require new version

* Tue Oct 18 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.4-7
- zero-initialize message buffer,
  bug fixed by Josh Parson (jbparsons@usdavis.edu) (bug 160603)
- fix typo in redhat-artwork requires line

* Mon Oct 17 2005 Steve Grubb <sgrubb@redhat.com> 1:2.8.0.4-6
- add login audit patch (bug 170569)

* Mon Oct 17 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.4-5
- bump redhat-artwork requirement to get rid of the boot
  throbber for now, since it seems to have reappeared
  mysteriously (bug 171025)
p
* Thu Oct 13 2005 Dan Walsh <dwalsh@redhat.com> 1:2.8.0.4-4
- Change to use getseuserbyname

* Thu Sep 28 2005 Dan Walsh <dwalsh@redhat.com> 1:2.8.0.4-3
- Fix selinux not to fail when in permissive mode

* Thu Sep 27 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.4-2
- remove flexiserver from menus

* Thu Sep  8 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.4-1
- update to 2.8.0.4

* Tue Sep  6 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.2-4
- Apply clean up patch from Steve Grubb (gnome bug 315388).

* Tue Aug 30 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.2-3
- Prune language list of installed languages
- Make config file noreplace again (bug 167087).

* Sat Aug 20 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.2-2
- hide throbber

* Fri Aug 19 2005 Ray Strode <rstrode@redhat.com> 1:2.8.0.2-1
- update to 2.8.0.2
- disable early login stuff temporarily

* Thu Aug 18 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-18
- rebuild

* Wed Aug 10 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-17
- Prune uninstalled languages from language list.

* Mon May 23 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-16
- Make sure username/password incorrect message gets displayed
  (bug 158127).
- reread system locale before starting gdm in early login mode
  (bug 158376).

* Thu May 19 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-15
- Take out some syslog spew (bug 157711).

* Thu May 12 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-14
- Fix processing of new-line characters that got broken
  in 2.6.0.8-11 (bug 157442).

* Tue May  3 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-12
- Fix processing of non-ascii characters that got broken
  in 2.6.0.8-11, found by Miloslav Trmac <mitr@redhat.com>,
  (bug 156590).

* Thu Apr 28 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-11
- Fix halt command (bug 156299)
- Process all messages sent to the greeter in a read, not just
  the first

* Wed Apr 27 2005 Jeremy Katz <katzj@redhat.com> - 1:2.6.0.8-10
- silence %%postun

* Tue Apr 26 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-9
- Change default standard greeter theme to clearlooks and
  default graphical greeter theme to Bluecurve specifically.

- Change default path values (bug 154280)

* Mon Apr 25 2005 Matthias Clasen <mclasen@redhat.com> 1:2.6.0.8-8
- for early-login, delay XDMCP initialization until allow-login

* Sun Apr 24 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-7
- calling gdm_debug and g_strdup_printf from signal handlers are
  bad news (Spotted by Mark McLoughlin <markmc@redhat.com>).

* Tue Apr 19 2005 Matthias Clasen <mclasen@redhat.com> 1:2.6.0.8-6
- Add a throbber for early login

* Mon Apr 18 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-5
- Don't install gnome.desktop to /usr/share/xsessions (bug 145791)

* Thu Apr 14 2005 Matthias Clasen <mclasen@redhat.com> 1:2.6.0.8-4
- Don't do early-login if firstboot is going to run
- Make early-login work with timed and automatic logins

* Wed Apr 13 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-3
- Don't hard code dpi setting to 96.0, but instead look at
  Xft.dpi

* Wed Apr 13 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-2
- touch /var/lock/subsys/gdm-early-login so gdm gets killed on
  runlevel changes (bug 154414)
- don't try to use system dpi settings for canvas text (bug 127532)
- merge resource database from displays other than :0

* Sat Apr  2 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.8-1
- update to 2.6.0.8
- add new init scripts to support early-login mode

* Tue Mar 29 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.7-8
- Add a --wait-for-bootup cmdline option.

* Mon Mar 28 2005 Christopher Aillon <caillon@redhat.com>
- rebuilt

* Fri Mar 25 2005 Christopher Aillon <caillon@redhat.com> 1:2.6.0.7-6
- Update the GTK+ theme icon cache on (un)install

* Fri Mar 11 2005 Alexandre Oliva <aoliva@redhat.com> 1:2.6.0.7-5
- fix patch for bug 149899 (fixes bug 150745)

* Wed Mar 09 2005 Than Ngo <than@redhat.com> 1:2.6.0.7-4
- add OnlyShowIn=GNOME;

* Mon Feb 28 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.7-3
- seteuid/egid as user before testing for presence of
  user's home directory (fixes bug 149899)

* Thu Feb 10 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.7-2
- Turn off "switchdesk" mode by default which accidentally got
  turned on by default in 2.6.0.5-4

* Wed Feb  2 2005 Matthias Clasen <mclasen@redhat.com> 1:2.6.0.7-1
- Update to 2.6.0.7

* Tue Jan 25 2005 Ray Strode <rstrode@redhat.com> 1:2.6.0.5-11
- Fix bug in greeter sort-session-list patch where selecting
  a session did nothing (bug 145626)

* Thu Dec 9 2004 Dan Walsh <dwalsh@redhat.com> 1:2.6.0.5-10
- Remove pam_selinux from gdmsetup pam file

* Wed Dec  1 2004  Ray Strode  <rstrode@redhat.com> 1:2.6.0.5-9
- Look up and use username instead of assuming that user entered
  login is cannonical.  Patch from
  Mike Patnode <mike.patnode@centrify.com> (fixes bug 141380).

* Thu Nov 11 2004  Ray Strode  <rstrode@redhat.com> 1:2.6.0.5-8
- Sort session list so that default session comes out on top
  (fixes bug 107324)

* Wed Nov 10 2004  Ray Strode  <rstrode@redhat.com> 1:2.6.0.5-7
- Make desktop file symlink instead of absolute (bug 104390)
- Add flexiserver back to menus

* Wed Oct 20 2004  Ray Strode  <rstrode@redhat.com> 1:2.6.0.5-6
- Clean up xses if the session was successfullly completed.
  (fixes bug #136382)

* Tue Oct 19 2004  Ray Strode  <rstrode@redhat.com> 1:2.6.0.5-5
- Prefer nb_NO over no_NO for Norwegian (fixes bug #136033)

* Thu Oct  7 2004 Alexander Larsson <alexl@redhat.com> - 1:2.6.0.5-4
- Change default greeter theme to "Default", require
  redhat-artwork with Default symlink.

* Wed Sep 29 2004 Ray Strode <rstrode@redhat.com> 1:2.6.0.5-3
- Check if there is a selected node before using iterator.
  (fixes bug #133329).

* Fri Sep 24 2004 Ray Strode <rstrode@redhat.com> 1:2.6.0.5-2
- Don't mess with gdmphotosetup categories.  Upstream categories
  are fine.

* Mon Sep 20 2004 Ray Strode <rstrode@redhat.com> 1:2.6.0.5-1
- update to 2.6.0.5

* Tue Aug 3 2004 Matthias Clasen <mclasen@redhat.com> 1:2.6.0.3-5
- fix messed up changelog

* Tue Aug 3 2004 Matthias Clasen <mclasen@redhat.com> 1:2.6.0.3-4
- rebuilt

* Thu Aug 2 2004 Ray Strode <rstrode@redhat.com> 1:2.6.0.3-3
- rebuilt

* Mon Jul 26 2004 Bill Nottingham <notting@redhat.com> 1:2.6.0.3-2
- fix theme (#128599)

* Thu Jun 17 2004 Ray Strode <rstrode@redhat.com> 1:2.6.0.3-1
- update to 2.6.0.3 (fixes bug #117677)

* Tue Jun 15 2004 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Fri May 21 2004 Matthias Clasen <mclasen@redhat.com> 1:2.6.0.0-5
- rebuild

* Mon May 17 2004 Than Ngo <than@redhat.com> 1:2.6.0.0-4
- add patch to build gdm-binary with PIE

* Thu Apr 22 2004 Mark McLoughlin <markmc@redhat.com> - 1:2.6.0.0-3
- Update the "use switchdesk" message to only be display when
  switchdesk-gui is installed and to not reference a non existant
  menu item (bug #121460)

* Fri Apr  2 2004 Colin Walters <walters@redhat.com> 1:2.6.0.0-2
- Always put session errors in /tmp, in preparation for
  completely preventing gdm from writing to /home/

* Thu Apr  1 2004 Alex Larsson <alexl@redhat.com> 1:2.6.0.0-1
- update to 2.6.0.0

* Tue Mar 16 2004 Dan Walsh <dwalsh@redhat.com> 1:2.5.90.3-1
- Use selinux patch again

* Tue Mar 16 2004 Dan Walsh <dwalsh@redhat.com> 1:2.5.90.3-1
- Stop using selinux patch and use pam_selinux instead.

* Wed Mar 10 2004 Alex Larsson <alexl@redhat.com> 1:2.5.90.2-1
- update to 2.5.90.2

* Tue Mar 02 2004 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Thu Feb 26 2004 Alexander Larsson <alexl@redhat.com> 1:2.5.90.1-1
- update to 2.5.90.1

* Fri Feb 13 2004 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Tue Feb 03 2004 Warren Togami <wtogami@redhat.com> 1:2.4.4.5-9
- add two lines to match upstream CVS to xdmcp_sessions.patch
  Fully resolves #110315 and #113154

* Sun Feb 01 2004 Warren Togami <wtogami@redhat.com> 1:2.4.4.5-8
- patch30 xdmcp_session counter fix from gdm-2.5.90.0 #110315
- automake14 really needed, not automake
- BR libcroco-devel, libcroco-devel, libattr-devel, gettext
- conditionally BR libselinux-devel
- explicit epoch in all deps
- make the ja.po time format change with a sed expression rather than
  overwriting the whole file (Petersen #113995)

* Thu Jan 29 2004 Jeremy Katz <katzj@redhat.com> - 1:2.4.4.5-7
- fix build with current auto*

* Tue Jan 27 2004 Jeremy Katz <katzj@redhat.com> 1:2.4.4.5-5
- try a simple rebuild for libcroco abi change

* Mon Jan 26 2004 Dan Walsh <dwalsh@redhat.com> 1:2.4.4.5-4
- Fix call to is_selinux_enabled

* Fri Jan 16 2004 Dan Walsh <dwalsh@redhat.com> 1:2.4.4.5-3
- Use /sbin/reboot and /sbin/poweroff instead of consolehelper version

* Thu Oct 30 2003 Dan Walsh <dwalsh@redhat.com> 1:2.4.4.5-2.sel
- turn on SELinux

* Mon Oct 20 2003 Jonathan Blandford <jrb@redhat.com> 2:2.4.4.5-1
- get rid of the teal

* Fri Oct 17 2003 Jonathan Blandford <jrb@redhat.com> 1:2.4.4.5-1
- new version

* Thu Oct  9 2003 Jonathan Blandford <jrb@redhat.com> 1:2.4.4.3-6.sel
- new patch from George to fix #106189
- change bg color in rhdefaults patch
- turn off SELinux

* Thu Oct 8 2003 Dan Walsh <dwalsh@redhat.com> 1:2.4.4.3-6.sel
- turn on SELinux

* Tue Oct  7 2003 Alexander Larsson <alexl@redhat.com> 1:2.4.4.3-5
- Fix greeter line-breaking crash (rest of #106189)

* Tue Oct  7 2003 Alexander Larsson <alexl@redhat.com> 1:2.4.4.3-4
- Set the BaseXSession properly in the config.
- This fixes parts of bug #106189

* Mon Oct  6 2003 Havoc Pennington <hp@redhat.com> 1:2.4.4.3-3
- change DefaultSession=Default.desktop to DefaultSession=default.desktop
- SELinux off again

* Fri Oct 3 2003 Dan Walsh <dwalsh@redhat.com> 1:2.4.4.3-2.sel
- turn on SELinux

* Thu Oct  2 2003 Havoc Pennington <hp@redhat.com> 1:2.4.4.3-1
- 2.4.4.3
- --without-selinux for now, since libselinux not in the buildroot

* Mon Sep 8 2003 Dan Walsh <dwalsh@redhat.com> 1:2.4.4.0-4
- turn off SELinux

* Fri Sep 5 2003 Dan Walsh <dwalsh@redhat.com> 1:2.4.4.0-3.sel
- turn on SELinux

* Thu Sep  4 2003 Alexander Larsson <alexl@redhat.com> 1:2.4.4.0-2
- Use the right default session (#103546)

* Wed Sep  3 2003 Alexander Larsson <alexl@redhat.com> 1:2.4.4.0-1
- update to 2.4.4.0
- update to georges new selinux patch

* Fri Aug 29 2003 Elliot Lee <sopwith@redhat.com> 1:2.4.2.102-2
- Remove scrollkeeper files

* Tue Aug 26 2003 George Lebl <jirka@5z.com> 1:2.4.2.102-1
- updated to 2.4.2.102
- removed outdated patches
- Use Xsetup_0 only for :0 since that's the way it works
  for xdm
- remove the gnome.desktop file, its going into gnome-session

* Thu Aug 14 2003 Havoc Pennington <hp@redhat.com> 1:2.4.1.6-1
- update to latest bugfix version on george's advice
- remove setlocale patch that's upstream
- remove console setup patches that are upstream

* Thu Jun 12 2003 Dan Walsh <dwalsh@redhat.com> 2.4.1.3-9
- Port to SELinux

* Wed Jun 04 2003 Elliot Lee <sopwith@redhat.com>
- rebuilt

* Tue Jun  3 2003 Jeff Johnson <jbj@redhat.com>
- add explicit epoch's where needed.

* Sun May 04 2003 Florian La Roche <Florian.LaRoche@redhat.de>
- fix post: localstatedir -> _localstatedir

* Thu May  1 2003 Havoc Pennington <hp@redhat.com> 1:2.4.1.3-6
- enable UTF-8 for CJK

* Mon Feb 24 2003 Elliot Lee <sopwith@redhat.com>
- debuginfo rebuild

* Thu Feb 20 2003 Owen Taylor <otaylor@redhat.com>
- Run the error dialogs under /bin/sh --login, so we
  get lang.sh, and thus unicode_start running. Fixes
  the X-doesn't-start dialog showing up as random
  blinking characters.

* Fri Feb 14 2003 Havoc Pennington <hp@redhat.com> 1:2.4.1.3-2
- nuke buildreq Xft

* Wed Feb  5 2003 Havoc Pennington <hp@redhat.com> 1:2.4.1.3-1
- upgrade to 2.4.1.3

* Mon Feb  3 2003 Matt Wilson <msw@redhat.com> 1:2.4.1.1-6
- added gdm-2.4.1.1-64bit.patch to fix 64 bit crash in cookie
  generation (#83334)

* Mon Feb  3 2003 Owen Taylor <otaylor@redhat.com>
- Add patch to fix problem where setting LC_COLLATE=C would give LC_MESSAGES=wa_BE (#82019)

* Thu Jan 30 2003 Matt Wilson <msw@redhat.com> 1:2.4.1.1-3
- fix pam.d entry, pam_env wasn't properly patched
- disable optimizations on x86_64 to work around gcc bug

* Wed Jan 22 2003 Tim Powers <timp@redhat.com>
- rebuilt

* Mon Jan 20 2003 Owen Taylor <otaylor@redhat.com>
- Upgrade to 2.4.1.1 (Fixes #81907)
- Redirect stdout of kill to /dev/null (#80814)

* Thu Jan  9 2003 Havoc Pennington <hp@redhat.com>
- 2.4.1.0
- add patch from george to ask "are you sure?" for shutdown/reboot since it's now just one click away

* Thu Dec 19 2002 Havoc Pennington <hp@redhat.com>
- 2.4.0.12
- update new patch for no-utf8-in-cjk
- drop patch to photo setup, now upstream
- drop confdocs patch now upstream
- move all the gdm.conf changes into single "rhconfig" patch
- remove "sid-fix" patch now upstream

* Mon Nov 11 2002 Nalin Dahyabhai <nalin@redhat.com> 2.4.0.7-14
- remove the directory part of module specifications from the PAM config files,
  allowing the same PAM config to work for either arch on multilib boxes

* Thu Sep  5 2002 Owen Taylor <otaylor@redhat.com>
- Change zh_CN entry in language menu to zh_CN.GB18030

* Thu Sep  5 2002 Akira TAGOH <tagoh@redhat.com> 2.4.0.7-12
- copied gdm-ja.po to ja.po.

* Mon Sep  2 2002 Owen Taylor <otaylor@redhat.com>
- Fix problem where gdm was opening ~/.xsession-errors itself to bad effect

* Sat Aug 31 2002 Havoc Pennington <hp@redhat.com>
- include ja.po with new date format

* Wed Aug 28 2002 Havoc Pennington <hp@redhat.com>
- remove noreplace on gdm.conf #71309
- make gnome-gdmsetup absolute, #72910

* Wed Aug 28 2002 Havoc Pennington <hp@redhat.com>
- put /usr/X11R6/bin in path for now fixes #72781
- use proper i18n algorithm for word wrap, #71937
- remove greek text from language picker due to lack
  of greek font
- reorder PAM config file #72657

* Wed Aug 28 2002 Havoc Pennington <hp@redhat.com>
- improve gdmsetup icon
- remove GNOME session, we will instead put it in gnome-session
- apply patch from george to make gdmphotosetup file selector
  work

* Mon Aug 26 2002 Elliot Lee <sopwith@redhat.com> 2.4.0.7-6
- Patches for #64902, #66486, #68483, #71308
- post-install script changes from the gdm.spec mentioned in #70965
- noreplace on gdm.conf for #71309

* Sun Aug 25 2002 Havoc Pennington <hp@redhat.com>
- put in a patch from george to fix some setsid()/kill() confusion
  possibly fixing #72295
- turn off UseCirclesInEntry for now, fixes #72433

* Tue Aug 20 2002 Alexander Larsson <alexl@redhat.com>
- Set UseCirclesInEntry to true in config

* Thu Aug 15 2002 Havoc Pennington <hp@redhat.com>
- rename Gnome session to GNOME, this was just bugging me

* Thu Aug  8 2002 Havoc Pennington <hp@redhat.com>
- 2.4.0.7 with bugfixes George kindly did for me,
  including mnemonics for the graphical greeter
- use Wonderland gtk theme for the nongraphical greeter
- remove patches that are now upstream

* Tue Jul 30 2002 Havoc Pennington <hp@redhat.com>
- update rhconfig patch
- use pam_timestamp for the config tool
- link to a desktop file in redhat-menus
- update .gnome2 patch, filed upstream bug
- 2.4.0.4
- rebuild with new gail, librsvg2

* Tue Jun 25 2002 Owen Taylor <otaylor@redhat.com>
- Require redhat-artwork, make the default greeter theme Wonderland
- Look for all configuration in .gnome2 not .gnome. This avoids problems
  with changes in the set of session/lang.
- Remove English from locale.alias, make most locales UTF-8
- Call find_lang with the right name

* Sun Jun 16 2002 Havoc Pennington <hp@redhat.com>
- rebuild for new libs
- put gdm-autologin pam config file in file list, hope
  its absence wasn't deliberate
- use desktop-file-install

* Mon Jun 10 2002 Havoc Pennington <hp@redhat.com>
- rebuild in different environment

* Mon Jun 10 2002 Havoc Pennington <hp@redhat.com>
- 2.4.0.0

* Tue May 21 2002 Havoc Pennington <hp@redhat.com>
- rebuild in different environment

* Tue May 21 2002 Havoc Pennington <hp@redhat.com>
- 2.3.90.3

* Tue May 14 2002 Matt Wilson <msw@redhat.com> 2.3.90.2.90-1
- pulled from current CVS, named it 2.3.90.2.90-1

* Thu Feb 14 2002 Havoc Pennington <hp@redhat.com>
- rebuild for new libs
- add URL tag

* Mon Feb 11 2002 Alex Larsson <alexl@redhat.com> 2.3.90.1.90-1
- Updated to a cvs snapshot that has the new greeter.

* Thu Jan 24 2002 Havoc Pennington <hp@redhat.com>
- rebuild in rawhide

* Tue Sep  4 2001 Havoc Pennington <hp@redhat.com>
- fix #52997 (ukrainian in language list)

* Fri Aug 31 2001 Havoc Pennington <hp@redhat.com>
- Add po files from sources.redhat.com

* Mon Aug 27 2001 Havoc Pennington <hp@redhat.com>
- Add po files from sources.redhat.com

* Wed Aug 15 2001 Nalin Dahyabhai <nalin@redhat.com>
- set SESSION to true in console.apps control file

* Tue Aug 14 2001 Havoc Pennington <hp@redhat.com>
- change default title font to work in CJK, #51698

* Wed Aug  8 2001 Bill Nottingham <notting@redhat.com>
- fix %%pre for using /var/gdm as home dir

* Sun Aug  5 2001 Nalin Dahyabhai <nalin@redhat.com>
- Tweak PAM setup for gdmconfig to match other consolehelper users

* Fri Aug  3 2001 Owen Taylor <otaylor@redhat.com>
- Set RUNNING_UNDER_GDM when running display init script
- Run xsri as the background program

* Thu Aug 02 2001 Havoc Pennington <hp@redhat.com>
- Change how session switching works, #49480
- don't offer to make Failsafe the default, #49479

* Thu Aug 02 2001 Havoc Pennington <hp@redhat.com>
- clean up some format string mess, and don't
  log username to syslog, #5681
- own some directories #50692

* Wed Aug 01 2001 Havoc Pennington <hp@redhat.com>
- require/buildrequire latest gnome-libs, to compensate
  for upstream crackrock. #50554

* Tue Jul 31 2001 Havoc Pennington <hp@redhat.com>
- get rid of GiveConsole/TakeConsole, bug #33710

* Sun Jul 22 2001 Havoc Pennington <hp@redhat.com>
- use Raleigh theme for gdm

* Thu Jul 19 2001 Havoc Pennington <hp@redhat.com>
- depend on usermode, xinitrc

* Thu Jul 19 2001 Havoc Pennington <hp@redhat.com>
- build requires pam-devel, should fix #49448

* Mon Jul 16 2001 Havoc Pennington <hp@redhat.com>
- log to /var/log/gdm/*

* Mon Jul 16 2001 Havoc Pennington <hp@redhat.com>
- make Halt... power off

* Tue Jul 10 2001 Havoc Pennington <hp@redhat.com>
- gdm user's homedir to /var/gdm not /home/gdm

* Mon Jul 09 2001 Havoc Pennington <hp@redhat.com>
- put pam.d/gdm back in file list

* Sun Jul 08 2001 Havoc Pennington <hp@redhat.com>
- upgrade to 2.2.3.1, pray this fixes more than it breaks

* Thu Jul 05 2001 Havoc Pennington <hp@redhat.com>
- add "rpm" user to those not to show in greeter

* Tue Jul 03 2001 Havoc Pennington <hp@redhat.com>
- Upgrade to 2.2.3
- require usermode since configure script now checks for it

* Fri Jun 01 2001 Havoc Pennington <hp@redhat.com>
- Prereq for scrollkeeper-update

* Thu May 30 2001 Havoc Pennington <hp@redhat.com>
- New CVS snap with the "no weird sessions" options;
  more default settings changes

* Wed May 30 2001 Havoc Pennington <hp@redhat.com>
- Change a bunch of default settings; remaining fixes will involve C hacking

* Wed May 30 2001 Havoc Pennington <hp@redhat.com>
- After, oh, 2 years or so, finally upgrade version and set
  release to 1. Remove all hacks and patches, pretty much;
  this will break a few things, will be putting them back
  via GNOME CVS. All changes should go in 'gdm2' module in
  CVS for now.

  This RPM enables all kinds of features that I'm going to turn
  off shortly, so don't get excited about them. ;-)

* Thu Mar 22 2001 Nalin Dahyabhai <nalin@redhat.com>
- reinitialize pam credentials after calling initgroups() -- the
  credentials may be group memberships

* Mon Mar 19 2001 Owen Taylor <otaylor@redhat.com>
- Fix colors patch

* Thu Mar 15 2001 Havoc Pennington <hp@redhat.com>
- translations

* Mon Mar  5 2001 Preston Brown <pbrown@redhat.com>
- don't screw up color map on 8 bit displays

* Fri Feb 23 2001 Trond Eivind Glomsrød <teg@redhat.com>
- langify
- Don't define and use "ver" and "nam" at the top of the spec file
- use %%{_tmppath}

* Tue Feb 13 2001 Tim Powers <timp@redhat.com>
- don't allow gdm to show some system accounts in the browser bugzilla
  #26898

* Fri Jan 19 2001 Akira TAGOH <tagoh@redhat.com>
- Updated Japanese translation.

* Tue Jan 02 2001 Havoc Pennington <hp@redhat.com>
- add another close() to the fdleak patch, bugzilla #22794

* Sun Aug 13 2000 Owen Taylor <otaylor@redhat.com>
- Return to toplevel main loop and start Xdcmp if enabled
  (Bug #16106)

* Fri Aug 11 2000 Jonathan Blandford <jrb@redhat.com>
- Up Epoch and release

* Wed Aug 02 2000 Havoc Pennington <hp@redhat.com>
- Requires Xsession script

* Wed Jul 19 2000 Owen Taylor <otaylor@redhat.com>
- Italian is better as it_IT than it_CH (bugzilla 12425)

* Mon Jul 17 2000 Jonathan Blandford <jrb@redhat.com>
- Don't instally gdmconfig as it doesn't work.

* Fri Jul 14 2000 Havoc Pennington <hp@redhat.com>
- Rearrange code to avoid calling innumerable system calls
  in a signal handler

* Fri Jul 14 2000 Havoc Pennington <hp@redhat.com>
- Verbose debug spew for infinite loop stuff

* Fri Jul 14 2000 Havoc Pennington <hp@redhat.com>
- Try to fix infinite loops on X server failure

* Thu Jul 13 2000 Prospector <bugzilla@redhat.com>
- automatic rebuild

* Thu Jun 29 2000 Havoc Pennington <hp@redhat.com>
- Remove Docdir

* Mon Jun 19 2000 Havoc Pennington <hp@redhat.com>
- Fix file descriptor leak (Bugzilla 12301)

* Mon Jun 19 2000 Havoc Pennington <hp@redhat.com>
- Apply security errata patch we released for 6.2
- Add Gnome.session back, don't know when it disappeared or why

* Thu Jun  1 2000 Nalin Dahyabhai <nalin@redhat.com>
- modify PAM setup to use system-auth

* Fri May 19 2000 Havoc Pennington <hp@redhat.com>
- rebuild for the Winston tree

* Fri Feb 04 2000 Havoc Pennington <hp@redhat.com>
- Modify Default.session and Failsafe.session not to add -login option to bash
- exec the session scripts with the user's shell with a hyphen prepended
- doesn't seem to actually work yet with tcsh, but it doesn't seem to
  break anything. needs a look to see why it doesn't work

* Fri Feb 04 2000 Havoc Pennington <hp@redhat.com>
- Link PreSession/Default to xdm/GiveConsole
- Link PostSession/Default to xdm/TakeConsole

* Fri Feb 04 2000 Havoc Pennington <hp@redhat.com>
- Fix the fix to the fix (8877)
- remove docs/gdm-manual.txt which doesn't seem to exist from %%doc

* Fri Feb 04 2000 Havoc Pennington <hp@redhat.com>
- Enhance 8877 fix by not deleting the "Please login"
  message

* Fri Feb 04 2000 Havoc Pennington <hp@redhat.com>
- Try to fix bug 8877 by clearing the message below
  the entry box when the prompt changes. may turn
  out to be a bad idea.

* Mon Jan 17 2000 Elliot Lee <sopwith@redhat.com>
- Fix bug #7666: exec Xsession instead of just running it

* Mon Oct 25 1999 Jakub Jelinek <jakub@redhat.com>
- Work around so that russian works (uses koi8-r instead
  of the default iso8859-5)

* Tue Oct 12 1999 Owen Taylor <otaylor@redhat.com>
- Try again

* Tue Oct 12 1999 Owen Taylor <otaylor@redhat.com>
- More fixes for i18n

* Tue Oct 12 1999 Owen Taylor <otaylor@redhat.com>
- Fixes for i18n

* Fri Sep 26 1999 Elliot Lee <sopwith@redhat.com>
- Fixed pipewrite bug (found by mkj & ewt).

* Fri Sep 17 1999 Michael Fulbright <drmike@redhat.com>
- added requires for pam >= 0.68

* Fri Sep 10 1999 Elliot Lee <sopwith@redhat.com>
- I just update this package every five minutes, so any recent changes are my fault.

* Thu Sep 02 1999 Michael K. Johnson <johnsonm@redhat.com>
- built gdm-2.0beta2

* Mon Aug 30 1999 Michael K. Johnson <johnsonm@redhat.com>
- built gdm-2.0beta1

* Tue Aug 17 1999 Michael Fulbright <drmike@redhat.com>
- included rmeier@liberate.com patch for tcp socket X connections

* Mon Apr 19 1999 Michael Fulbright <drmike@redhat.com>
- fix to handling ancient gdm config files with non-standard language specs
- dont close display connection for xdmcp connections, else we die if remote
  end dies.

* Fri Apr 16 1999 Michael Fulbright <drmike@redhat.com>
- fix language handling to set GDM_LANG variable so gnome-session
  can pick it up

* Wed Apr 14 1999 Michael Fulbright <drmike@redhat.com>
- fix so certain dialog boxes dont overwrite background images

* Wed Apr 14 1999 Michael K. Johnson <johnsonm@redhat.com>
- do not specify -r 42 to useradd -- it doesn't know how to fall back
  if id 42 is already taken

* Fri Apr 9 1999 Michael Fulbright <drmike@redhat.com>
- removed suspend feature

* Mon Apr 5 1999 Jonathan Blandford <jrb@redhat.com>
- added patch from otaylor to not call gtk funcs from a signal.
- added patch to tab when username not added.
- added patch to center About box (and bring up only one) and ignore "~"
  and ".rpm" files.

* Fri Mar 26 1999 Michael Fulbright <drmike@redhat.com>
- fixed handling of default session, merged all gdmgreeter patches into one

* Tue Mar 23 1999 Michael Fulbright <drmike@redhat.com>
- remove GNOME/KDE/AnotherLevel session scripts, these have been moved to
  the appropriate packages instead.
- added patch to make option menus always active (security problem otherwise)
- added jrb's patch to disable stars in passwd entry field

* Fri Mar 19 1999 Michael Fulbright <drmike@redhat.com>
- made sure /usr/bin isnt in default path twice
- strip binaries

* Wed Mar 17 1999 Michael Fulbright <drmike@redhat.com>
- fixed to use proper system path when root logs in

* Tue Mar 16 1999 Michael Fulbright <drmike@redhat.com>
- linked Init/Default to Red Hat default init script for xdm
- removed logo from login dialog box

* Mon Mar 15 1999 Michael Johnson <johnsonm@redhat.com>
- pam_console integration

* Tue Mar 09 1999 Michael Fulbright <drmike@redhat.com>
- added session files for GNOME/KDE/AnotherLevel/Default/Failsafe
- patched gdmgreeter to not complete usernames
- patched gdmgreeter to not safe selected session permanently
- patched gdmgreeter to center dialog boxes

* Mon Mar 08 1999 Michael Fulbright <drmike@redhat.com>
- removed comments from gdm.conf file, these are not parsed correctly

* Sun Mar 07 1999 Michael Fulbright <drmike@redhat.com>
- updated source line for accuracy

* Fri Feb 26 1999 Owen Taylor <otaylor@redhat.com>
- Updated patches for 1.0.0
- Fixed some problems in 1.0.0 with installation directories
- moved /usr/var/gdm /var/gdm

* Thu Feb 25 1999 Michael Fulbright <drmike@redhat.com>
- moved files from /usr/etc to /etc

* Tue Feb 16 1999 Michael Johnson <johnsonm@redhat.com>
- removed commented-out #1 definition -- put back after testing gnome-libs
  comment patch

* Sat Feb 06 1999 Michael Johnson <johnsonm@redhat.com>
- initial packaging
