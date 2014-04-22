/*
 * connection-presence.c - Presence interface implementation of HazeConnection
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <config.h>
#include "connection-presence.h"

#include "debug.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

typedef enum {
    HAZE_STATUS_AVAILABLE = 0,
    HAZE_STATUS_BUSY,
    HAZE_STATUS_AWAY,
    HAZE_STATUS_EXT_AWAY,
    HAZE_STATUS_INVISIBLE,
    HAZE_STATUS_OFFLINE,
    HAZE_STATUS_UNKNOWN,

    HAZE_NUM_STATUSES
} HazeStatusIndex;

/* Indexed by HazeStatusIndex */
static const TpPresenceStatusSpec statuses[] = {
    { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE, TRUE },
    { "busy", TP_CONNECTION_PRESENCE_TYPE_BUSY, TRUE, TRUE },
    { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, TRUE },
    { "xa", TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY, TRUE, TRUE },
    { "hidden", TP_CONNECTION_PRESENCE_TYPE_HIDDEN, TRUE, FALSE },
    { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE, FALSE },
    { "unknown", TP_CONNECTION_PRESENCE_TYPE_UNKNOWN, FALSE, FALSE },
    { NULL }
};

/* Indexed by HazeStatusIndex */
static const PurpleStatusPrimitive primitives[] = {
    PURPLE_STATUS_AVAILABLE,
    PURPLE_STATUS_UNAVAILABLE,
    PURPLE_STATUS_AWAY,
    PURPLE_STATUS_EXTENDED_AWAY,
    PURPLE_STATUS_INVISIBLE,
    PURPLE_STATUS_UNSET,
};

/* Indexed by PurpleStatusPrimitive */
static const HazeStatusIndex status_indices[] = {
    HAZE_NUM_STATUSES,     /* invalid! */
    HAZE_STATUS_OFFLINE,   /* PURPLE_STATUS_OFFLINE */
    HAZE_STATUS_AVAILABLE, /* PURPLE_STATUS_AVAILABLE */
    HAZE_STATUS_BUSY,      /* PURPLE_STATUS_UNAVAILABLE */
    HAZE_STATUS_INVISIBLE, /* PURPLE_STATUS_INVISIBLE */
    HAZE_STATUS_AWAY,      /* PURPLE_STATUS_AWAY */
    HAZE_STATUS_EXT_AWAY   /* PURPLE_STATUS_EXTENDED_AWAY */
};

static TpPresenceStatus *
_get_tp_status (PurpleStatus *p_status)
{
    PurpleStatusType *type;
    PurpleStatusPrimitive prim;
    guint status_ix = -1;
    const gchar *xhtml_message;
    gchar *message = NULL;
    TpPresenceStatus *tp_status;

    if (p_status == NULL)
    {
        status_ix = HAZE_STATUS_UNKNOWN;
    }
    else
    {
        type = purple_status_get_type (p_status);
        prim = purple_status_type_get_primitive (type);

        if (prim <= 0 || prim >= G_N_ELEMENTS (status_indices))
        {
            /* guess wildly rather than crashing */
            status_ix = HAZE_STATUS_AVAILABLE;
        }
        else
        {
            status_ix = status_indices[prim];
        }

        xhtml_message = purple_status_get_attr_string (p_status, "message");
        if (xhtml_message)
        {
            message = purple_markup_strip_html (xhtml_message);
        }
    }

    tp_status = tp_presence_status_new (status_ix, message);
    g_free (message);
    return tp_status;
}

static const char *
_get_purple_status_id (HazeConnection *self,
                       guint index)
{
    PurpleStatusPrimitive prim = PURPLE_STATUS_UNSET;
    PurpleStatusType *type;

    g_assert (index < HAZE_NUM_STATUSES);
    prim = primitives[index];

    type = purple_account_get_status_type_with_primitive (self->account, prim);
    if (type)
    {
        return (purple_status_type_get_id (type));
    }
    else
    {
        return NULL;
    }
}

static gboolean
_status_available (TpPresenceMixin *mixin,
                   guint index)
{
    HazeConnection *self = HAZE_CONNECTION (mixin);
    /* FIXME: (a) should we be able to set offline on ourselves;
     *        (b) deal with some protocols not having status messages.
     */
    return (_get_purple_status_id (self, index) != NULL);
}


