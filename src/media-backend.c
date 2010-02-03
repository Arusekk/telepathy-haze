/*
 * media-backend.c - Source for HazeMediaBackend
 * Copyright © 2006-2009 Collabora Ltd.
 * Copyright © 2006-2009 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "media-backend.h"

#include <libpurple/media/backend-iface.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include <string.h>

#include "debug.h"

static void media_backend_iface_init(PurpleMediaBackendIface *iface);
static void session_handler_iface_init (gpointer g_iface,
                                        gpointer iface_data);
static void haze_backend_state_changed_cb (PurpleMedia *media,
                                           PurpleMediaState state,
                                           const gchar *sid,
                                           const gchar *name,
                                           HazeMediaBackend *backend);

G_DEFINE_TYPE_WITH_CODE (HazeMediaBackend,
    haze_media_backend,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (PURPLE_TYPE_MEDIA_BACKEND,
      media_backend_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_SESSION_HANDLER,
      session_handler_iface_init);
    )

/* properties */
enum
{
  PROP_CONFERENCE_TYPE = 1,
  PROP_MEDIA,
  LAST_PROPERTY
};

/* private structure */
struct _HazeMediaBackendPrivate
{
  gchar *conference_type;
  PurpleMedia *media;
  GPtrArray *streams;

  gboolean ready;
};

static void
haze_media_backend_init (HazeMediaBackend *self)
{
  HazeMediaBackendPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      HAZE_TYPE_MEDIA_BACKEND, HazeMediaBackendPrivate);

  self->priv = priv;
}

