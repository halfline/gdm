/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "gdm-log.h"
#include "gdm-common-unknown-origin.h"

#define MAX_LOGS 5

static gboolean initialized = FALSE;
static gboolean enabled = FALSE;
static int log_levels = (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);
static char *log_path = NULL;
static int log_fd = -1;

static void
rotate_logs (const char *path,
             guint       n_copies)
{
        int i;

        for (i = n_copies - 1; i > 0; i--) {
                char *name_n;
                char *name_n1;

                name_n = g_strdup_printf ("%s.%d", path, i);
                if (i > 1) {
                        name_n1 = g_strdup_printf ("%s.%d", path, i - 1);
                } else {
                        name_n1 = g_strdup (path);
                }

                VE_IGNORE_EINTR (g_unlink (name_n));
                VE_IGNORE_EINTR (g_rename (name_n1, name_n));

                g_free (name_n1);
                g_free (name_n);
        }

        VE_IGNORE_EINTR (g_unlink (path));
}

static void
log_level_to_prefix (GLogLevelFlags   log_level,
                     const char     **prefixp)
{
        const char *prefix;

        /* Process the message prefix */
        switch (log_level & G_LOG_LEVEL_MASK) {
        case G_LOG_FLAG_FATAL:
                prefix = "FATAL";
                break;
        case G_LOG_LEVEL_ERROR:
                prefix = "ERROR";
                break;
        case G_LOG_LEVEL_CRITICAL:
                prefix = "CRITICAL";
                break;
        case G_LOG_LEVEL_WARNING:
                prefix = "WARNING";
                break;
        case G_LOG_LEVEL_MESSAGE:
                prefix = "MESSAGE";
                break;
        case G_LOG_LEVEL_INFO:
                prefix = "INFO";
                break;
        case G_LOG_LEVEL_DEBUG:
                if (log_level & G_LOG_LEVEL_DEBUG) {
                        prefix = "DEBUG(+)";
                } else {
                        prefix = "DEBUG";
                }
                break;
        default:
                prefix = "UNKNOWN";
                break;
        }

        if (prefixp != NULL) {
                *prefixp = prefix;
        }
}

void
gdm_log_default_handler (const gchar   *log_domain,
                         GLogLevelFlags log_level,
                         const gchar   *message,
                         gpointer       unused_data)
{
        GString     *gstring;
        const char  *level_prefix;
        char        *string;
        gboolean     do_log;
        gboolean     is_fatal;

        is_fatal = (log_level & G_LOG_FLAG_FATAL) != 0;

        do_log = (log_level & log_levels);
        if (! do_log && !enabled) {
                return;
        }

        if (! initialized) {
                gdm_log_init ();
        }

        if (log_fd < 0) {
                if (log_path != NULL) {
                        log_fd = open (log_path, O_CREAT|O_APPEND|O_TRUNC|O_WRONLY|O_EXCL, 0644);
                } else {
                        log_fd = STDERR_FILENO;
                }
        }

        log_level_to_prefix (log_level,
                             &level_prefix);

        gstring = g_string_new (NULL);

        if (log_domain != NULL) {
                g_string_append (gstring, log_domain);
                g_string_append_c (gstring, '-');
        }
        g_string_append (gstring, level_prefix);

        g_string_append (gstring, ": ");
        if (message == NULL) {
                g_string_append (gstring, "(NULL) message");
        } else {
                g_string_append (gstring, message);
        }
        if (is_fatal) {
                g_string_append (gstring, "\naborting...\n");
        } else {
                g_string_append (gstring, "\n");
        }

        write (log_fd, gstring->str,  gstring->len);
        fsync (log_fd);

        g_string_free (gstring, TRUE);
}

void
gdm_log_toggle_debug (void)
{
        if (enabled) {
                g_debug ("Debugging disabled");
                enabled = FALSE;
        } else {
                enabled = TRUE;
                g_debug ("Debugging enabled");
        }
}

void
gdm_log_set_debug (gboolean debug)
{
	enabled = debug;
}

void
gdm_log_init (void)
{
        const char *prg_name;

        g_log_set_default_handler (gdm_log_default_handler, NULL);

        prg_name = g_get_prgname ();

        if (g_strcmp0 (prg_name, "gdm-binary") == 0) {
               log_path = g_build_filename (LOGDIR, prg_name, NULL);
	        rotate_logs (log_path, MAX_LOGS);
        }

        initialized = TRUE;
}

void
gdm_log_shutdown (void)
{
        initialized = FALSE;

        if (log_path != NULL) {
                g_free (log_path);
                log_path = NULL;
        }
        if (log_fd >= 0) {
                close (log_fd);
                log_fd = -1;
        }
}

