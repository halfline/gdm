/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display.h"
#include "gdm-xdmcp-chooser-display.h"
#include "gdm-xdmcp-chooser-slave.h"

#include "gdm-common.h"
#include "gdm-address.h"

static void     gdm_xdmcp_chooser_display_class_init    (GdmXdmcpChooserDisplayClass *klass);
static void     gdm_xdmcp_chooser_display_init          (GdmXdmcpChooserDisplay      *xdmcp_chooser_display);

G_DEFINE_TYPE (GdmXdmcpChooserDisplay, gdm_xdmcp_chooser_display, GDM_TYPE_XDMCP_DISPLAY)

static void
gdm_xdmcp_chooser_display_class_init (GdmXdmcpChooserDisplayClass *klass)
{
}

static void
gdm_xdmcp_chooser_display_init (GdmXdmcpChooserDisplay *xdmcp_chooser_display)
{
}

GdmDisplay *
gdm_xdmcp_chooser_display_new (const char              *hostname,
                               int                      number,
                               GdmAddress              *address,
                               gint32                   session_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf ("%s:%d", hostname, number);
        object = g_object_new (GDM_TYPE_XDMCP_CHOOSER_DISPLAY,
                               "slave-type", GDM_TYPE_XDMCP_CHOOSER_SLAVE,
                               "remote-hostname", hostname,
                               "x11-display-number", number,
                               "x11-display-name", x11_display,
                               "is-local", FALSE,
                               "remote-address", address,
                               "session-number", session_number,
                               NULL);
        g_free (x11_display);

        return GDM_DISPLAY (object);
}
