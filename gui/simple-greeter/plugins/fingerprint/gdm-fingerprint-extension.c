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

#include <config.h>
#include <stdlib.h>

#include "gdm-fingerprint-extension.h"
#include "gdm-conversation.h"
#include "gdm-task.h"

#include <dbus/dbus-glib.h>

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

struct _GdmFingerprintExtensionPrivate
{
        GIcon     *icon;
        GtkWidget *page;
        GtkActionGroup *actions;

        GtkWidget *message_label;
        GtkWidget *prompt_label;
        GtkWidget *prompt_entry;

        DBusGConnection *bus_connection;

        guint      answer_pending : 1;
};

static void gdm_fingerprint_extension_finalize (GObject *object);

static void gdm_task_iface_init (GdmTaskIface *iface);
static void gdm_conversation_iface_init (GdmConversationIface *iface);
static void gdm_greeter_extension_iface_init (GdmGreeterExtensionIface *iface);

G_DEFINE_TYPE_WITH_CODE (GdmFingerprintExtension,
                         gdm_fingerprint_extension,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_GREETER_EXTENSION,
                                                gdm_greeter_extension_iface_init)
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_TASK,
                                                gdm_task_iface_init)
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_CONVERSATION,
                                                gdm_conversation_iface_init));

static void
gdm_fingerprint_extension_set_message (GdmConversation *conversation,
                                       const char *message)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);
        gtk_widget_show (extension->priv->message_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->message_label), message);
}

static void
gdm_fingerprint_extension_ask_question (GdmConversation *conversation,
                                        const char      *message)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_fingerprint_extension_ask_secret (GdmConversation *conversation,
                                      const char      *message)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), FALSE);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_fingerprint_extension_reset (GdmConversation *conversation)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);
        gtk_widget_hide (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        extension->priv->answer_pending = FALSE;

        gdm_task_set_enabled (GDM_TASK (conversation), FALSE);
}

static void
gdm_fingerprint_extension_set_ready (GdmConversation *conversation)
{
        gdm_task_set_enabled (GDM_TASK (conversation), TRUE);
}

char *
gdm_fingerprint_extension_get_service_name (GdmConversation *conversation)
{
        return g_strdup (PAMSERVICENAME);
}

GtkWidget *
gdm_fingerprint_extension_get_page (GdmConversation *conversation)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);
        return extension->priv->page;
}

GtkActionGroup *
gdm_fingerprint_extension_get_actions (GdmConversation *conversation)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);

        return g_object_ref (extension->priv->actions);
}

void
gdm_fingerprint_extension_request_answer (GdmConversation *conversation)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);
        const char *text;

        if (!extension->priv->answer_pending) {
                gdm_conversation_answer (conversation, NULL);
                return;
        }

        extension->priv->answer_pending = FALSE;
        text = gtk_entry_get_text (GTK_ENTRY (extension->priv->prompt_entry));
        gdm_conversation_answer (conversation, text);

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
}

gboolean
gdm_fingerprint_extension_focus (GdmConversation *conversation)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (conversation);

        if (!extension->priv->answer_pending) {
                return FALSE;
        }

        gtk_widget_grab_focus (extension->priv->prompt_entry);
        return TRUE;
}

GIcon *
gdm_fingerprint_extension_get_icon (GdmTask *task)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (task);
        return g_object_ref (extension->priv->icon);
}

char *
gdm_fingerprint_extension_get_name (GdmTask *task)
{
        return g_strdup (_("Fingerprint Authentication"));
}

char *
gdm_fingerprint_extension_get_description (GdmTask *task)
{
        return g_strdup (_("Log into session with fingerprint"));
}

gboolean
gdm_fingerprint_extension_is_choosable (GdmTask *task)
{
        return FALSE;
}

