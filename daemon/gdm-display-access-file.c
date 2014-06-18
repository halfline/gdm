/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * gdm-display-access-file.c - Abstraction around xauth cookies
 *
 * Copyright (C) 2007 Ray Strode <rstrode@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <X11/X.h>
#include <X11/Xauth.h>

#include "gdm-display-access-file.h"
#include "gdm-common.h"

struct _GdmDisplayAccessFilePrivate
{
        char *username;
        FILE *fp;
        char *path;
};

#ifndef GDM_DISPLAY_ACCESS_COOKIE_SIZE
#define GDM_DISPLAY_ACCESS_COOKIE_SIZE 16
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

static void gdm_display_access_file_finalize (GObject * object);

enum
{
        PROP_0 = 0,
        PROP_USERNAME,
        PROP_PATH
};

G_DEFINE_TYPE (GdmDisplayAccessFile, gdm_display_access_file, G_TYPE_OBJECT)

static void
gdm_display_access_file_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
        GdmDisplayAccessFile *access_file;

        access_file = GDM_DISPLAY_ACCESS_FILE (object);

        switch (prop_id) {
            case PROP_USERNAME:
                g_value_set_string (value, access_file->priv->username);
                break;

            case PROP_PATH:
                g_value_set_string (value, access_file->priv->path);
                break;

            default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gdm_display_access_file_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
        GdmDisplayAccessFile *access_file;

        access_file = GDM_DISPLAY_ACCESS_FILE (object);

        switch (prop_id) {
            case PROP_USERNAME:
                g_assert (access_file->priv->username == NULL);
                access_file->priv->username = g_value_dup_string (value);
                break;

            default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gdm_display_access_file_class_init (GdmDisplayAccessFileClass *access_file_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (access_file_class);

        object_class->finalize = gdm_display_access_file_finalize;
        object_class->get_property = gdm_display_access_file_get_property;
        object_class->set_property = gdm_display_access_file_set_property;

        param_spec = g_param_spec_string ("username",
                                          "Username",
                                          "Owner of Xauthority file",
                                          NULL,
                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
        g_object_class_install_property (object_class, PROP_USERNAME, param_spec);
        param_spec = g_param_spec_string ("path",
                                          "Path",
                                          "Path to Xauthority file",
                                          NULL,
                                          G_PARAM_READABLE);
        g_object_class_install_property (object_class, PROP_PATH, param_spec);
        g_type_class_add_private (access_file_class, sizeof (GdmDisplayAccessFilePrivate));
}

static void
gdm_display_access_file_init (GdmDisplayAccessFile *access_file)
{
        access_file->priv = G_TYPE_INSTANCE_GET_PRIVATE (access_file,
                                                         GDM_TYPE_DISPLAY_ACCESS_FILE,
                                                         GdmDisplayAccessFilePrivate);
}

static void
gdm_display_access_file_finalize (GObject *object)
{
        GdmDisplayAccessFile *file;
        GObjectClass *parent_class;

        file = GDM_DISPLAY_ACCESS_FILE (object);
        parent_class = G_OBJECT_CLASS (gdm_display_access_file_parent_class);

        if (file->priv->fp != NULL) {
            gdm_display_access_file_close (file);
        }
        g_assert (file->priv->path == NULL);

        if (file->priv->username != NULL) {
                g_free (file->priv->username);
                file->priv->username = NULL;
                g_object_notify (object, "username");
        }

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

GQuark
gdm_display_access_file_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("gdm-display-access-file");
        }

        return error_quark;
}

GdmDisplayAccessFile *
gdm_display_access_file_new (const char *username)
{
        GdmDisplayAccessFile *access_file;
        g_return_val_if_fail (username != NULL, NULL);

        access_file = g_object_new (GDM_TYPE_DISPLAY_ACCESS_FILE,
                                    "username", username,
                                    NULL);

        return access_file;
}

static gboolean
get_uid_for_user (const char *username,
                  uid_t      *uid)
{
        gboolean res;
        char *command_line;
        char *output, *error_output;
        gint exit_code;
        GError *error;

        g_assert (username != NULL);
        g_assert (uid != NULL);

        command_line = g_strdup_printf ("id -u -- '%s'", username);

        error = NULL;
        output = NULL;
        error_output = NULL;
        res = g_spawn_command_line_sync (command_line, &output, &error_output, &exit_code, &error);
        if (!res) {
                g_warning ("Could not run '%s'", error->message);

                if (error != NULL) {
                        g_warning ("(error output: %s)", error_output);
                        g_error_free (error);
                }
        } else if (!WIFEXITED (exit_code)) {
                if (WIFSIGNALED (exit_code)) {
                        g_warning ("Command '%s' died from signal %d",
                                   command_line, WTERMSIG (exit_code));
                } else {
                        g_warning ("Command '%s' died unexpectedly",
                                   command_line);
                }
        } else if (WEXITSTATUS (exit_code) != 0) {
                g_warning ("Command '%s' exited unsuccessfully with code: %d",
                           command_line, WEXITSTATUS (exit_code));
        } else {
                long number;
                char *end;
                static const long long max_uid = (1LL << (sizeof (uid_t) * 8)) - 1;

                g_assert (sizeof (uid_t) <= sizeof (long));

                number = strtol (output, &end, 10);

                if (*end != '\n' && *end != '\r' && *end != '\0') {
                        res = FALSE;
                        g_warning ("Command '%s' return unparseable output: '%s'",
                                   command_line, output);
                } else if (number < 0 || number > max_uid) {
                        g_warning ("Command '%s' return out of range uid: %ld",
                                   command_line, number);
                        res = FALSE;
                } else {
                        res = TRUE;
                        *uid = (uid_t) number;
                }
        }
        g_free (output);
        g_free (error_output);
        g_free (command_line);

        return res;
}

static gboolean
get_gid_for_user (const char *username,
                  gid_t      *gid)
{
        gboolean res;
        char *command_line;
        char *output, *error_output;
        gint exit_code;
        GError *error;

        g_assert (username != NULL);
        g_assert (gid != NULL);

        command_line = g_strdup_printf ("id -g -- '%s'", username);

        error = NULL;
        output = NULL;
        error_output = NULL;
        res = g_spawn_command_line_sync (command_line, &output, &error_output, &exit_code, &error);
        if (!res) {
                g_warning ("Could not run '%s'", error->message);

                if (error != NULL) {
                        g_warning ("(error output: %s)", error_output);
                        g_error_free (error);
                }
        } else if (!WIFEXITED (exit_code)) {
                if (WIFSIGNALED (exit_code)) {
                        g_warning ("Command '%s' died from signal %d",
                                   command_line, WTERMSIG (exit_code));
                } else {
                        g_warning ("Command '%s' died unexpectedly",
                                   command_line);
                }
        } else if (WEXITSTATUS (exit_code) != 0) {
                g_warning ("Command '%s' exited unsuccessfully with code: %d",
                           command_line, WEXITSTATUS (exit_code));
        } else {
                long number;
                char *end;
                static const long long max_gid = (1LL << (sizeof (gid_t) * 8)) - 1;

                g_assert (sizeof (gid_t) <= sizeof (long));

                number = strtol (output, &end, 10);

                if (*end != '\n' && *end != '\r' && *end != '\0') {
                        res = FALSE;
                        g_warning ("Command '%s' return unparseable output: '%s'",
                                   command_line, output);
                } else if (number < 0 || number > max_gid) {
                        g_warning ("Command '%s' return out of range gid: %ld",
                                   command_line, number);
                        res = FALSE;
                } else {
                        res = TRUE;
                        *gid = (gid_t) number;
                }
        }
        g_free (output);
        g_free (error_output);
        g_free (command_line);

        return res;
}

static gboolean
_get_uid_and_gid_for_user (const char *username,
                           uid_t      *uid,
                           gid_t      *gid)
{
        g_assert (username != NULL);
        g_assert (uid != NULL);
        g_assert (gid != NULL);

        if (!get_uid_for_user (username, uid)) {
                return FALSE;
        }

        if (!get_gid_for_user (username, gid)) {
                return FALSE;
        }

        return TRUE;
}

static void
clean_up_stale_auth_subdirs (void)
{
        GDir *dir;
        const char *filename;

        dir = g_dir_open (GDM_XAUTH_DIR, 0, NULL);

        if (dir == NULL) {
                return;
        }

        while ((filename = g_dir_read_name (dir)) != NULL) {
                char *path;

                path = g_build_filename (GDM_XAUTH_DIR, filename, NULL);

                /* Will only succeed if the directory is empty
                 */
                g_rmdir (path);
                g_free (path);
        }
        g_dir_close (dir);
}

static FILE *
_create_xauth_file_for_user (const char  *username,
                             char       **filename,
                             GError     **error)
{
        char   *template;
        const char *dir_name;
        char   *auth_filename;
        int     fd;
        FILE   *fp;
        uid_t   uid;
        gid_t   gid;

        g_assert (filename != NULL);

        *filename = NULL;

        template = NULL;
        auth_filename = NULL;
        fp = NULL;
        fd = -1;

        /* Create directory if not exist, then set permission 0711 and ownership root:gdm */
        if (g_file_test (GDM_XAUTH_DIR, G_FILE_TEST_IS_DIR) == FALSE) {
                g_unlink (GDM_XAUTH_DIR);
                if (g_mkdir (GDM_XAUTH_DIR, 0711) != 0) {
                        g_set_error (error,
                                     G_FILE_ERROR,
                                     g_file_error_from_errno (errno),
                                     "%s", g_strerror (errno));
                        goto out;
                }

                g_chmod (GDM_XAUTH_DIR, 0711);
                _get_uid_and_gid_for_user (GDM_USERNAME, &uid, &gid);
                if (chown (GDM_XAUTH_DIR, 0, gid) != 0) {
                        g_warning ("Unable to change owner of '%s'",
                                   GDM_XAUTH_DIR);
                }
        } else {
                /* if it does exist make sure it has correct mode 0711 */
                g_chmod (GDM_XAUTH_DIR, 0711);

                /* and clean up any stale auth subdirs */
                clean_up_stale_auth_subdirs ();
        }

        if (!_get_uid_and_gid_for_user (username, &uid, &gid)) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_USER_INFO,
                             _("could not find user \"%s\" on system"),
                             username);
                goto out;

        }

        template = g_strdup_printf (GDM_XAUTH_DIR
                                    "/auth-for-%s-XXXXXX",
                                    username);

        g_debug ("GdmDisplayAccessFile: creating xauth directory %s", template);
        /* Initially create with mode 01700 then later chmod after we create database */
        errno = 0;
        dir_name = gdm_make_temp_dir (template);
        if (dir_name == NULL) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to create temp dir from tempalte '%s': %s",
                             template,
                             g_strerror (errno));
                goto out;
        }

        g_debug ("GdmDisplayAccessFile: chowning %s to %u:%u",
                 dir_name, (guint)uid, (guint)gid);
        errno = 0;
        if (chown (dir_name, uid, gid) < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to change permission of '%s': %s",
                             dir_name,
                             g_strerror (errno));
                goto out;
        }

        auth_filename = g_build_filename (dir_name, "database", NULL);

        g_debug ("GdmDisplayAccessFile: creating %s", auth_filename);
        /* mode 00600 */
        errno = 0;
        fd = g_open (auth_filename,
                     O_RDWR | O_CREAT | O_EXCL | O_BINARY,
                     S_IRUSR | S_IWUSR);

        if (fd < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to open '%s': %s",
                             auth_filename,
                             g_strerror (errno));
                goto out;
        }

        g_debug ("GdmDisplayAccessFile: chowning %s to %u:%u", auth_filename, (guint)uid, (guint)gid);
        errno = 0;
        if (fchown (fd, uid, gid) < 0) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "Unable to change owner for '%s': %s",
                             auth_filename,
                             g_strerror (errno));
                close (fd);
                fd = -1;
                goto out;
        }

        /* now open up permissions on per-session directory */
        g_debug ("GdmDisplayAccessFile: chmoding %s to 0711", dir_name);
        g_chmod (dir_name, 0711);

        errno = 0;
        fp = fdopen (fd, "w");
        if (fp == NULL) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "%s", g_strerror (errno));
                close (fd);
                fd = -1;
                goto out;
        }

        *filename = auth_filename;
        auth_filename = NULL;

        /* don't close it */
        fd = -1;
