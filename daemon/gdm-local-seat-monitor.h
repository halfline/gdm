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
#ifndef __GDM_LOCAL_SEAT_MONITOR_H__
#define __GDM_LOCAL_SEAT_MONITOR_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SEAT_MONITOR gdm_local_seat_monitor_get_type ()

G_DECLARE_FINAL_TYPE (GdmLocalSeatMonitor, gdm_local_seat_monitor, GDM_LOCAL_SEAT_MONITOR, FILE, GObject)

GdmLocalSeatMonitor *gdm_local_seat_monitor_new (void);

G_END_DECLS

#endif /* __GDM_LOCAL_SEAT_MONITOR_H__ */
