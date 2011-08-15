/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 * Written By: Ray Strode <rstrode@redhat.com>
 *
 */

#include <glib.h>
#include <glib-object.h>

#include <gtk/gtk.h>

#include "gdm-conversation.h"
#include "gdm-marshal.h"
#include "gdm-task.h"

enum {
        ANSWER,
        USER_CHOSEN,
        CANCEL,
        MESSAGE_SET,
        LAST_SIGNAL
};

typedef struct {
        char                  *text;
        GdmServiceMessageType  type;
} QueuedMessage;

static guint signals [LAST_SIGNAL] = { 0, };

static void gdm_conversation_class_init (gpointer g_iface);

GType
gdm_conversation_get_type (void)
{
        static GType type = 0;

        if (!type) {
                type = g_type_register_static_simple (G_TYPE_INTERFACE,
                                                      "GdmConversation",
                                                      sizeof (GdmConversationIface),
                                                      (GClassInitFunc) gdm_conversation_class_init,
                                                      0, NULL, 0);

                g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
                g_type_interface_add_prerequisite (type, GDM_TYPE_TASK);
        }

        return type;
}

static void
gdm_conversation_class_init (gpointer g_iface)
{
        GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);

        signals [ANSWER] =
                g_signal_new ("answer",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmConversationIface, answer),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [USER_CHOSEN] =
                g_signal_new ("user-chosen",
                              iface_type,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmConversationIface, user_chosen),
                              NULL,
                              NULL,
                              gdm_marshal_BOOLEAN__STRING,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_STRING);
        signals [CANCEL] =
                g_signal_new ("cancel",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmConversationIface, cancel),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [MESSAGE_SET] =
                g_signal_new ("message-set",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmConversationIface, message_set),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

void
gdm_conversation_set_message  (GdmConversation   *conversation,
                               const char        *message)
{
        GDM_CONVERSATION_GET_IFACE (conversation)->set_message (conversation, message);
}

static void
free_queued_message (QueuedMessage *message)
{
        g_free (message->text);
        g_slice_free (QueuedMessage, message);
}

static void
purge_message_queue (GdmConversation *conversation)
{
        GQueue    *message_queue;
        guint      message_timeout_id;

        message_queue = g_object_get_data (G_OBJECT (conversation),
                                           "gdm-conversation-message-queue");
        message_timeout_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (conversation),
                                                "gdm-conversation-message-timeout-id"));

        if (message_timeout_id) {
                g_source_remove (message_timeout_id);
                g_object_set_data (G_OBJECT (conversation),
                                   "gdm-conversation-message-timeout-id",
                                   GINT_TO_POINTER (0));
        }
        g_queue_foreach (message_queue,
                         (GFunc) free_queued_message,
                         NULL);
        g_queue_clear (message_queue);
}

static gboolean
dequeue_message (GdmConversation *conversation)
{
        GQueue    *message_queue;
        guint      message_timeout_id;

        message_queue = g_object_get_data (G_OBJECT (conversation),
                                           "gdm-conversation-message-queue");
        message_timeout_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (conversation),
                                                "gdm-conversation-message-timeout-id"));

        if (!g_queue_is_empty (message_queue)) {
                QueuedMessage *message;
                int duration;
                gboolean needs_beep;

                message = (QueuedMessage *) g_queue_pop_head (message_queue);

                switch (message->type) {
                        case GDM_SERVICE_MESSAGE_TYPE_INFO:
                                needs_beep = FALSE;
                                break;
                        case GDM_SERVICE_MESSAGE_TYPE_PROBLEM:
                                needs_beep = TRUE;
                                break;
                        default:
                                g_assert_not_reached ();
                }

                gdm_conversation_set_message (conversation, message->text);

                duration = (int) (g_utf8_strlen (message->text, -1) / 66.0) * 1000;
                duration = CLAMP (duration, 3000, 7000);

                message_timeout_id = g_timeout_add (duration,
                                                    (GSourceFunc) dequeue_message,
                                                    conversation);

                g_object_set_data (G_OBJECT (conversation),
                                   "gdm-conversation-message-timeout-id",
                                   GINT_TO_POINTER (message_timeout_id));

                if (needs_beep) {
                        gdk_window_beep (gtk_widget_get_window (GTK_WIDGET (gdm_conversation_get_page (GDM_CONVERSATION (conversation)))));
                }

                free_queued_message (message);
        } else {
                gdm_conversation_set_message (conversation, "");

                g_object_set_data (G_OBJECT (conversation),
                                   "gdm-conversation-message-timeout-id",
                                   GINT_TO_POINTER (0));

                gdm_conversation_message_set (conversation);
        }

        return FALSE;
}