out:
        g_free (template);
        g_free (auth_filename);
        if (fd != -1) {
                close (fd);
        }

        return fp;
}

gboolean
gdm_display_access_file_open (GdmDisplayAccessFile  *file,
                              GError               **error)
{
        GError *create_error;

        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->fp == NULL, FALSE);
        g_return_val_if_fail (file->priv->path == NULL, FALSE);

        create_error = NULL;
        file->priv->fp = _create_xauth_file_for_user (file->priv->username,
                                                      &file->priv->path,
                                                      &create_error);

        if (file->priv->fp == NULL) {
                g_propagate_error (error, create_error);
                return FALSE;
        }

        return TRUE;
}

static void
_get_auth_info_for_display (GdmDisplayAccessFile *file,
                            GdmDisplay           *display,
                            unsigned short       *family,
                            unsigned short       *address_length,
                            char                **address,
                            unsigned short       *number_length,
                            char                **number,
                            unsigned short       *name_length,
                            char                **name)
{
        int display_number;
        gboolean is_local;

        gdm_display_is_local (display, &is_local, NULL);

        if (is_local) {
                char localhost[HOST_NAME_MAX + 1] = "";
                *family = FamilyLocal;
                if (gethostname (localhost, HOST_NAME_MAX) == 0) {
                        *address = g_strdup (localhost);
                } else {
                        *address = g_strdup ("localhost");
                }
	       *address_length = strlen (*address);
        } else {
                char *remote_hostname;
                int s;

                gdm_display_get_remote_hostname (display, &remote_hostname, NULL);
                g_debug ("GdmDisplayAccessFile: looking up hostname: %s", remote_hostname);
#ifdef ENABLE_IPV6
                *address = g_malloc0 (sizeof (struct in6_addr));
#else
                *address = g_malloc0 (sizeof (struct in_addr));
#endif
                if ((s = inet_pton(AF_INET, remote_hostname, *address)) > 0) {
                    /* Success, ipv4 address */
                    g_debug ("GdmDisplayAccessFile: %s recognized as IPv4", remote_hostname);
                    g_free (remote_hostname);
                    *family = FamilyInternet;
                    *address_length = (sizeof (struct in_addr));
                }
#ifdef ENABLE_IPV6
                else if ((s = inet_pton(AF_INET6, remote_hostname, *address)) > 0) {
                    /* Success, ipv6 address */
                    g_debug ("GdmDisplayAccessFile: %s recognized as IPv6", remote_hostname);
                    g_free (remote_hostname);
                    *family = FamilyInternet6;
                    *address_length = (sizeof (struct in6_addr));
                }
#endif
                else { /* Not an address */
                    g_debug ("GdmDisplayAccessFile: %s not recognized as an address", remote_hostname);
                    g_free (*address);
                    *family = FamilyWild;
                    *address = remote_hostname;
                    *address_length = strlen (*address);
                } 
        }

        gdm_display_get_x11_display_number (display, &display_number, NULL);
        *number = g_strdup_printf ("%d", display_number);
        *number_length = strlen (*number);

        *name = g_strdup ("MIT-MAGIC-COOKIE-1");
        *name_length = strlen (*name);
}

