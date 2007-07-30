#include <string.h>

#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>

#include <accountopt.h>

#include "defines.h"
#include "connection.h"
#include "connection-presence.h"
#include "connection-aliasing.h"

enum
{
    PROP_USERNAME = 1,
    PROP_PASSWORD,
    PROP_SERVER,

    LAST_PROPERTY
};

G_DEFINE_TYPE_WITH_CODE(HazeConnection,
    haze_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
        tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        haze_connection_aliasing_iface_init);
    );

typedef struct _HazeConnectionPrivate
{
    char *username;
    char *password;
    char *server;
} HazeConnectionPrivate;

#define HAZE_CONNECTION_GET_PRIVATE(o) \
  ((HazeConnectionPrivate *)o->priv)

#define PC_GET_BASE_CONN(pc) \
    (ACCOUNT_GET_TP_BASE_CONNECTION (purple_connection_get_account (pc)))

void
signed_on_cb (PurpleConnection *pc, gpointer data)
{
    TpBaseConnection *base_conn = PC_GET_BASE_CONN (pc);

    tp_base_connection_change_status (base_conn,
        TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

void
signing_off_cb (PurpleConnection *pc, gpointer data)
{
    TpBaseConnection *base_conn = PC_GET_BASE_CONN (pc);

    /* FIXME: reason for disconnection, via
     *        PurpleConnectionUiOps.report_disconnect I guess
     */
    if(base_conn->status != TP_CONNECTION_STATUS_DISCONNECTED)
    {
        tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
    }
}

static gboolean
idle_signed_off_cb(gpointer data)
{
    PurpleAccount *account = (PurpleAccount *) data;
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    g_debug ("deleting account %s", account->username);
    purple_accounts_delete (account);
    tp_base_connection_finish_shutdown (TP_BASE_CONNECTION (conn));
    return FALSE;
}

void
signed_off_cb (PurpleConnection *pc, gpointer data)
{
    PurpleAccount *account = purple_connection_get_account (pc);
    g_idle_add(idle_signed_off_cb, account);
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);
    char *protocol, *password, *server, *prpl_id;
    PurpleAccount *account;
    PurplePlugin *prpl;
    PurplePluginProtocolInfo *prpl_info;
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    g_object_get(G_OBJECT(self),
                 "protocol", &protocol,
                 "password", &password,
                 "server", &server,
                 NULL);

    base->self_handle = tp_handle_ensure(contact_handles, priv->username,
                                         NULL, error);

    prpl_id = g_strconcat("prpl-", protocol, NULL);
    prpl = purple_find_prpl (prpl_id);
    g_assert (prpl);
    account = self->account = purple_account_new(priv->username, prpl_id);
    g_free(prpl_id);

    account->ui_data = self;
    purple_account_set_password (account, password);
    if (server && *server)
    {
        GList *l;
        PurpleAccountOption *option;
        prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (prpl);

        /* :'-( :'-( :'-( :'-( */
        for (l = prpl_info->protocol_options; l != NULL; l = l->next)
        {
            option = (PurpleAccountOption *)l->data;
            if (!strcmp (option->pref_name, "server") /* oscar */
                || !strcmp (option->pref_name, "connect_server")) /* xmpp */
            {
                purple_account_set_string (account, option->pref_name, server);
                break;
            }
        }
        if (l == NULL)
            g_warning ("server protocol option not found!");
    }
    purple_account_set_enabled(self->account, UI_ID, TRUE);
    purple_account_connect(self->account);

    tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTING,
                                     TP_CONNECTION_STATUS_REASON_REQUESTED);

    return TRUE;
}

static void
_haze_connection_shut_down (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    if(!self->account->disconnecting)
        purple_account_disconnect(self->account);
}

/* Must be in the same order as HazeListHandle in connection.h */
static const char *list_handle_strings[] =
{
    "subscribe",    /* HAZE_LIST_HANDLE_SUBSCRIBE */
#if 0
    "publish",      /* HAZE_LIST_HANDLE_PUBLISH */
    "known",        /* HAZE_LIST_HANDLE_KNOWN */
    "deny",         /* HAZE_LIST_HANDLE_DENY */
#endif
    NULL
};

static gchar*
_contact_normalize (TpHandleRepoIface *repo,
                    const gchar *id,
                    gpointer context,
                    GError **error)
{
    HazeConnection *conn = HAZE_CONNECTION (context);
    PurpleAccount *account = conn->account;
    return g_strdup (purple_normalize (account, id));
}

