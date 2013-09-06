/**
 * @file media.h Media API
 * @ingroup core
 */

/* purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#ifndef _PURPLE_MEDIA_H_
#define _PURPLE_MEDIA_H_

#include <glib.h>
#include <glib-object.h>

#include "media/candidate.h"
#include "media/codec.h"
#include "media/enum-types.h"

#define PURPLE_TYPE_MEDIA            (purple_media_get_type())
#define PURPLE_MEDIA(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), PURPLE_TYPE_MEDIA, PurpleMedia))
#define PURPLE_MEDIA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), PURPLE_TYPE_MEDIA, PurpleMediaClass))
#define PURPLE_IS_MEDIA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), PURPLE_TYPE_MEDIA))
#define PURPLE_IS_MEDIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), PURPLE_TYPE_MEDIA))
#define PURPLE_MEDIA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), PURPLE_TYPE_MEDIA, PurpleMediaClass))

/** A structure representing a media call. */
typedef struct _PurpleMedia PurpleMedia;

#include "signals.h"
#include "util.h"

#ifdef USE_VV

/** @copydoc _PurpleMediaClass */
typedef struct _PurpleMediaClass    PurpleMediaClass;
/** @copydoc _PurpleMediaPrivate */
typedef struct _PurpleMediaPrivate  PurpleMediaPrivate;

/** The media instance */
struct _PurpleMedia
{
	GObject parent;                /**< The parent of this object. */
	PurpleMediaPrivate *priv;      /**< The private data of this object. */
};

/** The media class */
struct _PurpleMediaClass
{
	GObjectClass parent_class;     /**< The parent class. */

	void (*purple_reserved1)(void);
	void (*purple_reserved2)(void);
	void (*purple_reserved3)(void);
	void (*purple_reserved4)(void);
};

#endif

G_BEGIN_DECLS

/**
 * Gets the media class's GType
 *
 * @return The media class's GType.
 */
GType purple_media_get_type(void);

/**
 * Gets a list of session IDs.
 *
 * @param media The media session from which to retrieve session IDs.
 *
 * @return GList of session IDs. The caller must free the list.
 */
GList *purple_media_get_session_ids(PurpleMedia *media);

/**
 * Gets the PurpleAccount this media session is on.
 *
 * @param media The media session to retrieve the account from.
 *
 * @return The account retrieved.
 */
PurpleAccount *purple_media_get_account(PurpleMedia *media);

/**
 * Gets the protocol data from the media session.
 *
 * @param media The media session to retrieve the protocol data from.
 *
 * @return The protocol data retrieved.
 */
gpointer purple_media_get_protocol_data(PurpleMedia *media);

/**
 * Sets the protocol data on the media session.
 *
 * @param media The media session to set the protocol data on.
 * @param protocol_data The data to set on the media session.
 */
void purple_media_set_protocol_data(PurpleMedia *media, gpointer protocol_data);

/**
 * Signals an error in the media session.
 *
 * @param media The media object to set the state on.
 * @param error The format of the error message to send in the signal.
 * @param ... The arguments to plug into the format.
 */
void purple_media_error(PurpleMedia *media, const gchar *error, ...);

/**
 * Ends all streams that match the given parameters
 *
 * @param media The media object with which to end streams.
 * @param session_id The session to end streams on.
 * @param participant The participant to end streams with.
 */
void purple_media_end(PurpleMedia *media, const gchar *session_id,
		const gchar *participant);

/**
 * Signals different information about the given stream.
 *
 * @param media The media instance to containing the stream to signal.
 * @param type The type of info being signaled.
 * @param session_id The id of the session of the stream being signaled.
 * @param participant The participant of the stream being signaled.
 * @param local TRUE if the info originated locally, FALSE if on the remote end.
 */
void purple_media_stream_info(PurpleMedia *media, PurpleMediaInfoType type,
		const gchar *session_id, const gchar *participant,
		gboolean local);