gboolean
gdm_display_access_file_add_display (GdmDisplayAccessFile  *file,
                                     GdmDisplay            *display,
                                     char                 **cookie,
                                     gsize                 *cookie_size,
                                     GError               **error)
{
        GError  *add_error;
        gboolean display_added;

        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->path != NULL, FALSE);
        g_return_val_if_fail (cookie != NULL, FALSE);

        add_error = NULL;
        *cookie = gdm_generate_random_bytes (GDM_DISPLAY_ACCESS_COOKIE_SIZE,
                                             &add_error);

        if (*cookie == NULL) {
                g_propagate_error (error, add_error);
                return FALSE;
        }

        *cookie_size = GDM_DISPLAY_ACCESS_COOKIE_SIZE;

        display_added = gdm_display_access_file_add_display_with_cookie (file, display,
                                                                         *cookie,
                                                                         *cookie_size,
                                                                         &add_error);
        if (!display_added) {
                g_free (*cookie);
                *cookie = NULL;
                g_propagate_error (error, add_error);
                return FALSE;
        }

        return TRUE;
}

/* 
 * write_compat_entries_for_localhost:
 *
 * https://bugzilla.redhat.com/show_bug.cgi?id=867981
 * xcb has some questionable code that makes it automatically
 * treat display connections coming to localhost as FamilyLocal.
 * This means that our xauth cookie won't work, since we use
 * FamilyInternet for XDMCP, even if it's localhost.
 *
 * This function adds two additional entries to the Xauthority
 * file.  One for FamilyLocal, and one for FamilyWild to keep
 * xcb chugging. Technically, either entry should work, but
 * we add both for maximum compatibility.
 */
