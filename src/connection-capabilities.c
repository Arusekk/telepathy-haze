/*
 * connection-capabilities.c - Capabilities interface implementation of HazeConnection
 * Copyright (C) 2009 Collabora Ltd.
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
#include "connection-capabilities.h"

#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/interfaces.h>

#include "connection.h"
#include "debug.h"
#include "mediamanager.h"

static PurpleMediaCaps
tp_flags_to_purple_caps (guint flags)
{
  PurpleMediaCaps caps = PURPLE_MEDIA_CAPS_NONE;
  if (flags & TP_CHANNEL_MEDIA_CAPABILITY_AUDIO)
    caps |= PURPLE_MEDIA_CAPS_AUDIO;
  if (flags & TP_CHANNEL_MEDIA_CAPABILITY_VIDEO)
    caps |= PURPLE_MEDIA_CAPS_VIDEO;
  return caps;
}

static guint
purple_caps_to_tp_flags (PurpleMediaCaps caps)
{
  guint flags = 0;
  if (caps & PURPLE_MEDIA_CAPS_AUDIO)
    flags |= TP_CHANNEL_MEDIA_CAPABILITY_AUDIO;
  if (caps & PURPLE_MEDIA_CAPS_VIDEO)
    flags |= TP_CHANNEL_MEDIA_CAPABILITY_VIDEO;
  return flags;
}

static void
_emit_capabilities_changed (HazeConnection *conn,
                            TpHandle handle,
                            const guint old_specific,
                            const guint new_specific)
{
  GPtrArray *caps_arr;
  guint i;

  /* o.f.T.C.Capabilities */

  caps_arr = g_ptr_array_new ();

  if (old_specific != 0 || new_specific != 0)
    {
      GValue caps_monster_struct = {0, };
      guint old_generic = old_specific ?
          TP_CONNECTION_CAPABILITY_FLAG_CREATE |
          TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;
      guint new_generic = new_specific ?
          TP_CONNECTION_CAPABILITY_FLAG_CREATE |
          TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;

      if (0 != (old_specific ^ new_specific))
        {
          g_value_init (&caps_monster_struct,
              TP_STRUCT_TYPE_CAPABILITY_CHANGE);
          g_value_take_boxed (&caps_monster_struct,
              dbus_g_type_specialized_construct
              (TP_STRUCT_TYPE_CAPABILITY_CHANGE));

          dbus_g_type_struct_set (&caps_monster_struct,
              0, handle,
              1, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
              2, old_generic,
              3, new_generic,
              4, old_specific,
              5, new_specific,
              G_MAXUINT);

          g_ptr_array_add (caps_arr, g_value_get_boxed (&caps_monster_struct));
        }
    }

  if (caps_arr->len)
    tp_svc_connection_interface_capabilities_emit_capabilities_changed (
        conn, caps_arr);

  for (i = 0; i < caps_arr->len; i++)
    {
      g_boxed_free (TP_STRUCT_TYPE_CAPABILITY_CHANGE,
          g_ptr_array_index (caps_arr, i));
    }

  g_ptr_array_free (caps_arr, TRUE);
}

/**
 * haze_connection_advertise_capabilities
 *
 * Implements D-Bus method AdvertiseCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 */