static TpPresenceStatus *
_get_contact_status (TpPresenceMixin *mixin,
    TpHandle handle)
{
    HazeConnection *conn = HAZE_CONNECTION (mixin);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (mixin);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, TP_ENTITY_TYPE_CONTACT);
    PurpleStatus *p_status;

    g_assert (tp_handle_is_valid (handle_repo, handle, NULL));

    if (handle == tp_base_connection_get_self_handle (base_conn))
    {
        p_status = purple_account_get_active_status (conn->account);
    }
    else
    {
        const gchar *bname;
        PurpleBuddy *buddy;

        bname = tp_handle_inspect (handle_repo, handle);
        buddy = purple_find_buddy (conn->account, bname);

        if (buddy)
        {
            PurplePresence *presence = purple_buddy_get_presence (buddy);

            p_status = purple_presence_get_active_status (presence);
        }
        else
        {
            DEBUG ("[%s] %s isn't on the blist, ergo no status!",
                     conn->account->username, bname);
            p_status = NULL;
        }
    }

    return _get_tp_status (p_status);
}

void
haze_connection_presence_account_status_changed (PurpleAccount *account,
                                                 PurpleStatus *status)
{
    TpBaseConnection *base_conn;
    TpPresenceStatus *tp_status;

    /* This gets called as soon as the account is created, before we get a
     * chance to set ui_data.  This is a "shame".  (You'd think that an account
     * could not have a status before it is enabled, but you'd be "wrong".)
     */
    if (account->ui_data)
    {
        base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);
        tp_status = _get_tp_status (status);

        tp_presence_mixin_emit_one_presence_update (
            TP_PRESENCE_MIXIN (base_conn),
            tp_base_connection_get_self_handle (base_conn), tp_status);
    }
}

static void
update_status (PurpleBuddy *buddy,
               PurpleStatus *status)
{
    PurpleAccount *account = purple_buddy_get_account (buddy);
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, TP_ENTITY_TYPE_CONTACT);

    const gchar *bname = purple_buddy_get_name (buddy);
    TpHandle handle = tp_handle_ensure (handle_repo, bname, NULL, NULL);

    TpPresenceStatus *tp_status;

    DEBUG ("%s changed to status %s", bname, purple_status_get_id (status));

    tp_status = _get_tp_status (status);

    tp_presence_mixin_emit_one_presence_update (TP_PRESENCE_MIXIN (conn),
        handle, tp_status);
}

static void
status_changed_cb (PurpleBuddy *buddy,
                   PurpleStatus *old_status,
                   PurpleStatus *new_status,
                   gpointer unused)
{
    update_status (buddy, new_status);
}

static void
signed_on_off_cb (PurpleBuddy *buddy,
                  gpointer data)
{
    /*
    gboolean signed_on = GPOINTER_TO_INT (data);
    */
    PurplePresence *presence = purple_buddy_get_presence (buddy);
    update_status (buddy, purple_presence_get_active_status (presence));
}

static gboolean
_set_own_status (TpPresenceMixin *mixin,
                 const TpPresenceStatus *status,
                 GError **error)
{
    HazeConnection *self = HAZE_CONNECTION (mixin);
    const char *status_id = NULL;
    const gchar *message = NULL;
    GList *attrs = NULL;

    if (status != NULL)
      {
        status_id = _get_purple_status_id (self, status->index);
        message = status->message;
      }

    if (status_id == NULL)
      {
        /* TODO: Is there a more sensible way to have a default? */
        DEBUG ("defaulting to 'available' status");
        status_id = "available";
      }

    if (message != NULL)
      {
        attrs = g_list_append (attrs, "message");
        attrs = g_list_append (attrs, (gchar *) message);
      }

    purple_account_set_status_list (self->account, status_id, TRUE, attrs);

    g_list_free (attrs);

    return TRUE;
}

void
haze_connection_presence_class_init (GObjectClass *object_class)
{
    void *blist_handle = purple_blist_get_handle ();

    purple_signal_connect (blist_handle, "buddy-status-changed", object_class,
        PURPLE_CALLBACK (status_changed_cb), NULL);
    purple_signal_connect (blist_handle, "buddy-signed-on", object_class,
        PURPLE_CALLBACK (signed_on_off_cb), GINT_TO_POINTER (TRUE));
    purple_signal_connect (blist_handle, "buddy-signed-off", object_class,
        PURPLE_CALLBACK (signed_on_off_cb), GINT_TO_POINTER (FALSE));
}

void
haze_connection_presence_iface_init (TpPresenceMixinInterface *iface)
{
  iface->status_available = _status_available;
  iface->get_contact_status = _get_contact_status;
  iface->set_own_status = _set_own_status;
  iface->statuses = statuses;
}