static gboolean
write_compat_entries_for_localhost (GdmDisplayAccessFile  *file,
                                    Xauth                 *auth_entry)
{
        Xauth     compat_auth_entry;
        gboolean  needs_compat_entries = FALSE;
        char      localhostname[1024] = ""; 
        gboolean  wrote_entry = FALSE;

        switch (auth_entry->family) {

#ifdef ENABLE_IPV6
                case FamilyInternet6:
                {
			struct in6_addr *address;

                        address = (struct in6_addr *) auth_entry->address;

                        if (IN6_IS_ADDR_LOOPBACK (address)) {
                                needs_compat_entries = TRUE;
                        }
                }
                break;
#endif
                case FamilyInternet:
                {
                	struct in_addr *address;

                        address = (struct in_addr *) (auth_entry->address);

                        if (address->s_addr == htonl (INADDR_LOOPBACK)) { 
                                needs_compat_entries = TRUE;
                        }
                }
                break;

                default:
                break;
        }

        if (!needs_compat_entries) {
                return FALSE;
        }

        if (gethostname (localhostname, 1023) < 0) {
                return FALSE;
        }

        compat_auth_entry = *auth_entry;
        compat_auth_entry.address = localhostname;
        compat_auth_entry.address_length = strlen (localhostname);

        compat_auth_entry.family = FamilyLocal;
        if (XauWriteAuth (file->priv->fp, &compat_auth_entry)) {
                if (fflush (file->priv->fp) != EOF) {
                        wrote_entry = TRUE;
                }
        }

        compat_auth_entry.family = FamilyWild;
        if (XauWriteAuth (file->priv->fp, &compat_auth_entry)) {
                if (fflush (file->priv->fp) != EOF) {
                        wrote_entry = TRUE;
                }
        }

        return wrote_entry;
}

