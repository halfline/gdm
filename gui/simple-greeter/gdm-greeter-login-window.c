/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008, 2009 Red Hat, Inc.
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
 * Written by: William Jon McCann <mccann@jhu.edu>
 *             Ray Strode <rstrode@redhat.com>
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
#include <sys/time.h>
#include <errno.h>
#include <pwd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-marshal.h"

#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"
#include "gdm-profile.h"

#include "gdm-greeter-client.h"
#include "gdm-greeter-login-window.h"
#include "gdm-user-chooser-widget.h"
#include "gdm-task-list.h"

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

#define UI_XML_FILE       "gdm-greeter-login-window.ui"

#define KEY_GREETER_DIR             "/apps/gdm/simple-greeter"
#define KEY_BANNER_MESSAGE_ENABLED  KEY_GREETER_DIR "/banner_message_enable"
#define KEY_BANNER_MESSAGE_TEXT     KEY_GREETER_DIR "/banner_message_text"
#define KEY_BANNER_MESSAGE_TEXT_NOCHOOSER     KEY_GREETER_DIR "/banner_message_text_nochooser"
#define KEY_LOGO                    KEY_GREETER_DIR "/logo_icon_name"
#define KEY_DISABLE_USER_LIST       "/apps/gdm/simple-greeter/disable_user_list"

#define GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_LOGIN_WINDOW, GdmGreeterLoginWindowPrivate))

enum {
        MODE_UNDEFINED = 0,
        MODE_TIMED_LOGIN,
        MODE_SELECTION,
        MODE_AUTHENTICATION,
};

enum {
        LOGIN_BUTTON_HIDDEN = 0,
        LOGIN_BUTTON_ANSWER_QUERY,
        LOGIN_BUTTON_TIMED_LOGIN
};

struct GdmGreeterLoginWindowPrivate
{
        GtkBuilder      *builder;
        GtkWidget       *user_chooser;
        GtkWidget       *conversation_list;
        GtkWidget       *auth_banner_label;
        GtkWidget       *auth_page_box;
        guint            display_is_local : 1;
        guint            is_interactive : 1;
        guint            user_chooser_loaded : 1;
        GConfClient     *client;
        GList           *tasks_to_enable;

        gboolean         banner_message_enabled;
        guint            gconf_cnxn;

        guint            last_mode;
        guint            dialog_mode;

        gboolean         user_list_disabled;
        guint            num_queries;

        gboolean         timed_login_already_enabled;
        gboolean         timed_login_enabled;
        guint            timed_login_delay;
        char            *timed_login_username;
        guint            timed_login_timeout_id;

        guint            login_button_handler_id;
        guint            start_session_handler_id;
};

enum {
        PROP_0,
        PROP_DISPLAY_IS_LOCAL,
        PROP_IS_INTERACTIVE,
};

enum {
        START_CONVERSATION,
        BEGIN_AUTO_LOGIN,
        BEGIN_VERIFICATION,
        BEGIN_VERIFICATION_FOR_USER,
        QUERY_ANSWER,
        START_SESSION,
        USER_SELECTED,
        DISCONNECTED,
        CANCELLED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_greeter_login_window_class_init   (GdmGreeterLoginWindowClass *klass);
static void     gdm_greeter_login_window_init         (GdmGreeterLoginWindow      *greeter_login_window);
static void     gdm_greeter_login_window_finalize     (GObject                    *object);

static void     restart_timed_login_timeout (GdmGreeterLoginWindow *login_window);
static void     on_user_unchosen            (GdmUserChooserWidget *user_chooser,
                                             GdmGreeterLoginWindow *login_window);

static void     switch_mode                 (GdmGreeterLoginWindow *login_window,
                                             int                    number);
static void     update_banner_message       (GdmGreeterLoginWindow *login_window);
static void     gdm_greeter_login_window_start_session_when_ready (GdmGreeterLoginWindow *login_window,
                                                                   const char            *service_name);

G_DEFINE_TYPE (GdmGreeterLoginWindow, gdm_greeter_login_window, GTK_TYPE_WINDOW)

static void
set_busy (GdmGreeterLoginWindow *login_window)
{
        GdkCursor *cursor;

        cursor = gdk_cursor_new (GDK_WATCH);
        gdk_window_set_cursor (GTK_WIDGET (login_window)->window, cursor);
        gdk_cursor_unref (cursor);
}

static void
set_ready (GdmGreeterLoginWindow *login_window)
{
        gdk_window_set_cursor (GTK_WIDGET (login_window)->window, NULL);
}

static void
set_sensitive (GdmGreeterLoginWindow *login_window,
               gboolean               sensitive)
{
        GtkWidget *box;

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "buttonbox"));
        gtk_widget_set_sensitive (box, sensitive);

        gtk_widget_set_sensitive (login_window->priv->user_chooser, sensitive);
}

static void
set_focus (GdmGreeterLoginWindow *login_window)
{
        GdmTask *task;

        gdk_window_focus (GTK_WIDGET (login_window)->window, GDK_CURRENT_TIME);

        task = gdm_task_list_get_active_task (GDM_TASK_LIST (login_window->priv->conversation_list));

        if (task != NULL && gdm_conversation_focus (GDM_CONVERSATION (task))) {
                char *name;
                name = gdm_task_get_name (task);
                g_debug ("GdmGreeterLoginWindow: focusing task %s", name);
                g_free (name);
        } else if (GTK_WIDGET_REALIZED (login_window->priv->user_chooser) && ! GTK_WIDGET_HAS_FOCUS (login_window->priv->user_chooser)) {
                gtk_widget_grab_focus (login_window->priv->user_chooser);
        }
        g_object_unref (task);
}

static gboolean
set_task_conversation_message (GdmTaskList *task_list,
                               GdmTask     *task,
                               const char  *message)
{

        gdm_conversation_set_message (GDM_CONVERSATION (task), message);
        return FALSE;
}

static void
set_message (GdmGreeterLoginWindow *login_window,
             const char            *text)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                    (GdmTaskListForeachFunc)
                                    set_task_conversation_message,
                                    (gpointer) text);
}

static void
on_user_interaction (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: user is interacting with session!\n");
        restart_timed_login_timeout (login_window);
}

static GdkFilterReturn
on_xevent (XEvent                *xevent,
           GdkEvent              *event,
           GdmGreeterLoginWindow *login_window)
{
        switch (xevent->xany.type) {
                case KeyPress:
                case KeyRelease:
                case ButtonPress:
                case ButtonRelease:
                        on_user_interaction (login_window);
                        break;
                case  PropertyNotify:
                        if (xevent->xproperty.atom == gdk_x11_get_xatom_by_name ("_NET_WM_USER_TIME")) {
                                on_user_interaction (login_window);
                        }
                        break;

                default:
                        break;
        }

        return GDK_FILTER_CONTINUE;
}

static void
stop_watching_for_user_interaction (GdmGreeterLoginWindow *login_window)
{
        gdk_window_remove_filter (NULL,
                                  (GdkFilterFunc) on_xevent,
                                  login_window);
}

static void
remove_timed_login_timeout (GdmGreeterLoginWindow *login_window)
{
        if (login_window->priv->timed_login_timeout_id > 0) {
                g_debug ("GdmGreeterLoginWindow: removing timed login timer");
                g_source_remove (login_window->priv->timed_login_timeout_id);
                login_window->priv->timed_login_timeout_id = 0;
        }

        stop_watching_for_user_interaction (login_window);
}

static void
_gdm_greeter_login_window_set_interactive (GdmGreeterLoginWindow *login_window,
                                           gboolean               is_interactive)
{
        if (login_window->priv->is_interactive != is_interactive) {
                login_window->priv->is_interactive = is_interactive;
                g_object_notify (G_OBJECT (login_window), "is-interactive");
        }
}

static gboolean
timed_login_timer (GdmGreeterLoginWindow *login_window)
{
        set_sensitive (login_window, FALSE);
        set_message (login_window, _("Automatically logging in..."));

        g_debug ("GdmGreeterLoginWindow: timer expired");
        _gdm_greeter_login_window_set_interactive (login_window, TRUE);
        login_window->priv->timed_login_timeout_id = 0;

        return FALSE;
}