gboolean
gdm_fingerprint_extension_is_visible (GdmTask *task)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (task);
        DBusGProxy *proxy;
        GError *error;
        char *object_path;
        char *contents, **lines;
        gboolean ret;
        guint i;

        /* Stolen from gnome-about-me.
         *
         * FIXME: We should fix pam_fprintd to return authinfo_unavail instead of
         * doing this distro specific hack.
         */

        if (g_file_get_contents ("/etc/sysconfig/authconfig",
                                 &contents, NULL, NULL) == FALSE) {
                return FALSE;
        }

        lines = g_strsplit (contents, "\n", -1);
        g_free (contents);

        ret = FALSE;

        for (i = 0; lines[i] ; i++) {
                if (g_str_has_prefix (lines[i], "USEFPRINTD=") != FALSE) {
                        char *value;

                        value = lines[i] + strlen ("USEFPRINTD=");
                        if (rpmatch (value)) {
                                ret = TRUE;
                                break;
                        }
                }
        }

        g_strfreev (lines);

        if (!ret) {
                return FALSE;
        }

        if (extension->priv->bus_connection == NULL) {
                return FALSE;
        }

        proxy = dbus_g_proxy_new_for_name (extension->priv->bus_connection,
                                           "net.reactivated.Fprint",
                                           "/net/reactivated/Fprint/Manager",
                                           "net.reactivated.Fprint.Manager");

        error = NULL;
        object_path = NULL,
        ret = dbus_g_proxy_call (proxy,
                                 "GetDefaultDevice",
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 &object_path,
                                 G_TYPE_INVALID);
        g_object_unref (proxy);

        if (!ret) {
                return FALSE;
        }

        if (object_path == NULL) {
                ret = FALSE;
        } else {
                ret = TRUE;
        }
        g_free (object_path);

        return ret;
}

static void
gdm_task_iface_init (GdmTaskIface *iface)
{
        iface->get_icon = gdm_fingerprint_extension_get_icon;
        iface->get_description = gdm_fingerprint_extension_get_description;
        iface->get_name = gdm_fingerprint_extension_get_name;
        iface->is_choosable = gdm_fingerprint_extension_is_choosable;
        iface->is_visible = gdm_fingerprint_extension_is_visible;
}

static void
gdm_conversation_iface_init (GdmConversationIface *iface)
{
        iface->set_message = gdm_fingerprint_extension_set_message;
        iface->ask_question = gdm_fingerprint_extension_ask_question;
        iface->ask_secret = gdm_fingerprint_extension_ask_secret;
        iface->reset = gdm_fingerprint_extension_reset;
        iface->set_ready = gdm_fingerprint_extension_set_ready;
        iface->get_service_name = gdm_fingerprint_extension_get_service_name;
        iface->get_page = gdm_fingerprint_extension_get_page;
        iface->get_actions = gdm_fingerprint_extension_get_actions;
        iface->request_answer = gdm_fingerprint_extension_request_answer;
        iface->focus = gdm_fingerprint_extension_focus;
}

static void
gdm_greeter_extension_iface_init (GdmGreeterExtensionIface *iface)
{
}

static void
gdm_fingerprint_extension_class_init (GdmFingerprintExtensionClass *extension_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (extension_class);

        object_class->finalize = gdm_fingerprint_extension_finalize;

        g_type_class_add_private (extension_class,
                                  sizeof (GdmFingerprintExtensionPrivate));
}

static void
gdm_fingerprint_extension_finalize (GObject *object)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (object);
}

static void
create_page (GdmFingerprintExtension *extension)
{
        GtkBuilder *builder;
        GObject *object;
        GError *error;

        builder = gtk_builder_new ();

        error = NULL;
        gtk_builder_add_from_file (builder,
                                   PLUGINDATADIR "/page.ui",
                                   &error);

        if (error != NULL) {
                g_warning ("Could not load UI file: %s", error->message);
                g_error_free (error);
                return;
        }

        object = gtk_builder_get_object (builder, "page");
        g_object_ref (object);

        extension->priv->page = GTK_WIDGET (object);

        object = gtk_builder_get_object (builder, "auth-prompt-label");
        g_object_ref (object);
        extension->priv->prompt_label = GTK_WIDGET (object);
        gtk_widget_hide (extension->priv->prompt_label);

        object = gtk_builder_get_object (builder, "auth-prompt-entry");
        g_object_ref (object);
        extension->priv->prompt_entry = GTK_WIDGET (object);
        gtk_widget_hide (extension->priv->prompt_entry);

        object = gtk_builder_get_object (builder, "auth-message-label");
        g_object_ref (object);
        extension->priv->message_label = GTK_WIDGET (object);
        gtk_widget_show (extension->priv->message_label);

        g_object_unref (builder);
}

static void
create_actions (GdmFingerprintExtension *extension)
{
        extension->priv->actions = gtk_action_group_new ("gdm-fingerprint-extension");
}

static void
gdm_fingerprint_extension_init (GdmFingerprintExtension *extension)
{
        extension->priv = G_TYPE_INSTANCE_GET_PRIVATE (extension,
                                                       GDM_TYPE_FINGERPRINT_EXTENSION,
                                                       GdmFingerprintExtensionPrivate);

        extension->priv->icon = g_themed_icon_new ("gdm-fingerprint");
        create_page (extension);
        create_actions (extension);

        extension->priv->bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);

        gdm_fingerprint_extension_reset (GDM_CONVERSATION (extension));
}