gboolean
gdm_display_access_file_add_display_with_cookie (GdmDisplayAccessFile  *file,
                                                 GdmDisplay            *display,
                                                 const char            *cookie,
                                                 gsize                  cookie_size,
                                                 GError               **error)
{
        Xauth auth_entry;
        gboolean display_added;

        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->path != NULL, FALSE);
        g_return_val_if_fail (cookie != NULL, FALSE);

        _get_auth_info_for_display (file, display,
                                    &auth_entry.family,
                                    &auth_entry.address_length,
                                    &auth_entry.address,
                                    &auth_entry.number_length,
                                    &auth_entry.number,
                                    &auth_entry.name_length,
                                    &auth_entry.name);

        auth_entry.data = (char *) cookie;
        auth_entry.data_length = cookie_size;

        /* FIXME: We should lock the file in case the X server is
         * trying to use it, too.
         */
        if (!XauWriteAuth (file->priv->fp, &auth_entry)
            || fflush (file->priv->fp) == EOF) {
                g_set_error (error,
                        G_FILE_ERROR,
                        g_file_error_from_errno (errno),
                        "%s", g_strerror (errno));
                display_added = FALSE;
        } else {
                display_added = TRUE;
        }

	if (write_compat_entries_for_localhost (file, &auth_entry)) {
                display_added = TRUE;
        }

        g_free (auth_entry.address);
        g_free (auth_entry.number);
        g_free (auth_entry.name);

        return display_added;
}