static void
watch_for_user_interaction (GdmGreeterLoginWindow *login_window)
{
        gdk_window_add_filter (NULL,
                               (GdkFilterFunc) on_xevent,
                               login_window);
}

static void
restart_timed_login_timeout (GdmGreeterLoginWindow *login_window)
{
        remove_timed_login_timeout (login_window);

        if (login_window->priv->timed_login_enabled) {
                g_debug ("GdmGreeterLoginWindow: adding timed login timer");
                watch_for_user_interaction (login_window);
                login_window->priv->timed_login_timeout_id = g_timeout_add_seconds (login_window->priv->timed_login_delay,
                                                                                    (GSourceFunc)timed_login_timer,
                                                                                    login_window);

                gdm_chooser_widget_set_item_timer (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                   GDM_USER_CHOOSER_USER_AUTO,
                                                   login_window->priv->timed_login_delay * 1000);
        }
}

static void
show_widget (GdmGreeterLoginWindow *login_window,
             const char            *name,
             gboolean               visible)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, name));
        if (widget != NULL) {
                if (visible) {
                        gtk_widget_show (widget);
                } else {
                        gtk_widget_hide (widget);
                }
        }
}

static void
hide_task_actions (GdmTask *task)
{
        GtkActionGroup *actions;

        actions = gdm_conversation_get_actions (GDM_CONVERSATION (task));

        if (actions != NULL) {
                gtk_action_group_set_visible (actions, FALSE);
                gtk_action_group_set_sensitive (actions, FALSE);
                g_object_unref (actions);
        }
}

static void
grab_default_button_for_task (GdmTask *task)
{
        GtkActionGroup *actions;
        GtkAction *action;
        GSList    *proxies, *node;

        actions = gdm_conversation_get_actions (GDM_CONVERSATION (task));

        if (actions == NULL) {
                return;
        }

        action = gtk_action_group_get_action (actions, GDM_CONVERSATION_DEFAULT_ACTION);
        g_object_unref (actions);

        if (action == NULL) {
                return;
        }

        proxies = gtk_action_get_proxies (action);
        for (node = proxies; node != NULL; node = node->next) {
                GtkWidget *widget;

                widget = GTK_WIDGET (node->data);

                if (GTK_WIDGET_CAN_DEFAULT (widget) &&
                    GTK_WIDGET_VISIBLE (widget)) {
                        gtk_widget_grab_default (widget);
                        break;
                }
        }

}

static void
show_task_actions (GdmTask *task)
{
        GtkActionGroup *actions;

        actions = gdm_conversation_get_actions (GDM_CONVERSATION (task));

        if (actions != NULL) {
                gtk_action_group_set_sensitive (actions, TRUE);
                gtk_action_group_set_visible (actions, TRUE);
                g_object_unref (actions);
        }
}

static void
on_login_button_clicked_timed_login (GtkButton             *button,
                                     GdmGreeterLoginWindow *login_window)
{
        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        _gdm_greeter_login_window_set_interactive (login_window, TRUE);
}

static void
set_log_in_button_mode (GdmGreeterLoginWindow *login_window,
                        int                    mode)
{
        GtkWidget *button;
        GdmTask   *task;

        button = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "log-in-button"));
        gtk_widget_grab_default (button);

        /* disconnect any signals */
        if (login_window->priv->login_button_handler_id > 0) {
                g_signal_handler_disconnect (button, login_window->priv->login_button_handler_id);
                login_window->priv->login_button_handler_id = 0;
       }

        switch (mode) {
        case LOGIN_BUTTON_HIDDEN:
                task = gdm_task_list_get_active_task (GDM_TASK_LIST (login_window->priv->conversation_list));
                if (task != NULL) {
                        hide_task_actions (task);
                        g_object_unref (task);
                }

                gtk_widget_hide (button);
                break;
        case LOGIN_BUTTON_ANSWER_QUERY:
                task = gdm_task_list_get_active_task (GDM_TASK_LIST (login_window->priv->conversation_list));
                if (task != NULL) {
                        show_task_actions (task);
                        grab_default_button_for_task (task);
                        g_object_unref (task);
                }

                gtk_widget_hide (button);
                break;
        case LOGIN_BUTTON_TIMED_LOGIN:
                login_window->priv->login_button_handler_id = g_signal_connect (button, "clicked", G_CALLBACK (on_login_button_clicked_timed_login), login_window);
                gtk_widget_show (button);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static gboolean
user_chooser_has_no_user (GdmGreeterLoginWindow *login_window)
{
        guint num_items;

        num_items = gdm_chooser_widget_get_number_of_items (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));
        g_debug ("GdmGreeterLoginWindow: loaded=%d num_items=%d",
                 login_window->priv->user_chooser_loaded,
                 num_items);
        return (login_window->priv->user_chooser_loaded && num_items == 0);
}

static void
maybe_show_cancel_button (GdmGreeterLoginWindow *login_window)
{
        gboolean show;

        show = FALSE;

        /* only show the cancel button if there is something to go
           back to */

        switch (login_window->priv->dialog_mode) {
        case MODE_SELECTION:
                /* should never have anything to return to from here */
                show = FALSE;
                break;
        case MODE_TIMED_LOGIN:
                /* should always have something to return to from here */
                show = TRUE;
                break;
        case MODE_AUTHENTICATION:
                if (login_window->priv->num_queries > 1) {
                        /* if we are inside a pam conversation past
                           the first step */
                        show = TRUE;
                } else {
                        if (login_window->priv->user_list_disabled || user_chooser_has_no_user (login_window)) {
                                show = FALSE;
                        } else {
                                show = TRUE;
                        }
                }
                break;
        default:
                g_assert_not_reached ();
        }

        show_widget (login_window, "cancel-button", show);
}

static void
update_conversation_list_visibility (GdmGreeterLoginWindow *login_window)
{
        int number_of_tasks;

        if (login_window->priv->dialog_mode != MODE_AUTHENTICATION) {
                gtk_widget_hide (login_window->priv->conversation_list);
                return;
        }

        number_of_tasks = gdm_task_list_get_number_of_visible_tasks (GDM_TASK_LIST (login_window->priv->conversation_list));
        if (number_of_tasks > 1) {
                gtk_widget_show (login_window->priv->conversation_list);
        } else {
                gtk_widget_hide (login_window->priv->conversation_list);
        }
}

static void
switch_mode (GdmGreeterLoginWindow *login_window,
             int                    number)
{
        const char *default_name;
        GtkWidget  *box;

        /* Should never switch to MODE_UNDEFINED */
        g_assert (number != MODE_UNDEFINED);

        /* we want to run this even if we're supposed to
           be in the mode already so that we reset everything
           to a known state */
        if (login_window->priv->dialog_mode != number) {
                login_window->priv->last_mode = login_window->priv->dialog_mode;
                login_window->priv->dialog_mode = number;
        }

        default_name = NULL;

        switch (number) {
        case MODE_SELECTION:
                set_log_in_button_mode (login_window, LOGIN_BUTTON_HIDDEN);
                break;
        case MODE_TIMED_LOGIN:
                set_log_in_button_mode (login_window, LOGIN_BUTTON_TIMED_LOGIN);
                break;
        case MODE_AUTHENTICATION:
                set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);
                break;
        default:
                g_assert_not_reached ();
        }

        show_widget (login_window, "auth-input-box", FALSE);
        update_conversation_list_visibility (login_window);
        maybe_show_cancel_button (login_window);

        /*
         * The rest of this function sets up the user list, so just return if
         * the user list is disabled.
         */
        if (login_window->priv->user_list_disabled && number != MODE_TIMED_LOGIN) {
                return;
        }

        box = gtk_widget_get_parent (login_window->priv->user_chooser);
        if (GTK_IS_BOX (box)) {
                guint       padding;
                GtkPackType pack_type;

                gtk_box_query_child_packing (GTK_BOX (box),
                                             login_window->priv->user_chooser,
                                             NULL,
                                             NULL,
                                             &padding,
                                             &pack_type);
                gtk_box_set_child_packing (GTK_BOX (box),
                                           login_window->priv->user_chooser,
                                           number == MODE_SELECTION,
                                           number == MODE_SELECTION,
                                           padding,
                                           pack_type);
        }
}

