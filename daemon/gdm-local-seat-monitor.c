/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Red Hat, Inc.
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

#include "gdm-local-seat-monitor.h"

#define GDM_LOCAL_SEAT_MONITOR_FROM_SOURCE(s) (((GdmLocalSeatMonitorSource *) source)->seat_monitor)

typedef struct
{
        GSource parent_instance;
        GdmLocalSeatMonitor *seat_monitor;
} GdmLocalSeatMonitorSource;

struct _GdmLocalSeatMonitor
{
        GObject parent_instance;

        GSource *source;
        GPollFD poll_fd;

        sd_login_monitor *login_monitor;
};

G_DEFINE_TYPE (GdmLocalSeatMonitor, gdm_local_seat_monitor, G_TYPE_OBJECT);

static gboolean
seat_source_prepare (GSource *source,
                     gint    *timeout)
{
        *timeout = -1;
        return FALSE;
}

static gboolean
seat_source_check (GSource *source)
{
        GdmLocalSeatMonitor *self = GDM_LOCAL_SEAT_MONITOR_FROM_SOURCE (source);

        return self->poll_fd.revents != 0;
}

static gboolean
seat_source_dispatch (GSource     *source,
                      GSourceFunc  callback,
                      gpointer     user_data)

{
        GdmLocalSeatMonitor *self = GDM_LOCAL_SEAT_MONITOR_FROM_SOURCE (source);
        gboolean ret;

        g_warn_if_fail (callback != NULL);

        ret = (* callback) (user_data);

        sd_login_monitor_flush (self->login_monitor);

        return ret;
}

static void
seat_source_finalize (GSource *source)
{
        GdmLocalSeatMonitor *self = GDM_LOCAL_SEAT_MONITOR_FROM_SOURCE (source);

        sd_login_monitor_unref (self->login_monitor);
}

static GSourceFuncs seat_source_funcs = {
        seat_source_prepare,
        seat_source_check,
        seat_source_dispatch,
        seat_source_finalize
};

static void
gdm_local_seat_monitor_dispose (GObject *object)
{
        G_OBJECT_CLASS (gdm_local_seat_monitor_parent_class)->dispose (object);
}

static void
gdm_local_seat_monitor_class_init (GdmLocalSeatMonitorClass *monitor_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (monitor_class);

        object_class->dispose = gdm_local_seat_monitor_dispose;
}

static gboolean
on_login_monitor_activity (GdmLocalSeatMonitor *self)
{
        return G_SOURCE_CONTINUE;
}

static void
gdm_local_seat_monitor_init (GdmLocalSeatMonitor *self)
{
        GdmLocalSeatMonitorSource *source;

        int ret;

        ret = sd_login_monitor_new ("seat", &self->login_monitor);

        if (ret < 0) {
                g_warning ("Error watching logind for VT changes on local seat: %s",
                           g_strerror (-ret));
                return;
        }

        source = (GdmLocalSeatMonitorSource *) g_source_new (&seat_source_funcs, sizeof (*source));
        source->seat_monitor = self;

        self->source = (GSource *) source;

        self->poll_fd.fd = sd_login_monitor_get_fd (self->login_monitor);
        self->poll_fd.events = G_IO_IN;
        g_source_add_poll (self->source, &self->poll_fd);

        g_source_set_callback (self->source, (GSourceFunc) on_login_monitor_activity, self, NULL);
        g_source_attach (self->source, NULL);
}
