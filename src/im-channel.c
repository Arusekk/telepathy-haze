#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#include "im-channel.h"
#include "connection.h"

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,

  LAST_PROPERTY
};

typedef struct _HazeIMChannelPrivate
{
    HazeConnection *conn;
    char *object_path;
    guint handle;
    PurpleConversation *conv;
} HazeIMChannelPrivate;

#define HAZE_IM_CHANNEL_GET_PRIVATE(o) \
  ((HazeIMChannelPrivate *)o->priv)

static void channel_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(HazeIMChannel, haze_im_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    )

#define CONV_IM_CHANNEL_KEY "haze-im-channel"

#if 0
static HazeIMChannel *
get_haze_im_channel (PurpleConversation *conv)
{
    gpointer thing = purple_conversation_get_data (conv, CONV_IM_CHANNEL_KEY);
    return HAZE_IM_CHANNEL (thing);
}   
#endif

static void
haze_im_channel_close (TpSvcChannel *iface,
                       DBusGMethodInvocation *context)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (iface);
    HazeIMChannelPrivate *priv = HAZE_IM_CHANNEL_GET_PRIVATE (self);

    purple_conversation_destroy (priv->conv);

    tp_svc_channel_return_from_close(context);
}

static void
haze_im_channel_get_channel_type (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
    tp_svc_channel_return_from_get_channel_type (context,
        TP_IFACE_CHANNEL_TYPE_TEXT);
}

static void
haze_im_channel_get_handle (TpSvcChannel *iface,
                            DBusGMethodInvocation *context)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (iface);
    HazeIMChannelPrivate *priv = HAZE_IM_CHANNEL_GET_PRIVATE (self);

    tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
        priv->handle);
}

static void
haze_im_channel_get_interfaces (TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  const char *interfaces[] = { NULL };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
    TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, haze_im_channel_##x)
    IMPLEMENT(close);
    IMPLEMENT(get_channel_type);
    IMPLEMENT(get_handle);
    IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}


void
haze_im_channel_send (TpSvcChannelTypeText *channel,
                      guint type,
                      const gchar *text,
                      DBusGMethodInvocation *context)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (channel);
    HazeIMChannelPrivate *priv = HAZE_IM_CHANNEL_GET_PRIVATE (self);
    GError *error = NULL;

    switch (type) {
        case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
        case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
        case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
        case TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY:
            purple_conv_im_send (PURPLE_CONV_IM (priv->conv), text);
            tp_svc_channel_type_text_return_from_send (context);

            break;

        default:
            
            g_debug ("invalid message type %u", type);
            g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                    "invalid message type: %u", type);
            dbus_g_method_return_error (context, error);
            g_error_free (error);

            return;
    }
}

static void
text_iface_init (gpointer g_iface, gpointer iface_data)
{
    TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)g_iface;

    tp_text_mixin_iface_init (g_iface, iface_data);
#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (\
        klass, haze_im_channel_##x)
    IMPLEMENT(send);
#undef IMPLEMENT
}

static void
haze_im_channel_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeIMChannel *chan = HAZE_IM_CHANNEL (object);
    HazeIMChannelPrivate *priv = HAZE_IM_CHANNEL_GET_PRIVATE (chan);

    switch (property_id) {
        case PROP_OBJECT_PATH:
            g_value_set_string (value, priv->object_path);
            break;
        case PROP_CHANNEL_TYPE:
            g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
            break;
        case PROP_HANDLE_TYPE:
            g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
            break;
        case PROP_HANDLE:
            g_value_set_uint (value, priv->handle);
            break;
        case PROP_CONNECTION:
            g_value_set_object (value, priv->conn);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_im_channel_set_property (GObject     *object,
                              guint        property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    HazeIMChannel *chan = HAZE_IM_CHANNEL (object);
    HazeIMChannelPrivate *priv = HAZE_IM_CHANNEL_GET_PRIVATE (chan);

    switch (property_id) {
        case PROP_OBJECT_PATH:
            g_free (priv->object_path);
            priv->object_path = g_value_dup_string (value);
            break;
        case PROP_HANDLE:
            /* we don't ref it here because we don't necessarily have access to the
             * contact repo yet - instead we ref it in the constructor.
             */
            priv->handle = g_value_get_uint (value);
            break;
        case PROP_HANDLE_TYPE:
            /* this property is writable in the interface, but not actually
             * meaningfully changable on this channel, so we do nothing */
            break;
        case PROP_CONNECTION:
            priv->conn = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}
static GObject *
haze_im_channel_constructor (GType type, guint n_props,
                             GObjectConstructParam *props)
{
    GObject *obj;
    TpHandleRepoIface *contact_handles;
    TpBaseConnection *conn;
    HazeIMChannelPrivate *priv;
    const char *recipient;
    DBusGConnection *bus;

    obj = G_OBJECT_CLASS (haze_im_channel_parent_class)->
        constructor (type, n_props, props);
    priv = HAZE_IM_CHANNEL_GET_PRIVATE(HAZE_IM_CHANNEL(obj));
    conn = TP_BASE_CONNECTION(priv->conn);

    contact_handles = tp_base_connection_get_handles (conn,
        TP_HANDLE_TYPE_CONTACT);
    tp_handle_ref (contact_handles, priv->handle);
    tp_text_mixin_init (obj, G_STRUCT_OFFSET (HazeIMChannel, text),
                        contact_handles);

    bus = tp_get_bus ();
    dbus_g_connection_register_g_object (bus, priv->object_path, obj);

    recipient = tp_handle_inspect(contact_handles, priv->handle);
    priv->conv = purple_conversation_new (PURPLE_CONV_TYPE_IM,
                                          priv->conn->account,
                                          recipient);
    purple_conversation_set_data (priv->conv, "haze-im-channel", obj);

    return obj;
}

static void
haze_im_channel_class_init (HazeIMChannelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;

    tp_text_mixin_class_init (object_class,
                              G_STRUCT_OFFSET(HazeIMChannelClass, text_class));

    g_type_class_add_private (klass, sizeof (HazeIMChannelPrivate));

    object_class->get_property = haze_im_channel_get_property;
    object_class->set_property = haze_im_channel_set_property;
    object_class->constructor = haze_im_channel_constructor;

    g_object_class_override_property (object_class, PROP_OBJECT_PATH,
        "object-path");
    g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
        "channel-type");
    g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
        "handle-type");
    g_object_class_override_property (object_class, PROP_HANDLE,
        "handle");

    param_spec = g_param_spec_object ("connection", "HazeConnection object",
                                      "Haze connection object that owns this "
                                      "IM channel object.",
                                      HAZE_TYPE_CONNECTION,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NICK |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
haze_im_channel_init (HazeIMChannel *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_IM_CHANNEL,
                                              HazeIMChannelPrivate);
}