static gboolean
task_has_service_name (GdmTaskList *task_list,
                       GdmTask     *task,
                       const char  *service_name)
{
        char *task_service_name;
        gboolean has_service_name;

        task_service_name = gdm_conversation_get_service_name (GDM_CONVERSATION (task));

        has_service_name = strcmp (service_name, task_service_name) == 0;
        g_free (task_service_name);

        return has_service_name;
}

static GdmTask *
find_task_with_service_name (GdmGreeterLoginWindow *login_window,
                             const char            *service_name)
{
        GdmTask *task;

        task = gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                           (GdmTaskListForeachFunc)
                                           task_has_service_name,
                                           (gpointer) service_name);

        return task;
}

static gboolean
reset_task (GdmTaskList           *task_list,
            GdmTask               *task,
            GdmGreeterLoginWindow *login_window)
{
        char *name;

        name = gdm_task_get_name (task);
        g_debug ("Resetting task '%s'", name);
        g_free (name);

        login_window->priv->tasks_to_enable = g_list_remove (login_window->priv->tasks_to_enable, task);

        gdm_conversation_reset (GDM_CONVERSATION (task));
        return FALSE;
}

static gboolean
task_is_disabled (GdmTaskList *task_list,
                  GdmTask     *task)
{
        return !gdm_task_is_enabled (task);
}

static gboolean
tasks_are_enabled (GdmGreeterLoginWindow *login_window)
{
        GdmTask *task;

        task = gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                           (GdmTaskListForeachFunc)
                                           task_is_disabled,
                                           NULL);

        return task == NULL;
}

static gboolean
can_jump_to_authenticate (GdmGreeterLoginWindow *login_window)
{
        gboolean res;

        if (!login_window->priv->user_chooser_loaded) {
                res = FALSE;
        } else if (!tasks_are_enabled (login_window)) {
                res = FALSE;
        } else if (login_window->priv->dialog_mode == MODE_AUTHENTICATION) {
                res = FALSE;
        } else if (login_window->priv->user_list_disabled) {
                res = (login_window->priv->timed_login_username == NULL);
        } else {
                res = user_chooser_has_no_user (login_window);
        }

        return res;
}

static gboolean
begin_task_verification (GdmTaskList           *task_list,
                         GdmTask               *task,
                         GdmGreeterLoginWindow *login_window)
{
        char *service_name;

        if (!gdm_task_is_visible (task)) {
                return FALSE;
        }

        service_name = gdm_conversation_get_service_name (GDM_CONVERSATION (task));
        if (service_name != NULL) {
                g_signal_emit (login_window, signals[BEGIN_VERIFICATION], 0, service_name);
                g_free (service_name);
        }

        return FALSE;
}

static void
begin_verification (GdmGreeterLoginWindow *login_window)
{
        gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                    (GdmTaskListForeachFunc)
                                    begin_task_verification,
                                    login_window);

        switch_mode (login_window, MODE_AUTHENTICATION);

        update_conversation_list_visibility (login_window);
}


static void
reset_dialog (GdmGreeterLoginWindow *login_window,
              guint                  dialog_mode)
{
        GtkWidget  *label;

        g_debug ("GdmGreeterLoginWindow: Resetting dialog to mode %u", dialog_mode);
        set_busy (login_window);
        set_sensitive (login_window, FALSE);

        login_window->priv->num_queries = 0;

        if (dialog_mode == MODE_SELECTION) {
                if (login_window->priv->timed_login_enabled) {
                        gdm_chooser_widget_set_item_timer (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                           GDM_USER_CHOOSER_USER_AUTO, 0);
                        remove_timed_login_timeout (login_window);
                        login_window->priv->timed_login_enabled = FALSE;
                }
                _gdm_greeter_login_window_set_interactive (login_window, FALSE);

                g_signal_handlers_block_by_func (G_OBJECT (login_window->priv->user_chooser),
                                                 G_CALLBACK (on_user_unchosen), login_window);
                gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), NULL);
                g_signal_handlers_unblock_by_func (G_OBJECT (login_window->priv->user_chooser),
                                                   G_CALLBACK (on_user_unchosen), login_window);

                if (login_window->priv->start_session_handler_id > 0) {
                        g_signal_handler_disconnect (login_window, login_window->priv->start_session_handler_id);
                        login_window->priv->start_session_handler_id = 0;
                }

                set_message (login_window, "");
        }

        gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                    (GdmTaskListForeachFunc)
                                    reset_task,
                                    login_window);

        label = GTK_WIDGET (gtk_builder_get_object (GDM_GREETER_LOGIN_WINDOW (login_window)->priv->builder, "auth-prompt-label"));
        gtk_label_set_text (GTK_LABEL (label), "");

        if (can_jump_to_authenticate (login_window)) {
                /* If we don't have a user list jump straight to authenticate */
                g_debug ("GdmGreeterLoginWindow: jumping straight to authenticate");

                g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                               0, GDM_USER_CHOOSER_USER_OTHER);
                begin_verification (login_window);
        } else {
                switch_mode (login_window, dialog_mode);
        }

        gtk_widget_set_sensitive (login_window->priv->conversation_list, TRUE);
        set_sensitive (login_window, TRUE);
        set_ready (login_window);
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));
        update_banner_message (login_window);

        if (gdm_chooser_widget_get_number_of_items (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser)) >= 1) {
                gdm_chooser_widget_propagate_pending_key_events (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));
        }
}

static void
restart_conversations (GdmGreeterLoginWindow *login_window)
{
        set_busy (login_window);
        set_sensitive (login_window, FALSE);
        g_signal_emit (login_window, signals[CANCELLED], 0);
}

static void
do_cancel (GdmGreeterLoginWindow *login_window)
{
        /* need to wait for response from backend */
        set_message (login_window, _("Cancelling..."));
        restart_conversations (login_window);
}

gboolean
gdm_greeter_login_window_ready (GdmGreeterLoginWindow *login_window,
                                const char            *service_name)
{
        GdmTask *task;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        task = find_task_with_service_name (login_window, service_name);

        if (task != NULL) {
                if (gdm_chooser_widget_is_loaded (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser))) {
                        gdm_conversation_set_ready (GDM_CONVERSATION (task));
                } else {
                        login_window->priv->tasks_to_enable = g_list_prepend (login_window->priv->tasks_to_enable, task);
                }
                g_object_unref (task);
        }

        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        /* If the user list is disabled, then start the PAM conversation */
        if (can_jump_to_authenticate (login_window)) {
                g_debug ("Starting PAM conversation since user list disabled or no local users");
                g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                               0, GDM_USER_CHOOSER_USER_OTHER);
                begin_verification (login_window);
        }

        return TRUE;
}

gboolean
gdm_greeter_login_window_conversation_stopped (GdmGreeterLoginWindow *login_window,
                                               const char            *service_name)
{
        GdmTask *task;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        g_debug ("GdmGreeterLoginWindow: conversation '%s' has stopped", service_name);

        /* If the password conversation failed, then start over
         *
         * FIXME: we need to get this policy out of the source code
         */
        if (strcmp (service_name, "gdm-password") == 0) {
                g_debug ("GdmGreeterLoginWindow: main conversation failed, starting over");
                restart_conversations (login_window);
                return TRUE;
        }

        task = find_task_with_service_name (login_window, service_name);

        if (task != NULL) {
                gdm_conversation_reset (GDM_CONVERSATION (task));
                g_object_unref (task);
        }

        /* If every conversation has failed, then just start over.
         */
        task = gdm_task_list_get_active_task (GDM_TASK_LIST (login_window->priv->conversation_list));

        if (!gdm_task_is_enabled (task)) {
                g_debug ("GdmGreeterLoginWindow: No conversations left, starting over");
                restart_conversations (login_window);
        }
        g_object_unref (task);

        update_conversation_list_visibility (login_window);

        return TRUE;
}

static gboolean
restart_task_conversation (GdmTaskList           *task_list,
                           GdmTask               *task,
                           GdmGreeterLoginWindow *login_window)
{
        char *service_name;

        service_name = gdm_conversation_get_service_name (GDM_CONVERSATION (task));
        if (service_name != NULL) {
                char *name;

                name = gdm_task_get_name (task);
                g_debug ("GdmGreeterLoginWindow: restarting '%s' conversation", name);
                g_free (name);

                g_signal_emit (login_window, signals[START_CONVERSATION], 0, service_name);
                g_free (service_name);
        }

        return FALSE;
}