static void
haze_media_backend_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  HazeMediaBackend *backend = HAZE_MEDIA_BACKEND (object);
  HazeMediaBackendPrivate *priv = backend->priv;

  switch (property_id)
    {
    case PROP_CONFERENCE_TYPE:
      g_value_set_string (value, priv->conference_type);
      break;
    case PROP_MEDIA:
      g_value_set_object (value, priv->media);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
haze_media_backend_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  HazeMediaBackend *backend = HAZE_MEDIA_BACKEND (object);
  HazeMediaBackendPrivate *priv = backend->priv;

  switch (property_id)
    {
    case PROP_CONFERENCE_TYPE:
      g_free (priv->conference_type);
      priv->conference_type = g_value_dup_string (value);
      break;
    case PROP_MEDIA:
      g_assert (priv->media == NULL);
      priv->media = g_value_get_object (value);

      if (!PURPLE_IS_MEDIA (priv->media))
        break;

      g_object_add_weak_pointer(G_OBJECT(priv->media),
          (gpointer*)&priv->media);
      g_signal_connect (priv->media, "state-changed",
          G_CALLBACK (haze_backend_state_changed_cb), backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void haze_media_backend_dispose (GObject *object);
static void haze_media_backend_finalize (GObject *object);

static void
haze_media_backend_class_init (HazeMediaBackendClass *haze_media_backend_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (haze_media_backend_class);

  g_type_class_add_private (haze_media_backend_class,
      sizeof (HazeMediaBackendPrivate));

  object_class->get_property = haze_media_backend_get_property;
  object_class->set_property = haze_media_backend_set_property;

  object_class->dispose = haze_media_backend_dispose;
  object_class->finalize = haze_media_backend_finalize;

  g_object_class_override_property(object_class, PROP_CONFERENCE_TYPE,
      "conference-type");
  g_object_class_override_property(object_class, PROP_MEDIA, "media");
}

void
haze_media_backend_dispose (GObject *object)
{
  DEBUG ("called");

  if (G_OBJECT_CLASS (haze_media_backend_parent_class)->dispose)
    G_OBJECT_CLASS (haze_media_backend_parent_class)->dispose (object);
}

void
haze_media_backend_finalize (GObject *object)
{
  HazeMediaBackend *self = HAZE_MEDIA_BACKEND (object);
  HazeMediaBackendPrivate *priv = self->priv;

  g_free(priv->conference_type);

  if (priv->streams != NULL)
    g_ptr_array_free (priv->streams, TRUE);

  G_OBJECT_CLASS (haze_media_backend_parent_class)->finalize (object);
}

static HazeMediaStream *
get_stream_by_name (HazeMediaBackend *self,
                    const gchar *sid)
{
  HazeMediaBackendPrivate *priv = self->priv;
  guint i;

  for (i = 0; i < priv->streams->len; ++i)
    {
      HazeMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (!strcmp (sid, stream->name))
        return stream;
    }

  return NULL;
}

static void
haze_backend_state_changed_cb (PurpleMedia *media,
                               PurpleMediaState state,
                               const gchar *sid,
                               const gchar *name,
                               HazeMediaBackend *backend)
{
  HazeMediaBackendPrivate *priv = backend->priv;

  if (state == PURPLE_MEDIA_STATE_END && sid != NULL && name == NULL)
    {
      HazeMediaStream *stream = get_stream_by_name (backend, sid);

      if (stream != NULL)
        {
          g_ptr_array_remove_fast (priv->streams, stream);
          g_object_unref (stream);
        }
    }
}

static void
_emit_new_stream (HazeMediaBackend *self,
                  HazeMediaStream *stream)
{
  gchar *object_path;
  guint id, media_type;

  g_object_get (stream,
                "object-path", &object_path,
                "id", &id,
                "media-type", &media_type,
                NULL);

  /* all of the streams are bidirectional from farsight's point of view, it's
   * just in the signalling they change */
  DEBUG ("emitting MediaSessionHandler:NewStreamHandler signal for %s stream %d",
      media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video", id);
  tp_svc_media_session_handler_emit_new_stream_handler (self,
      object_path, id, media_type, TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);
}

void
haze_media_backend_add_media_stream (HazeMediaBackend *self,
    HazeMediaStream *stream)
{
  HazeMediaBackendPrivate *priv = self->priv;

  if (priv->streams == NULL)
    priv->streams = g_ptr_array_new ();

  g_object_ref (stream);
  g_ptr_array_add (priv->streams, stream);

  if (priv->ready)
      _emit_new_stream (self, stream);
}

static gboolean
haze_media_backend_add_stream (PurpleMediaBackend *self,
    const gchar *sid, const gchar *who,
    PurpleMediaSessionType type, gboolean initiator,
    const gchar *transmitter,
    guint num_params, GParameter *params)
{
  DEBUG ("called");
  return TRUE;
}

static void
haze_media_backend_add_remote_candidates (PurpleMediaBackend *self,
                                          const gchar *sid,
                                          const gchar *who,
                                          GList *remote_candidates)
{
  HazeMediaStream *stream;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    haze_media_stream_add_remote_candidates (stream, remote_candidates);
  else
    DEBUG ("Couldn't find stream");
}

static gboolean
haze_media_backend_codecs_ready (PurpleMediaBackend *self,
                                 const gchar *sid)
{
  HazeMediaStream *stream;
  gboolean ready = FALSE;

  DEBUG ("called");

  if (sid != NULL)
    {
      stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

      if (stream != NULL)
        g_object_get (stream, "codecs-ready", &ready, NULL);

      return ready;
    }
  else
    {
      HazeMediaBackendPrivate *priv = HAZE_MEDIA_BACKEND (self)->priv;
      guint i;

      for (i = 0; i < priv->streams->len; ++i)
        {
          stream = g_ptr_array_index (priv->streams, i);

          if (stream != NULL)
            g_object_get (stream, "codecs-ready", &ready, NULL);

          if (!ready)
            return FALSE;
        }

      return TRUE;
    }
}

static GList *
haze_media_backend_get_codecs (PurpleMediaBackend *self,
                               const gchar *sid)
{
  HazeMediaStream *stream;
  GList *ret = NULL;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    ret = haze_media_stream_get_codecs (stream);

  return ret;
}

static GList *
haze_media_backend_get_local_candidates (PurpleMediaBackend *self,
                                         const gchar *sid,
                                         const gchar *who)
{
  HazeMediaStream *stream;
  GList *ret = NULL;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    ret = haze_media_stream_get_local_candidates (stream);

  return ret;
}

static gboolean
haze_media_backend_set_remote_codecs (PurpleMediaBackend *self,
                                      const gchar *sid,
                                      const gchar *who,
                                      GList *codecs)
{
  HazeMediaStream *stream;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    haze_media_stream_set_remote_codecs (stream, codecs);
  else
    DEBUG ("Couldn't find stream");

  return TRUE;
}

static gboolean
haze_media_backend_set_send_codec (PurpleMediaBackend *self,
                                   const gchar *sid,
                                   PurpleMediaCodec *codec)
{
  return FALSE;
}

static void
haze_media_backend_ready (TpSvcMediaSessionHandler *iface,
                          DBusGMethodInvocation *context)
{
  HazeMediaBackend *self = HAZE_MEDIA_BACKEND (iface);
  HazeMediaBackendPrivate *priv = self->priv;

  if (!priv->ready)
    {
      guint i;

      DEBUG ("emitting NewStreamHandler for each stream");

      priv->ready = TRUE;

      for (i = 0; i < priv->streams->len; i++)
        _emit_new_stream (self, g_ptr_array_index (priv->streams, i));
    }

  tp_svc_media_session_handler_return_from_ready (context);
}

static void
haze_media_backend_error (TpSvcMediaSessionHandler *iface,
                          guint errno,
                          const gchar *message,
                          DBusGMethodInvocation *context)
{
  HazeMediaBackend *self = HAZE_MEDIA_BACKEND (iface);
  HazeMediaBackendPrivate *priv;
  GPtrArray *tmp;
  guint i;

  g_assert (HAZE_IS_MEDIA_BACKEND (self));

  priv = self->priv;

  if (priv->media == NULL)
    {
      /* This could also be because someone called Error() before the
       * SessionHandler was announced. But the fact that the SessionHandler is
       * actually also the Channel, and thus this method is available before
       * NewSessionHandler is emitted, is an implementation detail. So the
       * error message describes the only legitimate situation in which this
       * could arise.
       */
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "call has already ended" };

      DEBUG ("no session, returning an error.");
      dbus_g_method_return_error (context, &e);
      return;
    }

  DEBUG ("Media.SessionHandler::Error called, error %u (%s) -- "
      "emitting error on each stream", errno, message);

  purple_media_end (priv->media, NULL, NULL);

  /* Calling haze_media_stream_error () on all the streams will ultimately
   * cause them all to emit 'closed'. In response to 'closed', stream_close_cb
   * unrefs them, and removes them from priv->streams. So, we copy the stream
   * list to avoid it being modified from underneath us.
   */
  tmp = g_ptr_array_sized_new (priv->streams->len);

  for (i = 0; i < priv->streams->len; i++)
    g_ptr_array_add (tmp, g_ptr_array_index (priv->streams, i));

  for (i = 0; i < tmp->len; i++)
    {
      HazeMediaStream *stream = g_ptr_array_index (tmp, i);

      haze_media_stream_error (stream, errno, message, NULL);
    }

  g_ptr_array_free (tmp, TRUE);

  tp_svc_media_session_handler_return_from_error (context);
}

static void
media_backend_iface_init (PurpleMediaBackendIface *iface)
{
#define IMPLEMENT(x) iface->x = haze_media_backend_##x
  IMPLEMENT(add_stream);
  IMPLEMENT(add_remote_candidates);
  IMPLEMENT(codecs_ready);
  IMPLEMENT(get_codecs);
  IMPLEMENT(get_local_candidates);
  IMPLEMENT(set_remote_codecs);
  IMPLEMENT(set_send_codec);
#undef IMPLEMENT
}

static void
session_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaSessionHandlerClass *klass =
    (TpSvcMediaSessionHandlerClass *) g_iface;

#define IMPLEMENT(x) tp_svc_media_session_handler_implement_##x (\
    klass, haze_media_backend_##x)
  IMPLEMENT(error);
  IMPLEMENT(ready);
#undef IMPLEMENT
}