/**
 * Sets various optional parameters of the media call.
 *
 * Currently supported are:
 *   - "sdes-cname"    : The CNAME for the RTP sessions
 *   - "sdes-name"     : Real name used to describe the source in SDES messages
 *   - "sdes-tool"     : The TOOL to put in SDES messages
 *   - "sdes-email"    : Email address to put in SDES messages
 *   - "sdes-location" : The LOCATION to put in SDES messages
 *   - "sdes-note"     : The NOTE to put in SDES messages
 *   - "sdes-phone"    : The PHONE to put in SDES messages
 *
 * @param media The media object to set the parameters on.
 * @param num_params The number of parameters to pass
 * @param params Array of @c GParameter to pass
 */
void purple_media_set_params(PurpleMedia *media,
		guint num_params, GParameter *params);

/**
 * Gets the list of optional parameters supported by the media backend.
 *
 * The list is owned by the @c PurpleMedia internals and should NOT be freed.
 *
 * @param media The media object
 *
 * @return NULL-terminated array of names of supported parameters.
 */
const gchar **purple_media_get_available_params(PurpleMedia *media);

/**
 * Checks if given optional parameter is supported by the media backend.
 *
 * @param media The media object
 * @param param name of parameter
 *
 * @return @c TRUE if backend recognizes the parameter, @c FALSE otherwise.
 */
gboolean purple_media_param_is_supported(PurpleMedia *media, const gchar *param);

/**
 * Adds a stream to a session.
 *
 * It only adds a stream to one audio session or video session as
 * the @c sess_id must be unique between sessions.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to add the stream to.
 * @param who The name of the remote user to add the stream for.
 * @param type The type of stream to create.
 * @param initiator Whether or not the local user initiated the stream.
 * @param transmitter The transmitter to use for the stream.
 * @param num_params The number of parameters to pass to Farsight.
 * @param params The parameters to pass to Farsight.
 *
 * @return @c TRUE The stream was added successfully, @c FALSE otherwise.
 */
gboolean purple_media_add_stream(PurpleMedia *media, const gchar *sess_id,
		const gchar *who, PurpleMediaSessionType type,
		gboolean initiator, const gchar *transmitter,
		guint num_params, GParameter *params);

/**
 * Gets the session type from a session
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to get the type from.
 *
 * @return The retreived session type.
 */
PurpleMediaSessionType purple_media_get_session_type(PurpleMedia *media, const gchar *sess_id);

/**
 * Gets the PurpleMediaManager this media session is a part of.
 *
 * @param media The media object to get the manager instance from.
 *
 * @return The PurpleMediaManager instance retrieved.
 */
struct _PurpleMediaManager *purple_media_get_manager(PurpleMedia *media);

/**
 * Gets the codecs from a session.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to get the codecs from.
 *
 * @return The retreieved codecs.
 */
GList *purple_media_get_codecs(PurpleMedia *media, const gchar *sess_id);

/**
 * Adds remote candidates to the stream.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session find the stream in.
 * @param participant The name of the remote user to add the candidates for.
 * @param remote_candidates The remote candidates to add.
 */
void purple_media_add_remote_candidates(PurpleMedia *media,
					const gchar *sess_id,
					const gchar *participant,
					GList *remote_candidates);

/**
 * Gets the local candidates from a stream.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to find the stream in.
 * @param participant The name of the remote user to get the candidates from.
 */
GList *purple_media_get_local_candidates(PurpleMedia *media,
					 const gchar *sess_id,
					 const gchar *participant);

/**
 * Gets the active local candidates for the stream.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to find the stream in.
 * @param participant The name of the remote user to get the active candidate
 *                    from.
 *
 * @return The active candidates retrieved.
 */
GList *purple_media_get_active_local_candidates(PurpleMedia *media,
		const gchar *sess_id, const gchar *participant);