gboolean
gdm_greeter_login_window_reset (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: window reset");

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);
        reset_dialog (GDM_GREETER_LOGIN_WINDOW (login_window), MODE_SELECTION);

        gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                    (GdmTaskListForeachFunc)
                                    restart_task_conversation,
                                    login_window);

        return TRUE;
}

gboolean
gdm_greeter_login_window_info (GdmGreeterLoginWindow *login_window,
                               const char            *service_name,
                               const char            *text)
{
        GdmTask *task;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);
        g_debug ("GdmGreeterLoginWindow: info: %s", text);

        maybe_show_cancel_button (login_window);
        task = find_task_with_service_name (login_window, service_name);

        if (task != NULL) {
                gdm_conversation_set_message (GDM_CONVERSATION (task),
                                              text);
                show_task_actions (task);
                g_object_unref (task);
        }

        return TRUE;
}

gboolean
gdm_greeter_login_window_problem (GdmGreeterLoginWindow *login_window,
                                  const char            *service_name,
                                  const char            *text)
{
        GdmTask *task;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);
        g_debug ("GdmGreeterLoginWindow: problem: %s", text);

        maybe_show_cancel_button (login_window);
        task = find_task_with_service_name (login_window, service_name);

        if (task != NULL) {
                gdm_conversation_set_message (GDM_CONVERSATION (task),
                                              text);
                show_task_actions (task);
                g_object_unref (task);
        }

        gdk_window_beep (GTK_WIDGET (login_window)->window);

        return TRUE;
}

static void
request_timed_login (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: requesting timed login");

        gtk_widget_show (login_window->priv->user_chooser);

        if (login_window->priv->dialog_mode != MODE_SELECTION) {
                reset_dialog (login_window, MODE_SELECTION);
        }

        if (!login_window->priv->timed_login_already_enabled) {
                gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                              GDM_USER_CHOOSER_USER_AUTO);
        }

        login_window->priv->timed_login_already_enabled = TRUE;
}

gboolean
gdm_greeter_login_window_service_unavailable (GdmGreeterLoginWindow *login_window,
                                              const char            *service_name)
{
        GdmTask *task;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);
        g_debug ("GdmGreeterLoginWindow: service unavailable: %s", service_name);

        task = find_task_with_service_name (login_window, service_name);

        if (task != NULL) {
                gdm_task_list_remove_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                           task);
                g_object_unref (task);
        }

        return TRUE;
}

void
gdm_greeter_login_window_request_timed_login (GdmGreeterLoginWindow *login_window,
                                              const char            *username,
                                              int                    delay)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        g_debug ("GdmGreeterLoginWindow: requested automatic login for user '%s' in %d seconds", username, delay);

        g_free (login_window->priv->timed_login_username);
        login_window->priv->timed_login_username = g_strdup (username);
        login_window->priv->timed_login_delay = delay;

        /* add the auto user right away so we won't trigger a mode
           switch to authenticate when the user list is disabled */
        gdm_user_chooser_widget_set_show_user_auto (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), TRUE);

        /* if the users aren't loaded then we'll handle it in when they are */
        if (login_window->priv->user_chooser_loaded) {
                g_debug ("Handling timed login request since users are already loaded.");
                request_timed_login (login_window);
        } else {
                g_debug ("Waiting to handle timed login request until users are loaded.");
        }
}

static void
on_ready_to_start_session (GdmGreeterLoginWindow *login_window,
                           GParamSpec            *param_spec,
                           char                  *service_name)
{
        if (!login_window->priv->is_interactive) {
                return;
        }

        gdm_greeter_login_window_start_session_when_ready (login_window, service_name);
        g_free (service_name);

        if (login_window->priv->start_session_handler_id > 0) {
                g_signal_handler_disconnect (login_window, login_window->priv->start_session_handler_id);
                login_window->priv->start_session_handler_id = 0;
        }
}

static void
gdm_greeter_login_window_start_session_when_ready (GdmGreeterLoginWindow *login_window,
                                                   const char            *service_name)
{
        if (login_window->priv->is_interactive) {
                g_debug ("GdmGreeterLoginWindow: starting session");
                g_signal_emit (login_window, signals[START_SESSION], 0, service_name);
        } else {
                g_debug ("GdmGreeterLoginWindow: not starting session since "
                         "user hasn't had an opportunity to pick language "
                         "and session yet.");

                /* Call back when we're ready to go
                 */
                login_window->priv->start_session_handler_id =
                    g_signal_connect (login_window, "notify::is-interactive",
                                      G_CALLBACK (on_ready_to_start_session),
                                      g_strdup (service_name));

                /* FIXME: If the user wasn't asked any questions by pam but
                 * pam still authorized them (passwd -d, or the questions got
                 * asked on an external device) then we need to let them log in.
                 * Right now we just log them in right away, but we really should
                 * set a timer up like timed login (but shorter, say ~5 seconds),
                 * so they can pick language/session.  Will need to refactor things
                 * a bit so we can share code with timed login.
                 */
                if (strcmp (service_name, "gdm-autologin") != 0) {

                        g_debug ("GdmGreeterLoginWindow: Okay, we'll start the session anyway,"
                                 "because the user isn't ever going to get an opportunity to"
                                 "interact with session");
                        _gdm_greeter_login_window_set_interactive (login_window, TRUE);
                }

        }
}

gboolean
gdm_greeter_login_window_info_query (GdmGreeterLoginWindow *login_window,
                                     const char            *service_name,
                                     const char            *text)
{
        GdmTask *task;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        login_window->priv->num_queries++;
        maybe_show_cancel_button (login_window);

        g_debug ("GdmGreeterLoginWindow: info query: %s", text);

        task = find_task_with_service_name (login_window, service_name);

        if (task != NULL) {
                gdm_conversation_ask_question (GDM_CONVERSATION (task),
                                               text);
                g_object_unref (task);
        }

        set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);
        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        gdm_chooser_widget_propagate_pending_key_events (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));

        return TRUE;
}

gboolean
gdm_greeter_login_window_secret_info_query (GdmGreeterLoginWindow *login_window,
                                            const char            *service_name,
                                            const char            *text)
{

        GdmTask *task;

        g_return_val_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window), FALSE);

        login_window->priv->num_queries++;
        maybe_show_cancel_button (login_window);

        task = find_task_with_service_name (login_window, service_name);

        if (task != NULL) {
                gdm_conversation_ask_secret (GDM_CONVERSATION (task),
                                             text);
                g_object_unref (task);
        }

        set_log_in_button_mode (login_window, LOGIN_BUTTON_ANSWER_QUERY);
        set_sensitive (GDM_GREETER_LOGIN_WINDOW (login_window), TRUE);
        set_ready (GDM_GREETER_LOGIN_WINDOW (login_window));
        set_focus (GDM_GREETER_LOGIN_WINDOW (login_window));

        gdm_chooser_widget_propagate_pending_key_events (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser));

        return TRUE;
}

void
gdm_greeter_login_window_user_authorized (GdmGreeterLoginWindow *login_window,
                                          const char            *service_name)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));

        g_debug ("GdmGreeterLoginWindow: user now authorized via service %s",
                  service_name);

        gdm_greeter_login_window_start_session_when_ready (login_window,
                                                           service_name);
}

static void
_gdm_greeter_login_window_set_display_is_local (GdmGreeterLoginWindow *login_window,
                                                gboolean               is)
{
        login_window->priv->display_is_local = is;
}

