/*
 * connection-capabilities.c - Capabilities interface implementation of HazeConnection
 * Copyright (C) 2005, 2006, 2008, 2009 Collabora Ltd.
 * Copyright (C) 2005, 2006, 2008 Nokia Corporation
 *
 * Copied heavily from telepathy-gabble
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
#include "config.h"

#include "connection-capabilities.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "connection.h"
#include "debug.h"

static void
haze_connection_update_capabilities (TpSvcConnectionInterfaceContactCapabilities1 *iface,
                                     const GPtrArray *clients,
                                     GDBusMethodInvocation *context)
{
  HazeConnection *self = HAZE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  tp_svc_connection_interface_contact_capabilities1_return_from_update_capabilities (
      context);
}

static GPtrArray *
haze_connection_get_handle_contact_capabilities (HazeConnection *self,
                                                 TpHandle handle)
{
  GPtrArray *arr = g_ptr_array_new ();
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_entity_type_value;
  const gchar * const text_allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE, NULL };

  if (0 == handle)
    {
      /* obsolete request for the connection's capabilities, do nothing */
      return arr;
    }

  /* TODO: Check for presence */

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_entity_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_entity_type_value, TP_ENTITY_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".TargetEntityType",
      target_entity_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, text_allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));

  return arr;
}

gboolean
haze_connection_contact_capabilities_fill_contact_attributes (
    HazeConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
  if (!tp_strdiff (dbus_interface,
        TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1))
    {
      GPtrArray *array;

      array = haze_connection_get_handle_contact_capabilities (self, handle);

      if (array->len > 0)
        {
          GValue *val = tp_g_value_slice_new (
              TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);

          g_value_take_boxed (val, array);
          tp_contact_attribute_map_take_sliced_gvalue (attributes,
              handle,
              TP_TOKEN_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1_CAPABILITIES,
              val);
        }
      else
        g_ptr_array_free (array, TRUE);

      return TRUE;
    }

  return FALSE;
}

void
haze_connection_contact_capabilities_iface_init (gpointer g_iface,
                                                 gpointer iface_data)
{
  TpSvcConnectionInterfaceContactCapabilities1Class *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_contact_capabilities1_implement_##x (\
    klass, haze_connection_##x)
  IMPLEMENT(update_capabilities);
#undef IMPLEMENT
}
