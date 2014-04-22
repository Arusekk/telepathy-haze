#ifndef __HAZE_CONNECTION_H__
#define __HAZE_CONNECTION_H__
/*
 * connection.h - HazeConnection header
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

#include <glib-object.h>
#include <telepathy-glib/telepathy-glib.h>

#include <libpurple/account.h>
#include <libpurple/prpl.h>

#include "contact-list.h"
#include "im-channel-factory.h"

G_BEGIN_DECLS

typedef struct _HazeConnection HazeConnection;
typedef struct _HazeConnectionPrivate HazeConnectionPrivate;
typedef struct _HazeConnectionClass HazeConnectionClass;

struct _HazeConnectionClass {
    TpBaseConnectionClass parent_class;
};

struct _HazeConnection {
    TpBaseConnection parent;

    PurpleAccount *account;

    HazeContactList *contact_list;
    HazeImChannelFactory *im_factory;
    TpSimplePasswordManager *password_manager;

    gchar **acceptable_avatar_mime_types;

    HazeConnectionPrivate *priv;
};

#define ACCOUNT_GET_HAZE_CONNECTION(account) \
    (HAZE_CONNECTION ((account)->ui_data))
#define ACCOUNT_GET_TP_BASE_CONNECTION(account) \
    (TP_BASE_CONNECTION ((account)->ui_data))
#define HAZE_CONNECTION_GET_PRPL_INFO(conn) \
    (PURPLE_PLUGIN_PROTOCOL_INFO (conn->account->gc->prpl))

PurpleAccountUiOps *haze_get_account_ui_ops (void);
PurpleConnectionUiOps *haze_get_connection_ui_ops (void);

const gchar *
haze_connection_handle_inspect (HazeConnection *conn,
                                TpEntityType entity_type,
                                TpHandle handle);

gboolean haze_connection_create_account (HazeConnection *self, GError **error);

GType haze_connection_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_CONNECTION \
  (haze_connection_get_type ())
#define HAZE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_CONNECTION, \
                              HazeConnection))
#define HAZE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_CONNECTION, \
                           HazeConnectionClass))
#define HAZE_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_CONNECTION))
#define HAZE_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_CONNECTION))
#define HAZE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_CONNECTION, \
                              HazeConnectionClass))

const gchar *haze_get_fallback_group (void);

GPtrArray * haze_connection_dup_implemented_interfaces (
        PurplePluginProtocolInfo *prpl_info);

const gchar **haze_connection_get_guaranteed_interfaces (void);

void haze_connection_request_password (PurpleAccount *account,
                                       gpointer user_data);
void haze_connection_cancel_password_request (PurpleAccount *account);

gboolean haze_connection_protocol_info_supports_avatar (
    PurplePluginProtocolInfo *prpl_info);

G_END_DECLS

#endif /* #ifndef __HAZE_CONNECTION_H__*/