static void
gdm_greeter_login_window_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
        GdmGreeterLoginWindow *self;

        self = GDM_GREETER_LOGIN_WINDOW (object);

        switch (prop_id) {
        case PROP_DISPLAY_IS_LOCAL:
                _gdm_greeter_login_window_set_display_is_local (self, g_value_get_boolean (value));
                break;
        case PROP_IS_INTERACTIVE:
                _gdm_greeter_login_window_set_interactive (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_login_window_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
        GdmGreeterLoginWindow *self;

        self = GDM_GREETER_LOGIN_WINDOW (object);

        switch (prop_id) {
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->priv->display_is_local);
                break;
        case PROP_IS_INTERACTIVE:
                g_value_set_boolean (value, self->priv->is_interactive);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
cancel_button_clicked (GtkButton             *button,
                       GdmGreeterLoginWindow *login_window)
{
        do_cancel (login_window);
}

static void
on_user_chooser_visibility_changed (GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: Chooser visibility changed");
        update_banner_message (login_window);
}

static gboolean
begin_task_verification_for_selected_user (GdmTaskList           *task_list,
                                           GdmTask               *task,
                                           GdmGreeterLoginWindow *login_window)
{
        char *user_name;
        char *service_name;

        user_name = gdm_user_chooser_widget_get_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser));

        if (user_name == NULL) {
                return TRUE;
        }

        service_name = gdm_conversation_get_service_name (GDM_CONVERSATION (task));
        if (service_name != NULL) {
                g_signal_emit (login_window, signals[BEGIN_VERIFICATION_FOR_USER], 0, service_name, user_name);
                g_free (service_name);
        }

        g_free (user_name);
        return FALSE;
}

static void
enable_waiting_tasks (GdmGreeterLoginWindow *login_window)
{
        GList *node;

        node = login_window->priv->tasks_to_enable;
        while (node != NULL) {
                GdmTask *task;

                task = GDM_TASK (node->data);

                gdm_conversation_set_ready (GDM_CONVERSATION (task));

                node = node->next;
        }

        login_window->priv->tasks_to_enable = NULL;
}

static void
on_users_loaded (GdmUserChooserWidget  *user_chooser,
                 GdmGreeterLoginWindow *login_window)
{
        g_debug ("GdmGreeterLoginWindow: users loaded");
        login_window->priv->user_chooser_loaded = TRUE;

        update_banner_message (login_window);

        if (!login_window->priv->user_list_disabled) {
                gtk_widget_show (login_window->priv->user_chooser);
        }

        enable_waiting_tasks (login_window);

        if (login_window->priv->timed_login_username != NULL
            && !login_window->priv->timed_login_already_enabled) {
                request_timed_login (login_window);
        } else if (can_jump_to_authenticate (login_window)) {

                /* jump straight to authenticate */
                g_debug ("GdmGreeterLoginWindow: jumping straight to authenticate");
                g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                               0, GDM_USER_CHOOSER_USER_OTHER);
                begin_verification (login_window);
        }
}

static void
choose_user (GdmGreeterLoginWindow *login_window,
             const char            *user_name)
{
        g_assert (user_name != NULL);
        g_debug ("GdmGreeterLoginWindow: user chosen '%s'", user_name);

        g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                       0, user_name);


        gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                    (GdmTaskListForeachFunc)
                                    begin_task_verification_for_selected_user,
                                    login_window);

        switch_mode (login_window, MODE_AUTHENTICATION);
        update_conversation_list_visibility (login_window);
}

static void
begin_auto_login (GdmGreeterLoginWindow *login_window)
{
        g_signal_emit (login_window, signals[BEGIN_AUTO_LOGIN], 0,
                       login_window->priv->timed_login_username);

        login_window->priv->timed_login_enabled = TRUE;
        restart_timed_login_timeout (login_window);

        /* just wait for the user to select language and stuff */
        set_message (login_window, _("Select language and click Log In"));

        switch_mode (login_window, MODE_TIMED_LOGIN);

        show_widget (login_window, "conversation-list", FALSE);
        gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                    (GdmTaskListForeachFunc) reset_task,
                                    login_window);
}

static gboolean
reset_task_if_not_given (GdmTaskList *task_list,
                         GdmTask     *task,
                         GdmTask     *given_task)
{
        if (task == given_task) {
                return FALSE;
        }

        gdm_conversation_reset (GDM_CONVERSATION (task));
        return FALSE;
}

static void
reset_every_task_but_given_task (GdmGreeterLoginWindow *login_window,
                                 GdmTask               *task)
{
        gdm_task_list_foreach_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                    (GdmTaskListForeachFunc)
                                    reset_task_if_not_given,
                                    task);
}

static void
begin_single_service_verification (GdmGreeterLoginWindow *login_window,
                                   const char            *service_name)
{
        GdmTask *task;

        task = find_task_with_service_name (login_window, service_name);

        if (task == NULL) {
                g_debug ("GdmGreeterLoginWindow: %s has no task associated with it", service_name);
                return;
        }

        g_debug ("GdmGreeterLoginWindow: Beginning %s auth conversation", service_name);

        /* FIXME: we should probably give the plugin more say for
         * what happens here.
         */
        g_signal_emit (login_window, signals[BEGIN_VERIFICATION], 0, service_name);

        switch_mode (login_window, MODE_AUTHENTICATION);
        gdm_task_list_set_active_task (GDM_TASK_LIST (login_window->priv->conversation_list), task);

        reset_every_task_but_given_task (login_window, task);
        g_object_unref (task);

        show_widget (login_window, "conversation-list", FALSE);
}

static void
on_user_chooser_activated (GdmUserChooserWidget  *user_chooser,
                           GdmGreeterLoginWindow *login_window)
{
        char *user_name;
        char *item_id;

        user_name = gdm_user_chooser_widget_get_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser));

        if (user_name != NULL) {
                g_debug ("GdmGreeterLoginWindow: user chosen '%s'", user_name);
                choose_user (login_window, user_name);
                g_free (user_name);
                return;
        }

        item_id = gdm_chooser_widget_get_active_item (GDM_CHOOSER_WIDGET (user_chooser));
        g_debug ("GdmGreeterLoginWindow: item chosen '%s'", item_id);

        g_signal_emit (G_OBJECT (login_window), signals[USER_SELECTED],
                       0, item_id);

        if (strcmp (item_id, GDM_USER_CHOOSER_USER_OTHER) == 0) {
                g_debug ("GdmGreeterLoginWindow: Starting all auth conversations");
                g_free (item_id);

                begin_verification (login_window);
        } else if (strcmp (item_id, GDM_USER_CHOOSER_USER_GUEST) == 0) {
                /* FIXME: handle guest account stuff */
                g_free (item_id);
        } else if (strcmp (item_id, GDM_USER_CHOOSER_USER_AUTO) == 0) {
                g_debug ("GdmGreeterLoginWindow: Starting auto login");
                g_free (item_id);

                begin_auto_login (login_window);
        } else {
                g_debug ("GdmGreeterLoginWindow: Starting single auth conversation");
                begin_single_service_verification (login_window, item_id);
                g_free (item_id);
        }
}

static void
on_user_unchosen (GdmUserChooserWidget  *user_chooser,
                  GdmGreeterLoginWindow *login_window)
{
        do_cancel (login_window);
}

static void
rotate_computer_info (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *notebook;
        int        current_page;
        int        n_pages;

        /* switch page */
        notebook = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-notebook"));
        current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
        n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));

        if (current_page + 1 < n_pages) {
                gtk_notebook_next_page (GTK_NOTEBOOK (notebook));
        } else {
                gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
        }

}

static gboolean
on_computer_info_label_button_press (GtkWidget             *widget,
                                     GdkEventButton        *event,
                                     GdmGreeterLoginWindow *login_window)
{
        rotate_computer_info (login_window);
        return FALSE;
}

static char *
file_read_one_line (const char *filename)
{
        FILE *f;
        char *line;
        char buf[4096];

        line = NULL;

        f = fopen (filename, "r");
        if (f == NULL) {
                g_warning ("Unable to open file %s: %s", filename, g_strerror (errno));
                goto out;
        }

        if (fgets (buf, sizeof (buf), f) == NULL) {
                g_warning ("Unable to read from file %s", filename);
        }

        line = g_strdup (buf);
        g_strchomp (line);

 out:
        fclose (f);

        return line;
}

static const char *known_etc_info_files [] = {
        "redhat-release",
        "SuSE-release",
        "gentoo-release",
        "arch-release",
        "debian_version",
        "mandriva-release",
        "slackware-version",
        NULL
};