gboolean
gdm_display_access_file_remove_display (GdmDisplayAccessFile  *file,
                                        GdmDisplay            *display,
                                        GError               **error)
{
        Xauth           *auth_entry;
        unsigned short  family;
        unsigned short  address_length;
        char           *address;
        unsigned short  number_length;
        char           *number;
        unsigned short  name_length;
        char           *name;


        g_return_val_if_fail (file != NULL, FALSE);
        g_return_val_if_fail (file->priv->path != NULL, FALSE);

        _get_auth_info_for_display (file, display,
                                    &family,
                                    &address_length,
                                    &address,
                                    &number_length,
                                    &number,
                                    &name_length,
                                    &name);

        auth_entry = XauGetAuthByAddr (family,
                                       address_length,
                                       address,
                                       number_length,
                                       number,
                                       name_length,
                                       name);
        g_free (address);
        g_free (number);
        g_free (name);

        if (auth_entry == NULL) {
                g_set_error (error,
                             GDM_DISPLAY_ACCESS_FILE_ERROR,
                             GDM_DISPLAY_ACCESS_FILE_ERROR_FINDING_AUTH_ENTRY,
                             "could not find authorization entry");
                return FALSE;
        }

        XauDisposeAuth (auth_entry);

        if (fflush (file->priv->fp) == EOF) {
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             "%s", g_strerror (errno));
                return FALSE;
        }

        return TRUE;
}

void
gdm_display_access_file_close (GdmDisplayAccessFile  *file)
{
        char *auth_dir;

        g_return_if_fail (file != NULL);
        g_return_if_fail (file->priv->fp != NULL);
        g_return_if_fail (file->priv->path != NULL);

        errno = 0;
        if (g_unlink (file->priv->path) != 0) {
                g_warning ("GdmDisplayAccessFile: Unable to remove X11 authority database '%s': %s",
                           file->priv->path,
                           g_strerror (errno));
        }

        /* still try to remove dir even if file remove failed,
           may have already been removed by someone else */
        /* we own the parent directory too */
        auth_dir = g_path_get_dirname (file->priv->path);
        if (auth_dir != NULL) {
                errno = 0;
                if (g_rmdir (auth_dir) != 0) {
                        g_warning ("GdmDisplayAccessFile: Unable to remove X11 authority directory '%s': %s",
                                   auth_dir,
                                   g_strerror (errno));
                }
                g_free (auth_dir);
        }

        g_free (file->priv->path);
        file->priv->path = NULL;
        g_object_notify (G_OBJECT (file), "path");

        fclose (file->priv->fp);
        file->priv->fp = NULL;
}

char *
gdm_display_access_file_get_path (GdmDisplayAccessFile *access_file)
{
        return g_strdup (access_file->priv->path);
}