static void
_haze_connection_create_handle_repos (TpBaseConnection *base,
        TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
    repos[TP_HANDLE_TYPE_CONTACT] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT, _contact_normalize,
                                    base);
    /* repos[TP_HANDLE_TYPE_ROOM] = XXX MUC */
    repos[TP_HANDLE_TYPE_GROUP] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_GROUP, NULL, NULL);
    repos[TP_HANDLE_TYPE_LIST] =
        tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST, list_handle_strings);
}

static GPtrArray *
_haze_connection_create_channel_factories (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    GPtrArray *channel_factories = g_ptr_array_new ();

    self->im_factory = HAZE_IM_CHANNEL_FACTORY (
        g_object_new (HAZE_TYPE_IM_CHANNEL_FACTORY, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->im_factory);

    self->contact_list = HAZE_CONTACT_LIST (
        g_object_new (HAZE_TYPE_CONTACT_LIST, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->contact_list);

    return channel_factories;
}

gchar *
haze_connection_get_unique_connection_name(TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    return g_strdup(priv->username);
}

static void
haze_connection_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_USERNAME:
            g_value_set_string (value, priv->username);
            break;
        case PROP_PASSWORD:
            g_value_set_string (value, priv->password);
            break;
        case PROP_SERVER:
            g_value_set_string (value, priv->server);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_connection_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_USERNAME:
            g_free (priv->username);
            priv->username = g_value_dup_string(value);
            break;
        case PROP_PASSWORD:
            g_free (priv->password);
            priv->password = g_value_dup_string(value);
            break;
        case PROP_SERVER:
            g_free (priv->server);
            priv->server = g_value_dup_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static GObject *
haze_connection_constructor (GType type,
                             guint n_construct_properties,
                             GObjectConstructParam *construct_params)
{
    HazeConnection *self = HAZE_CONNECTION (
            G_OBJECT_CLASS (haze_connection_parent_class)->constructor (
                type, n_construct_properties, construct_params));

    g_debug("Post-construction: (HazeConnection *)%p", self);


    return (GObject *)self;
}

static void
haze_connection_dispose (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION(object);

    g_debug("disposing of (HazeConnection *)%p", self);

    G_OBJECT_CLASS (haze_connection_parent_class)->dispose (object);
}

static void
haze_connection_finalize (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    g_free (priv->username);
    g_free (priv->password);
    g_free (priv->server);
    self->priv = NULL;

    tp_presence_mixin_finalize (object);

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;
    static const gchar *interfaces_always_present[] = {
        TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
        TP_IFACE_CONNECTION_INTERFACE_ALIASING, /* FIXME: I'm lying */
        NULL };
    void *connection_handle = purple_connections_get_handle ();

    g_debug("Initializing (HazeConnectionClass *)%p", klass);

    g_type_class_add_private (klass, sizeof (HazeConnectionPrivate));
    object_class->get_property = haze_connection_get_property;
    object_class->set_property = haze_connection_set_property;
    object_class->constructor = haze_connection_constructor;
    object_class->dispose = haze_connection_dispose;
    object_class->finalize = haze_connection_finalize;

    base_class->create_handle_repos = _haze_connection_create_handle_repos;
    base_class->create_channel_factories =
        _haze_connection_create_channel_factories;
    base_class->get_unique_connection_name =
        haze_connection_get_unique_connection_name;
    base_class->start_connecting = _haze_connection_start_connecting;
    base_class->shut_down = _haze_connection_shut_down;
    base_class->interfaces_always_present = interfaces_always_present;

    param_spec = g_param_spec_string ("username", "Account username",
                                      "The username used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

    param_spec = g_param_spec_string ("password", "Account password",
                                      "The password used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

    param_spec = g_param_spec_string ("server", "Hostname or IP of server",
                                      "The server used when establishing a connection.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_SERVER, param_spec);

    purple_signal_connect(connection_handle, "signed-on",
                          klass, PURPLE_CALLBACK(signed_on_cb), NULL);
    purple_signal_connect(connection_handle, "signing-off",
                          klass, PURPLE_CALLBACK(signing_off_cb), NULL);
    purple_signal_connect(connection_handle, "signed-off",
                          klass, PURPLE_CALLBACK(signed_off_cb), NULL);

    haze_connection_presence_class_init (object_class);
    haze_connection_aliasing_class_init (object_class);
}

static void
haze_connection_init (HazeConnection *self)
{
    g_debug("Initializing (HazeConnection *)%p", self);
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_CONNECTION,
                                              HazeConnectionPrivate);

    haze_connection_presence_init (self);
}