static char *
get_system_version (void)
{
        char *version;
        int i;

        version = NULL;

        for (i = 0; known_etc_info_files [i]; i++) {
                char *path1;
                char *path2;

                path1 = g_build_filename (SYSCONFDIR, known_etc_info_files [i], NULL);
                path2 = g_build_filename ("/etc", known_etc_info_files [i], NULL);
                if (g_access (path1, R_OK) == 0) {
                        version = file_read_one_line (path1);
                } else if (g_access (path2, R_OK) == 0) {
                        version = file_read_one_line (path2);
                }
                g_free (path2);
                g_free (path1);
                if (version != NULL) {
                        break;
                }
        }

        if (version == NULL) {
                char *output;
                output = NULL;
                if (g_spawn_command_line_sync ("uname -sr", &output, NULL, NULL, NULL)) {
                        version = g_strchomp (output);
                }
        }

        return version;
}

static void
create_computer_info (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *label;

        gdm_profile_start (NULL);

        label = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-name-label"));
        if (label != NULL) {
                char localhost[HOST_NAME_MAX + 1] = "";

                if (gethostname (localhost, HOST_NAME_MAX) == 0) {
                        gtk_label_set_text (GTK_LABEL (label), localhost);
                }

                /* If this isn't actually unique identifier for the computer, then
                 * don't bother showing it by default.
                 */
                if (strcmp (localhost, "localhost") == 0 ||
                    strcmp (localhost, "localhost.localdomain") == 0) {

                    rotate_computer_info (login_window);
                }
        }

        label = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-version-label"));
        if (label != NULL) {
                char *version;
                version = get_system_version ();
                gtk_label_set_text (GTK_LABEL (label), version);
                g_free (version);
        }

        gdm_profile_end (NULL);
}

#define INVISIBLE_CHAR_DEFAULT       '*'
#define INVISIBLE_CHAR_BLACK_CIRCLE  0x25cf
#define INVISIBLE_CHAR_WHITE_BULLET  0x25e6
#define INVISIBLE_CHAR_BULLET        0x2022
#define INVISIBLE_CHAR_NONE          0

static void
on_task_activated (GdmGreeterLoginWindow *login_window,
                   GdmTask               *task)
{
        GtkWidget *container;
        char *name;

        name = gdm_task_get_name (task);
        g_debug ("GdmGreeterLoginWindow: task '%s' activated", name);
        g_free (name);

        container = g_object_get_data (G_OBJECT (task),
                                       "gdm-greeter-login-window-page-container");

        if (container == NULL) {
                GtkWidget *page;

                container = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
                gtk_container_add (GTK_CONTAINER (login_window->priv->auth_page_box),
                                   container);

                page = gdm_conversation_get_page (GDM_CONVERSATION (task));
                if (page != NULL) {
                        gtk_container_add (GTK_CONTAINER (container), page);
                        gtk_widget_show (page);
                }
                g_object_set_data (G_OBJECT (task),
                                   "gdm-greeter-login-window-page-container",
                                   container);
        }

        gtk_widget_show (container);
        switch_mode (login_window, login_window->priv->dialog_mode);
}

static void
on_task_deactivated (GdmGreeterLoginWindow *login_window,
                     GdmTask               *task)
{
        GtkWidget *container;
        char *name;
        GtkActionGroup *actions;

        name = gdm_task_get_name (task);
        g_debug ("GdmGreeterLoginWindow: task '%s' now in background", name);
        g_free (name);

        container = g_object_get_data (G_OBJECT (task),
                                       "gdm-greeter-login-window-page-container");

        if (container != NULL) {
                gtk_widget_hide (container);
        }

        actions = gdm_conversation_get_actions (GDM_CONVERSATION (task));

        if (actions != NULL) {
                gtk_action_group_set_sensitive (actions, FALSE);
                gtk_action_group_set_visible (actions, FALSE);
                g_object_unref (actions);
        }
}

static void
register_custom_types (GdmGreeterLoginWindow *login_window)
{
        GType types[] = { GDM_TYPE_USER_CHOOSER_WIDGET,
                          GDM_TYPE_TASK_LIST };
        int i;

        for (i = 0; i < G_N_ELEMENTS (types); i++) {
                g_debug ("Registering type '%s'", g_type_name (types[i]));
        }
}

static void
load_theme (GdmGreeterLoginWindow *login_window)
{
        GtkWidget *button;
        GtkWidget *box;
        GtkWidget *image;
        GError* error = NULL;

        gdm_profile_start (NULL);

        register_custom_types (login_window);

        login_window->priv->builder = gtk_builder_new ();
        if (!gtk_builder_add_from_file (login_window->priv->builder, UIDIR "/" UI_XML_FILE, &error)) {
                g_warning ("Couldn't load builder file: %s", error->message);
                g_error_free (error);
        }

        g_assert (login_window->priv->builder != NULL);

        image = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "logo-image"));
        if (image != NULL) {
                char        *icon_name;
                GError      *error;

                error = NULL;
                icon_name = gconf_client_get_string (login_window->priv->client, KEY_LOGO, &error);
                if (error != NULL) {
                        g_debug ("GdmGreeterLoginWindow: unable to get logo icon name: %s", error->message);
                        g_error_free (error);
                }

                g_debug ("GdmGreeterLoginWindow: Got greeter logo '%s'",
                          icon_name ? icon_name : "(null)");
                if (icon_name != NULL) {
                        gtk_image_set_from_icon_name (GTK_IMAGE (image),
                                                      icon_name,
                                                      GTK_ICON_SIZE_DIALOG);
                        g_free (icon_name);
                }
        }

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "window-frame"));
        gtk_container_add (GTK_CONTAINER (login_window), box);

        login_window->priv->user_chooser = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "user-chooser"));

        gdm_user_chooser_widget_set_show_only_chosen (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser), TRUE);

        g_signal_connect (login_window->priv->user_chooser,
                          "loaded",
                          G_CALLBACK (on_users_loaded),
                          login_window);
        g_signal_connect (login_window->priv->user_chooser,
                          "activated",
                          G_CALLBACK (on_user_chooser_activated),
                          login_window);
        g_signal_connect (login_window->priv->user_chooser,
                          "deactivated",
                          G_CALLBACK (on_user_unchosen),
                          login_window);

        g_signal_connect_swapped (login_window->priv->user_chooser,
                                 "notify::list-visible",
                                 G_CALLBACK (on_user_chooser_visibility_changed),
                                 login_window);

        login_window->priv->conversation_list = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "task-list"));

        g_signal_connect_swapped (GDM_TASK_LIST (login_window->priv->conversation_list),
                                  "activated",
                                  G_CALLBACK (on_task_activated),
                                  login_window);
        g_signal_connect_swapped (GDM_TASK_LIST (login_window->priv->conversation_list),
                                  "deactivated",
                                  G_CALLBACK (on_task_deactivated),
                                  login_window);

        login_window->priv->auth_banner_label = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "auth-banner-label"));
        /*make_label_small_italic (login_window->priv->auth_banner_label);*/
        login_window->priv->auth_page_box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "auth-page-box"));

        button = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "cancel-button"));
        g_signal_connect (button, "clicked", G_CALLBACK (cancel_button_clicked), login_window);

        create_computer_info (login_window);

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "computer-info-event-box"));
        g_signal_connect (box, "button-press-event", G_CALLBACK (on_computer_info_label_button_press), login_window);

        switch_mode (login_window, MODE_SELECTION);

        gdm_profile_end (NULL);
}

static gboolean
gdm_greeter_login_window_key_press_event (GtkWidget   *widget,
                                          GdkEventKey *event)
{
        GdmGreeterLoginWindow *login_window;

        login_window = GDM_GREETER_LOGIN_WINDOW (widget);

        if (event->keyval == GDK_Escape) {
                if (login_window->priv->dialog_mode == MODE_AUTHENTICATION
                    || login_window->priv->dialog_mode == MODE_TIMED_LOGIN) {
                        do_cancel (GDM_GREETER_LOGIN_WINDOW (widget));
                }
        }

        return GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->key_press_event (widget, event);
}