static void
on_page_visible (GtkWidget       *widget,
                 GParamSpec      *pspec,
                 GdmConversation *conversation)
{
        guint message_timeout_id;

        if (!gtk_widget_get_visible (widget)) {
                return;
        }

        message_timeout_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (conversation),
                                                "gdm-conversation-message-timeout-id"));

        if (gdm_conversation_has_queued_messages (conversation) && message_timeout_id == 0) {
                dequeue_message (conversation);
        }
}

void
gdm_conversation_queue_message (GdmConversation   *conversation,
                                const char        *text,
                                GdmServiceMessageType type)
{
        GQueue    *message_queue;
        guint      message_timeout_id;
        QueuedMessage *message;
        GtkWidget *page;

        message_queue = g_object_get_data (G_OBJECT (conversation),
                                           "gdm-conversation-message-queue");
        message_timeout_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (conversation),
                                              "gdm-conversation-message-timeout-id"));
        page = gdm_conversation_get_page (conversation);

        if (message_queue == NULL) {
                message_queue = g_queue_new ();
                g_object_set_data (G_OBJECT (conversation),
                                   "gdm-conversation-message-queue",
                                   message_queue);
                g_signal_connect (G_OBJECT (page),
                                  "notify::visible",
                                  G_CALLBACK (on_page_visible),
                                  conversation);
        }

        message = g_slice_new (QueuedMessage);
        message->text = g_strdup (text);
        message->type = type;

        g_queue_push_tail (message_queue, message);

        if (message_timeout_id == 0 && gtk_widget_get_visible (page)) {
                dequeue_message (conversation);
        }
}

gboolean
gdm_conversation_has_queued_messages (GdmConversation *conversation)
{
        GQueue    *message_queue;
        guint      message_timeout_id;

        message_queue = g_object_get_data (G_OBJECT (conversation),
                                           "gdm-conversation-message-queue");

        if (message_queue == NULL) {
                return FALSE;
        }

        message_timeout_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (conversation),
                                                "gdm-conversation-message-timeout-id"));

        if (message_timeout_id != 0) {
                return TRUE;
        }

        if (!g_queue_is_empty (message_queue)) {
                return TRUE;
        }

        return FALSE;
}

void
gdm_conversation_ask_question (GdmConversation   *conversation,
                               const char        *message)
{
        GDM_CONVERSATION_GET_IFACE (conversation)->ask_question (conversation, message);
}

void
gdm_conversation_ask_secret (GdmConversation   *conversation,
                             const char        *message)
{
        GDM_CONVERSATION_GET_IFACE (conversation)->ask_secret (conversation, message);
}

void
gdm_conversation_reset (GdmConversation *conversation)
{

        gdm_conversation_set_message (conversation, "");
        purge_message_queue (conversation);

        return GDM_CONVERSATION_GET_IFACE (conversation)->reset (conversation);
}

void
gdm_conversation_set_ready (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->set_ready (conversation);
}

char *
gdm_conversation_get_service_name (GdmConversation   *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->get_service_name (conversation);
}

GtkWidget *
gdm_conversation_get_page (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->get_page (conversation);
}

GtkActionGroup *
gdm_conversation_get_actions (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->get_actions (conversation);
}

gboolean
gdm_conversation_focus (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->focus (conversation);
}

void
gdm_conversation_request_answer (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->request_answer (conversation);
}

/* protected
 */
void
gdm_conversation_answer (GdmConversation   *conversation,
                         const char        *answer)
{
        g_signal_emit (conversation, signals [ANSWER], 0, answer);
}

void
gdm_conversation_cancel (GdmConversation   *conversation)
{
        g_signal_emit (conversation, signals [CANCEL], 0);
}

gboolean
gdm_conversation_choose_user (GdmConversation *conversation,
                              const char      *username)
{
        gboolean was_chosen;

        was_chosen = FALSE;

        g_signal_emit (conversation, signals [USER_CHOSEN], 0, username, &was_chosen);

        return was_chosen;
}

void
gdm_conversation_message_set (GdmConversation *conversation)
{

        GQueue    *message_queue;

        message_queue = g_object_get_data (G_OBJECT (conversation),
                                           "gdm-conversation-message-queue");

        if (message_queue == NULL || g_queue_is_empty (message_queue)) {
                g_signal_emit (conversation, signals [MESSAGE_SET], 0);
        }
}
