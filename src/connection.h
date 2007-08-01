#ifndef __HAZE_CONNECTION_H__
#define __HAZE_CONNECTION_H__

#include <glib-object.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/presence-mixin.h>

#include <prpl.h>

#include "contact-list.h"
#include "im-channel-factory.h"

G_BEGIN_DECLS

/* Must be in the same order as list_handle_strings in connection.c */
typedef enum
{
  HAZE_LIST_HANDLE_SUBSCRIBE = 1,
#if 0
  HAZE_LIST_HANDLE_PUBLISH,
  HAZE_LIST_HANDLE_KNOWN,
  HAZE_LIST_HANDLE_DENY
#endif
} HazeListHandle;

typedef struct _HazeConnection HazeConnection;
typedef struct _HazeConnectionClass HazeConnectionClass;

struct _HazeConnectionClass {
    TpBaseConnectionClass parent_class;
    TpPresenceMixinClass presence_class;
};

struct _HazeConnection {
    TpBaseConnection parent;

    PurpleAccount *account;

    HazeContactList *contact_list;
    HazeImChannelFactory *im_factory;

    TpPresenceMixin presence;

    gpointer priv;
};

#define ACCOUNT_GET_HAZE_CONNECTION(account) \
    (HAZE_CONNECTION ((account)->ui_data))
#define ACCOUNT_GET_TP_BASE_CONNECTION(account) \
    (TP_BASE_CONNECTION ((account)->ui_data))
#define HAZE_CONNECTION_GET_PRPL_INFO(conn) \
    (PURPLE_PLUGIN_PROTOCOL_INFO (conn->account->gc->prpl))

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

G_END_DECLS

#endif /* #ifndef __HAZE_CONNECTION_H__*/