static void
gdm_greeter_login_window_size_request (GtkWidget      *widget,
                                       GtkRequisition *requisition)
{
        int             monitor;
        GdkScreen      *screen;
        GtkRequisition  child_requisition;
        GdkRectangle    area;

        if (GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->size_request) {
                GTK_WIDGET_CLASS (gdm_greeter_login_window_parent_class)->size_request (widget, requisition);
        }

        if (!GTK_WIDGET_REALIZED (widget)) {
                return;
        }

        screen = gtk_widget_get_screen (widget);
        monitor = gdk_screen_get_monitor_at_window (screen, widget->window);
        gdk_screen_get_monitor_geometry (screen, monitor, &area);

        gtk_widget_get_child_requisition (GTK_BIN (widget)->child, &child_requisition);
        *requisition = child_requisition;

        requisition->width += 2 * GTK_CONTAINER (widget)->border_width;
        requisition->height += 2 * GTK_CONTAINER (widget)->border_width;

        /* Make width be at least 33% screen width, and at
         * most 80% screen width, and at most 600pixels,
         * and height be at most 80% of screen height
         */
        requisition->width = CLAMP (requisition->width, .33 * area.width, .80 * area.width);
        requisition->width = MIN (requisition->width, 600);
        requisition->height = MIN (requisition->height, .80 * area.height);

       /* Don't ever shrink window width
        */
       requisition->width = MAX (requisition->width, widget->allocation.width);
}

static void
update_banner_message (GdmGreeterLoginWindow *login_window)
{
        GError      *error;
        gboolean     enabled;

        if (login_window->priv->auth_banner_label == NULL) {
                /* if the theme doesn't have a banner message */
                g_debug ("GdmGreeterLoginWindow: theme doesn't support a banner message");
                return;
        }

        error = NULL;
        enabled = gconf_client_get_bool (login_window->priv->client, KEY_BANNER_MESSAGE_ENABLED, &error);
        if (error != NULL) {
                g_debug ("GdmGreeterLoginWindow: unable to get configuration: %s", error->message);
                g_error_free (error);
        }

        login_window->priv->banner_message_enabled = enabled;

        if (! enabled) {
                g_debug ("GdmGreeterLoginWindow: banner message disabled");
                gtk_widget_hide (login_window->priv->auth_banner_label);
        } else {
                char *message = NULL;
                error = NULL;
                if (user_chooser_has_no_user (login_window)) {
                        message = gconf_client_get_string (login_window->priv->client, KEY_BANNER_MESSAGE_TEXT_NOCHOOSER, &error);
                        if (error != NULL) {
                                g_debug("GdmGreeterLoginWindow: unable to get nochooser banner text: %s", error->message);
                                g_error_free(error);
                        }
                }
                error = NULL;
                if (message == NULL) {
                        message = gconf_client_get_string (login_window->priv->client, KEY_BANNER_MESSAGE_TEXT, &error);
                        if (error != NULL) {
                                g_debug("GdmGreeterLoginWindow: unable to get banner text: %s", error->message);
                                g_error_free(error);
                        }
                }
                if (message != NULL) {
                        char *markup;
                        markup = g_markup_printf_escaped ("<small><i>%s</i></small>", message);
                        gtk_label_set_markup (GTK_LABEL (login_window->priv->auth_banner_label),
                                              markup);
                        g_free (markup);
                }
                g_debug ("GdmGreeterLoginWindow: banner message: %s", message);

                gtk_widget_show (login_window->priv->auth_banner_label);
        }
}

static GObject *
gdm_greeter_login_window_constructor (GType                  type,
                                      guint                  n_construct_properties,
                                      GObjectConstructParam *construct_properties)
{
        GdmGreeterLoginWindow      *login_window;

        gdm_profile_start (NULL);

        login_window = GDM_GREETER_LOGIN_WINDOW (G_OBJECT_CLASS (gdm_greeter_login_window_parent_class)->constructor (type,
                                                                                                                      n_construct_properties,
                                                                                                                      construct_properties));


        load_theme (login_window);
        update_banner_message (login_window);

        gdm_profile_end (NULL);

        return G_OBJECT (login_window);
}

static void
gdm_greeter_login_window_class_init (GdmGreeterLoginWindowClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->get_property = gdm_greeter_login_window_get_property;
        object_class->set_property = gdm_greeter_login_window_set_property;
        object_class->constructor = gdm_greeter_login_window_constructor;
        object_class->finalize = gdm_greeter_login_window_finalize;

        widget_class->key_press_event = gdm_greeter_login_window_key_press_event;
        widget_class->size_request = gdm_greeter_login_window_size_request;

        signals [START_CONVERSATION] =
                g_signal_new ("start-conversation",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, start_conversation),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);
        signals [BEGIN_AUTO_LOGIN] =
                g_signal_new ("begin-auto-login",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_auto_login),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);
        signals [BEGIN_VERIFICATION] =
                g_signal_new ("begin-verification",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_verification),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [BEGIN_VERIFICATION_FOR_USER] =
                g_signal_new ("begin-verification-for-user",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, begin_verification_for_user),
                              NULL,
                              NULL,
                              gdm_marshal_VOID__STRING_STRING,
                              G_TYPE_NONE,
                              2, G_TYPE_STRING, G_TYPE_STRING);
        signals [QUERY_ANSWER] =
                g_signal_new ("query-answer",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, query_answer),
                              NULL,
                              NULL,
                              gdm_marshal_VOID__STRING_STRING,
                              G_TYPE_NONE,
                              2, G_TYPE_STRING, G_TYPE_STRING);
        signals [USER_SELECTED] =
                g_signal_new ("user-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, user_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [CANCELLED] =
                g_signal_new ("cancelled",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, cancelled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, disconnected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [START_SESSION] =
                g_signal_new ("start-session",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterLoginWindowClass, start_session),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_IS_INTERACTIVE,
                                         g_param_spec_boolean ("is-interactive",
                                                               "Is Interactive",
                                                               "Use has had an oppurtunity to interact with window",
                                                               FALSE,
                                                               G_PARAM_READABLE));

        g_type_class_add_private (klass, sizeof (GdmGreeterLoginWindowPrivate));
}

static void
on_gconf_key_changed (GConfClient           *client,
                      guint                  cnxn_id,
                      GConfEntry            *entry,
                      GdmGreeterLoginWindow *login_window)
{
        const char *key;
        GConfValue *value;

        key = gconf_entry_get_key (entry);
        value = gconf_entry_get_value (entry);

        if (strcmp (key, KEY_BANNER_MESSAGE_ENABLED) == 0) {
                if (value->type == GCONF_VALUE_BOOL) {
                        gboolean enabled;

                        enabled = gconf_value_get_bool (value);
                        g_debug ("setting key %s = %d", key, enabled);
                        login_window->priv->banner_message_enabled = enabled;
                        update_banner_message (login_window);
                } else {
                        g_warning ("Error retrieving configuration key '%s': Invalid type",
                                   key);
                }
        } else if (strcmp (key, KEY_BANNER_MESSAGE_TEXT) == 0 || strcmp (key, KEY_BANNER_MESSAGE_TEXT_NOCHOOSER) == 0) {
                if (login_window->priv->banner_message_enabled) {
                        update_banner_message (login_window);
                }
        } else {
                g_debug ("GdmGreeterLoginWindow: Config key not handled: %s", key);
        }
}

static void
on_conversation_answer (GdmGreeterLoginWindow *login_window,
                        const char            *text,
                        GdmConversation       *conversation)
{
        if (text != NULL) {
                char *service_name;

                service_name = gdm_conversation_get_service_name (conversation);
                if (service_name != NULL) {
                        g_signal_emit (login_window, signals[QUERY_ANSWER], 0, service_name, text);
                        g_free (service_name);
                }
        }

        set_sensitive (login_window, TRUE);
        set_ready (login_window);
}

static void
on_conversation_cancel (GdmGreeterLoginWindow *login_window,
                        GdmConversation       *conversation)
{
        restart_conversations (login_window);
}

static gboolean
on_conversation_chose_user (GdmGreeterLoginWindow *login_window,
                            const char            *username,
                            GdmConversation       *conversation)
{
        if (!gdm_chooser_widget_is_loaded (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser))) {
                char *name;

                name = gdm_task_get_name (GDM_TASK (conversation));
                g_warning ("Task %s is trying to choose user before list is loaded", name);
                g_free (name);
                return FALSE;
        }

        /* If we're already authenticating then we can't pick a user
         */
        if (login_window->priv->dialog_mode == MODE_AUTHENTICATION) {
                return FALSE;
        }

        if (gdm_task_list_set_active_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                           GDM_TASK (conversation))) {
                gdm_user_chooser_widget_set_chosen_user_name (GDM_USER_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                                              username);
        }

        return TRUE;
}