/**
 * Gets the active remote candidates for the stream.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to find the stream in.
 * @param participant The name of the remote user to get the remote candidate
 *                    from.
 *
 * @return The remote candidates retrieved.
 */
GList *purple_media_get_active_remote_candidates(PurpleMedia *media,
		const gchar *sess_id, const gchar *participant);

/**
 * Sets remote candidates from the stream.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session find the stream in.
 * @param participant The name of the remote user to set the candidates from.
 * @param codecs The list of remote codecs to set.
 *
 * @return @c TRUE The codecs were set successfully, or @c FALSE otherwise.
 */
gboolean purple_media_set_remote_codecs(PurpleMedia *media, const gchar *sess_id,
					const gchar *participant, GList *codecs);

/**
 * Returns whether or not the candidates for set of streams are prepared
 *
 * @param media The media object to find the remote user in.
 * @param session_id The session id of the session to check.
 * @param participant The remote user to check for.
 *
 * @return @c TRUE All streams for the given session_id/participant combination have candidates prepared, @c FALSE otherwise.
 */
gboolean purple_media_candidates_prepared(PurpleMedia *media,
		const gchar *session_id, const gchar *participant);

/**
 * Sets the send codec for the a session.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to set the codec for.
 * @param codec The codec to set the session to stream.
 *
 * @return @c TRUE The codec was successfully changed, or @c FALSE otherwise.
 */
gboolean purple_media_set_send_codec(PurpleMedia *media, const gchar *sess_id, PurpleMediaCodec *codec);

/**
 * Gets whether a session's codecs are ready to be used.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to check.
 *
 * @return @c TRUE The codecs are ready, or @c FALSE otherwise.
 */
gboolean purple_media_codecs_ready(PurpleMedia *media, const gchar *sess_id);

/**
 * Gets whether the local user is the conference/session/stream's initiator.
 *
 * @param media The media instance to find the session in.
 * @param sess_id The session id of the session to check.
 * @param participant The participant of the stream to check.
 *
 * @return TRUE if the local user is the stream's initator, else FALSE.
 */
gboolean purple_media_is_initiator(PurpleMedia *media,
		const gchar *sess_id, const gchar *participant);

/**
 * Gets whether a streams selected have been accepted.
 *
 * @param media The media object to find the session in.
 * @param sess_id The session id of the session to check.
 * @param participant The participant to check.
 *
 * @return @c TRUE The selected streams have been accepted, or @c FALSE otherwise.
 */
gboolean purple_media_accepted(PurpleMedia *media, const gchar *sess_id,
		const gchar *participant);

/**
 * Sets the input volume of all the selected sessions.
 *
 * @param media The media object the sessions are in.
 * @param session_id The session to select (if any).
 * @param level The level to set the volume to.
 */
void purple_media_set_input_volume(PurpleMedia *media, const gchar *session_id, double level);

/**
 * Sets the output volume of all the selected streams.
 *
 * @param media The media object the streams are in.
 * @param session_id The session to limit the streams to (if any).
 * @param participant The participant to limit the streams to (if any).
 * @param level The level to set the volume to.
 */
void purple_media_set_output_volume(PurpleMedia *media, const gchar *session_id,
		const gchar *participant, double level);

/**
 * Sets a video output window for the given session/stream.
 *
 * @param media The media instance to set the output window on.
 * @param session_id The session to set the output window on.
 * @param participant Optionally, the participant to set the output window on.
 * @param window_id The window id use for embedding the video in.
 *
 * @return An id to reference the output window.
 */
gulong purple_media_set_output_window(PurpleMedia *media,
		const gchar *session_id, const gchar *participant,
		gulong window_id);

/**
 * Removes all output windows from a given media session.
 *
 * @param media The instance to remove all output windows from.
 */
void purple_media_remove_output_windows(PurpleMedia *media);

G_END_DECLS

#endif  /* _PURPLE_MEDIA_H_ */