static void
haze_connection_advertise_capabilities (TpSvcConnectionInterfaceCapabilities *iface,
                                        const GPtrArray *add,
                                        const gchar **del,
                                        DBusGMethodInvocation *context)
{
  HazeConnection *self = HAZE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  guint i;
  GPtrArray *ret;
  PurpleMediaCaps old_caps, caps;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  caps = old_caps = purple_media_manager_get_ui_caps (
      purple_media_manager_get ());
  for (i = 0; i < add->len; i++)
    {
      GValue iface_flags_pair = {0, };
      gchar *channel_type;
      guint flags;

      g_value_init (&iface_flags_pair, TP_STRUCT_TYPE_CAPABILITY_PAIR);
      g_value_set_static_boxed (&iface_flags_pair, g_ptr_array_index (add, i));

      dbus_g_type_struct_get (&iface_flags_pair,
                              0, &channel_type,
                              1, &flags,
                              G_MAXUINT);

      if (g_str_equal (channel_type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
        caps |= tp_flags_to_purple_caps(flags);

      g_free (channel_type);
    }

  for (i = 0; NULL != del[i]; i++)
    {
      if (g_str_equal (del[i], TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
        {
          caps = PURPLE_MEDIA_CAPS_NONE;
          break;
        }
    }

  purple_media_manager_set_ui_caps (purple_media_manager_get(), caps);

  _emit_capabilities_changed (self, base->self_handle, old_caps, caps);

  ret = g_ptr_array_new ();

/* TODO: store caps and return them properly */

  tp_svc_connection_interface_capabilities_return_from_advertise_capabilities (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}

static const gchar *assumed_caps[] =
{
  TP_IFACE_CHANNEL_TYPE_TEXT,
  NULL
};

/**
 * haze_connection_get_handle_capabilities
 *
 * Add capabilities of handle to the given GPtrArray
 */
static void
haze_connection_get_handle_capabilities (HazeConnection *self,
                                         TpHandle handle,
                                         GPtrArray *arr)
{
  TpBaseConnection *conn = TP_BASE_CONNECTION (self);
  PurpleAccount *account = self->account;
  TpHandleRepoIface *contact_handles =
      tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *bname;
  guint typeflags = 0;
  PurpleMediaCaps caps;
  const gchar **assumed;

  if (0 == handle)
    {
      /* obsolete request for the connection's capabilities, do nothing */
      return;
    }

  /* TODO: Check for presence */

  if (handle == conn->self_handle)
    caps = purple_media_manager_get_ui_caps (purple_media_manager_get ());
  else
    {
      bname = tp_handle_inspect (contact_handles, handle);
      caps = purple_prpl_get_media_caps (account, bname);
    }

  typeflags = purple_caps_to_tp_flags(caps);

  if (typeflags != 0)
    {
      GValue monster = {0, };
      g_value_init (&monster, TP_STRUCT_TYPE_CONTACT_CAPABILITY);
      g_value_take_boxed (&monster,
          dbus_g_type_specialized_construct (
          TP_STRUCT_TYPE_CONTACT_CAPABILITY));
      dbus_g_type_struct_set (&monster,
          0, handle,
          1, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
          2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
             TP_CONNECTION_CAPABILITY_FLAG_INVITE,
          3, typeflags,
          G_MAXUINT);

      g_ptr_array_add (arr, g_value_get_boxed (&monster));
    }

  for (assumed = assumed_caps; NULL != *assumed; assumed++)
    {
      GValue monster = {0, };
      g_value_init (&monster, TP_STRUCT_TYPE_CONTACT_CAPABILITY);
      g_value_take_boxed (&monster,
          dbus_g_type_specialized_construct (
          TP_STRUCT_TYPE_CONTACT_CAPABILITY));

      dbus_g_type_struct_set (&monster,
          0, handle,
          1, *assumed,
          2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
             TP_CONNECTION_CAPABILITY_FLAG_INVITE,
          3, 0,
          G_MAXUINT);

      g_ptr_array_add (arr, g_value_get_boxed (&monster));
    }
}

/**
 * haze_connection_get_capabilities
 *
 * Implements D-Bus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 */
static void
haze_connection_get_capabilities (TpSvcConnectionInterfaceCapabilities *iface,
                                  const GArray *handles,
                                  DBusGMethodInvocation *context)
{  
  HazeConnection *self = HAZE_CONNECTION (iface);
  TpBaseConnection *conn = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  guint i;
  GPtrArray *ret;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (conn, context);

  if (!tp_handles_are_valid (contact_handles, handles, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_ptr_array_new ();

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);

      haze_connection_get_handle_capabilities (self, handle, ret);
    }

  tp_svc_connection_interface_capabilities_return_from_get_capabilities (
      context, ret);

  for (i = 0; i < ret->len; i++)
    {
      g_value_array_free (g_ptr_array_index (ret, i));
    }

  g_ptr_array_free (ret, TRUE);
}

static void
conn_capabilities_fill_contact_attributes (GObject *obj,
                                           const GArray *contacts,
                                           GHashTable *attributes_hash)
{
  HazeConnection *self = HAZE_CONNECTION (obj);
  guint i;
  GPtrArray *array = NULL;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      if (array == NULL)
        array = g_ptr_array_new ();

      haze_connection_get_handle_capabilities (self, handle, array);

      if (array->len > 0)
        {
          GValue *val =  tp_g_value_slice_new (
              TP_ARRAY_TYPE_CONTACT_CAPABILITY_LIST);

          g_value_take_boxed (val, array);
          tp_contacts_mixin_set_contact_attribute (attributes_hash,
              handle, TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES"/caps",
              val);

          array = NULL;
        }
    }

    if (array != NULL)
      g_ptr_array_free (array, TRUE);
}

void
haze_connection_capabilities_iface_init (gpointer g_iface,
                                         gpointer iface_data)
{
  TpSvcConnectionInterfaceCapabilitiesClass *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_capabilities_implement_##x (\
    klass, haze_connection_##x)
  IMPLEMENT(advertise_capabilities);
  IMPLEMENT(get_capabilities);
#undef IMPLEMENT
}

/*
 * HACK: polling after 10 seconds of getting the presence on jabber
 * connections to get the real caps. Libpurple doesn't yet provide
 * a way to indicate when caps change or even when the initial caps
 * are received.
 */
/*
 * This array should be in the HazeConnection structure, but I'm not
 * going to bother since this is just a hack.
 */
static GArray *caps_cb_ids = NULL;

typedef struct {
  PurpleConnection *pc;
  gchar *bname;
  PurpleMediaCaps caps;
} CapsReceivedData;

static gboolean
caps_received_cb_cb (gpointer data)
{
  CapsReceivedData *crd = data;
  PurpleAccount *account = purple_connection_get_account (crd->pc);
  HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *bname = crd->bname;
  TpHandle contact = tp_handle_ensure (contact_repo, bname, NULL, NULL);
  PurpleMediaCaps caps = purple_prpl_get_media_caps (account, bname);

  caps_cb_ids = g_array_remove_index (caps_cb_ids, 0);

  _emit_capabilities_changed (conn, contact,
      purple_caps_to_tp_flags(crd->caps),
      purple_caps_to_tp_flags(caps));

  g_free(crd->bname);
  g_slice_free(CapsReceivedData, crd);
  return FALSE;
}

static gboolean
caps_received_cb (PurpleConnection *pc,
                  const char *type,
                  const char *from,
                  xmlnode *presence)
{
  PurpleAccount *account = purple_connection_get_account (pc);
  CapsReceivedData *crd = g_slice_new0 (CapsReceivedData);
  gulong id;

  crd->pc = pc;
  crd->bname = g_strdup (from);
  crd->caps = purple_prpl_get_media_caps (account, from);

  id = g_timeout_add_seconds (10, caps_received_cb_cb, crd);
  g_array_append_val (caps_cb_ids, id);
  return FALSE;
}

static void
connection_status_changed_cb (HazeConnection *conn,
                              guint status,
                              guint reason,
                              HazeMediaManager *self)
{
  PurplePlugin *jabber;

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      caps_cb_ids = g_array_new (FALSE, FALSE, sizeof (gulong));
      jabber = purple_find_prpl ("prpl-jabber");

      if (jabber)
        purple_signal_connect (jabber, "jabber-receiving-presence",
            conn, PURPLE_CALLBACK (caps_received_cb), NULL);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      jabber = purple_find_prpl ("prpl-jabber");

      if (jabber)
        purple_signal_disconnect (jabber, "jabber-receiving-presence",
            conn, PURPLE_CALLBACK (caps_received_cb));

      while (caps_cb_ids->len > 0)
        {
          gulong tmp = g_array_index (caps_cb_ids, gulong, 0);
          caps_cb_ids = g_array_remove_index_fast (caps_cb_ids, 0);
          g_source_remove (tmp);
        }

      g_array_free (caps_cb_ids, TRUE);

      if (conn->status_changed_id != 0)
        {
          g_signal_handler_disconnect (conn, conn->status_changed_id);
          conn->status_changed_id = 0;
        }

      break;
    }
}
/* end hack */

void
haze_connection_capabilities_class_init (GObjectClass *object_class)
{
}

void
haze_connection_capabilities_init (GObject *object)
{
  HazeConnection *conn = HAZE_CONNECTION (object);
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (object),
      TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES,
      conn_capabilities_fill_contact_attributes);

  /* Part of the hack to get media caps right for jabber */
  conn->status_changed_id = g_signal_connect (conn,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}