void
gdm_greeter_login_window_remove_extension (GdmGreeterLoginWindow *login_window,
 GdmGreeterExtension *extension)
{
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW_EXTENSION (extension));

        if (!GDM_IS_CONVERSATION (extension)) {
                return;
        }
}

static void
on_button_action_label_changed (GtkWidget *button)
{
        GtkAction *action;
        char *text;

        action = gtk_widget_get_action (button);

        g_object_get (G_OBJECT (action), "label", &text, NULL);

        gtk_button_set_label (GTK_BUTTON (button), text);
        g_free (text);
}

static void
on_button_action_icon_name_changed (GtkWidget *button)
{
        GtkAction *action;
        GtkWidget *image;

        action = gtk_widget_get_action (button);

        if (gtk_action_get_is_important (action)) {
                image = gtk_action_create_icon (GTK_ACTION (action), GTK_ICON_SIZE_BUTTON);
        } else {
                image = NULL;
        }

        gtk_button_set_image (GTK_BUTTON (button), image);

}

static void
on_button_action_tooltip_changed (GtkWidget *button)
{
        GtkAction *action;
        char *text;

        action = gtk_widget_get_action (button);

        g_object_get (G_OBJECT (action), "tooltip", &text, NULL);

        gtk_widget_set_tooltip_text (button, text);
        g_free (text);
}

static GtkWidget *
create_button_from_action (GtkAction *action)
{
        GtkWidget *button;

        button = gtk_button_new ();

        gtk_action_connect_proxy (GTK_ACTION (action), button);

        g_signal_connect_swapped (action,
                                  "notify::label",
                                  G_CALLBACK (on_button_action_label_changed),
                                  button);
        g_signal_connect_swapped (action,
                                  "notify::icon-name",
                                  G_CALLBACK (on_button_action_icon_name_changed),
                                  button);
        g_signal_connect_swapped (action,
                                  "notify::tooltip",
                                  G_CALLBACK (on_button_action_tooltip_changed),
                                  button);

        on_button_action_label_changed (button);
        on_button_action_icon_name_changed (button);
        on_button_action_tooltip_changed (button);

        if (strcmp (gtk_action_get_name (action),
                    GDM_CONVERSATION_DEFAULT_ACTION) == 0) {
                GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
        }

        return button;
}

static void
create_buttons_for_actions (GdmGreeterLoginWindow *login_window,
                            GtkActionGroup        *actions)
{
        GList *action_list;
        GList *node;
        GtkWidget *box;

        action_list = gtk_action_group_list_actions (actions);

        box = GTK_WIDGET (gtk_builder_get_object (login_window->priv->builder, "buttonbox"));
        for (node = action_list; node != NULL; node = node->next) {
                GtkAction *action;
                GtkWidget *button;

                action = node->data;

                button = create_button_from_action (action);
                gtk_container_add (GTK_CONTAINER (box), button);
        }

        g_list_free (action_list);
}

void
gdm_greeter_login_window_add_extension (GdmGreeterLoginWindow *login_window,
                                        GdmGreeterExtension *extension)
{
        char *name;
        char *description;
        char *service_name;
        GtkActionGroup *actions;

        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (login_window));
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW_EXTENSION (extension));

        if (!GDM_IS_CONVERSATION (extension)) {
                return;
        }

        name = gdm_task_get_name (GDM_TASK (extension));
        description = gdm_task_get_description (GDM_TASK (extension));

        if (!gdm_task_is_visible (GDM_TASK (extension))) {
                g_debug ("GdmGreeterLoginWindow: new extension '%s - %s' won't be added",
                         name, description);
                g_free (name);
                g_free (description);
                return;
        }

        actions = gdm_conversation_get_actions (GDM_CONVERSATION (extension));

        create_buttons_for_actions (login_window, actions);
        hide_task_actions (GDM_TASK (extension));

        g_object_unref (actions);

        g_signal_connect_swapped (GDM_CONVERSATION (extension),
                                  "answer",
                                  G_CALLBACK (on_conversation_answer),
                                  login_window);
        g_signal_connect_swapped (GDM_CONVERSATION (extension),
                                  "cancel",
                                  G_CALLBACK (on_conversation_cancel),
                                  login_window);
        g_signal_connect_swapped (GDM_CONVERSATION (extension),
                                  "user-chosen",
                                  G_CALLBACK (on_conversation_chose_user),
                                  login_window);

        g_debug ("GdmGreeterLoginWindow: new extension '%s - %s' added",
                name, description);

        gdm_task_list_add_task (GDM_TASK_LIST (login_window->priv->conversation_list),
                                GDM_TASK (extension));

        service_name = gdm_conversation_get_service_name (GDM_CONVERSATION (extension));

        if (gdm_task_is_choosable (GDM_TASK (extension))) {
                gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (login_window->priv->user_chooser),
                                             service_name, NULL, name, description, ~0,
                                             FALSE, TRUE, NULL, NULL);
        }

        g_free (name);
        g_free (description);

        g_debug ("GdmGreeterLoginWindow: starting conversation with '%s'", service_name);
        g_signal_emit (login_window, signals[START_CONVERSATION], 0, service_name);
        g_free (service_name);
}

static gboolean
on_window_state_event (GtkWidget           *widget,
                       GdkEventWindowState *event,
                       gpointer             data)
{
        if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
                g_debug ("GdmGreeterLoginWindow: window iconified");
                gtk_window_deiconify (GTK_WINDOW (widget));
        }

        return FALSE;
}

static void
gdm_greeter_login_window_init (GdmGreeterLoginWindow *login_window)
{
        GConfClient *client;
        GError      *error;
        gboolean     user_list_disable;

        gdm_profile_start (NULL);

        login_window->priv = GDM_GREETER_LOGIN_WINDOW_GET_PRIVATE (login_window);
        login_window->priv->timed_login_enabled = FALSE;
        login_window->priv->dialog_mode = MODE_UNDEFINED;

        client = gconf_client_get_default ();
        error = NULL;

        /* The user list is not shown only if the user list is disabled and
         * timed login is also not being used.
         */
        user_list_disable = gconf_client_get_bool (client,
                                                   KEY_DISABLE_USER_LIST,
                                                   &error);
        if (error != NULL) {
                g_debug ("GdmUserChooserWidget: unable to get disable-user-list configuration: %s", error->message);
                g_error_free (error);
        }

        login_window->priv->user_list_disabled = user_list_disable;

        gtk_window_set_title (GTK_WINDOW (login_window), _("Login Window"));
        /*gtk_window_set_opacity (GTK_WINDOW (login_window), 0.85);*/
        gtk_window_set_position (GTK_WINDOW (login_window), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_set_deletable (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_decorated (GTK_WINDOW (login_window), FALSE);
        gtk_window_set_keep_below (GTK_WINDOW (login_window), TRUE);
        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (login_window), TRUE);
        gtk_window_stick (GTK_WINDOW (login_window));
        gtk_container_set_border_width (GTK_CONTAINER (login_window), 0);

        g_signal_connect (login_window,
                          "window-state-event",
                          G_CALLBACK (on_window_state_event),
                          NULL);

        login_window->priv->client = gconf_client_get_default ();
        gconf_client_add_dir (login_window->priv->client,
                              KEY_GREETER_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
        login_window->priv->gconf_cnxn = gconf_client_notify_add (login_window->priv->client,
                                                                  KEY_GREETER_DIR,
                                                                  (GConfClientNotifyFunc)on_gconf_key_changed,
                                                                  login_window,
                                                                  NULL,
                                                                  NULL);
        gdm_profile_end (NULL);
}

static void
gdm_greeter_login_window_finalize (GObject *object)
{
        GdmGreeterLoginWindow *login_window;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_LOGIN_WINDOW (object));

        login_window = GDM_GREETER_LOGIN_WINDOW (object);

        g_return_if_fail (login_window->priv != NULL);

        if (login_window->priv->client != NULL) {
                g_object_unref (login_window->priv->client);
        }

        G_OBJECT_CLASS (gdm_greeter_login_window_parent_class)->finalize (object);
}

GtkWidget *
gdm_greeter_login_window_new (gboolean is_local)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_LOGIN_WINDOW,
                               "display-is-local", is_local,
                               "resizable", FALSE,
                               NULL);

        return GTK_WIDGET (object);
}
