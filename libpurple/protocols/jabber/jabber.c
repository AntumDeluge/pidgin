/*
 * purple - Jabber Protocol Plugin
 *
 * Copyright (C) 2003, Nathan Walp <faceprint@faceprint.com>
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
 *
 */
#include "internal.h"

#include "account.h"
#include "accountopt.h"
#include "blist.h"
#include "cmds.h"
#include "connection.h"
#include "conversation.h"
#include "debug.h"
#include "dnssrv.h"
#include "message.h"
#include "notify.h"
#include "pluginpref.h"
#include "privacy.h"
#include "proxy.h"
#include "prpl.h"
#include "request.h"
#include "server.h"
#include "util.h"
#include "version.h"
#include "xmlnode.h"

#include "auth.h"
#include "buddy.h"
#include "caps.h"
#include "chat.h"
#include "data.h"
#include "disco.h"
#include "google.h"
#include "iq.h"
#include "jutil.h"
#include "message.h"
#include "parser.h"
#include "presence.h"
#include "jabber.h"
#include "roster.h"
#include "ping.h"
#include "si.h"
#include "xdata.h"
#include "pep.h"
#include "adhoccommands.h"

#include "jingle/jingle.h"
#include "jingle/rtp.h"

#define JABBER_CONNECT_STEPS (js->gsc ? 9 : 5)

static PurplePlugin *my_protocol = NULL;
GList *jabber_features = NULL;
GList *jabber_identities = NULL;

static void jabber_unregister_account_cb(JabberStream *js);
static void try_srv_connect(JabberStream *js);

static void jabber_stream_init(JabberStream *js)
{
	char *open_stream;

	open_stream = g_strdup_printf("<stream:stream to='%s' "
				          "xmlns='jabber:client' "
						  "xmlns:stream='http://etherx.jabber.org/streams' "
						  "version='1.0'>",
						  js->user->domain);
	/* setup the parser fresh for each stream */
	jabber_parser_setup(js);
	jabber_send_raw(js, open_stream, -1);
	js->reinit = FALSE;
	g_free(open_stream);
}

static void
jabber_session_initialized_cb(JabberStream *js, xmlnode *packet, gpointer data)
{
	const char *type = xmlnode_get_attrib(packet, "type");
	if(type && !strcmp(type, "result")) {
		jabber_disco_items_server(js);
		if(js->unregistration)
			jabber_unregister_account_cb(js);
	} else {
		purple_connection_error_reason (js->gc,
			PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
			("Error initializing session"));
	}
}

static void jabber_session_init(JabberStream *js)
{
	JabberIq *iq = jabber_iq_new(js, JABBER_IQ_SET);
	xmlnode *session;

	jabber_iq_set_callback(iq, jabber_session_initialized_cb, NULL);

	session = xmlnode_new_child(iq->node, "session");
	xmlnode_set_namespace(session, "urn:ietf:params:xml:ns:xmpp-session");

	jabber_iq_send(iq);
}

static void jabber_bind_result_cb(JabberStream *js, xmlnode *packet,
		gpointer data)
{
	const char *type = xmlnode_get_attrib(packet, "type");
	xmlnode *bind;

	if(type && !strcmp(type, "result") &&
			(bind = xmlnode_get_child_with_namespace(packet, "bind", "urn:ietf:params:xml:ns:xmpp-bind"))) {
		xmlnode *jid;
		char *full_jid;
		if((jid = xmlnode_get_child(bind, "jid")) && (full_jid = xmlnode_get_data(jid))) {
			JabberBuddy *my_jb = NULL;
			jabber_id_free(js->user);
			if(!(js->user = jabber_id_new(full_jid))) {
				purple_connection_error_reason (js->gc,
					PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
					_("Invalid response from server."));
			}
			if((my_jb = jabber_buddy_find(js, full_jid, TRUE)))
				my_jb->subscription |= JABBER_SUB_BOTH;

			purple_connection_set_display_name(js->gc, full_jid);

			g_free(full_jid);
		}
	} else {
		PurpleConnectionError reason = PURPLE_CONNECTION_ERROR_NETWORK_ERROR;
		char *msg = jabber_parse_error(js, packet, &reason);
		purple_connection_error_reason (js->gc, reason, msg);
		g_free(msg);
	}

	jabber_session_init(js);
}

static char *jabber_prep_resource(char *input) {
	char hostname[256], /* current hostname */
		 *dot = NULL;

	/* Empty resource == don't send any */
	if (input == NULL || *input == '\0')
		return NULL;

	if (strstr(input, "__HOSTNAME__") == NULL)
		return g_strdup(input);

	/* Replace __HOSTNAME__ with hostname */
	if (gethostname(hostname, sizeof(hostname) - 1)) {
		purple_debug_warning("jabber", "gethostname: %s\n", g_strerror(errno));
		/* according to glibc doc, the only time an error is returned
		   is if the hostname is longer than the buffer, in which case
		   glibc 2.2+ would still fill the buffer with partial
		   hostname, so maybe we want to detect that and use it
		   instead
		*/
		strcpy(hostname, "localhost");
	}
	hostname[sizeof(hostname) - 1] = '\0';

	/* We want only the short hostname, not the FQDN - this will prevent the
	 * resource string from being unreasonably long on systems which stuff the
	 * whole FQDN in the hostname */
	if((dot = strchr(hostname, '.')))
			dot = '\0';

	return purple_strreplace(input, "__HOSTNAME__", hostname);
}

void jabber_stream_features_parse(JabberStream *js, xmlnode *packet)
{
	if(xmlnode_get_child(packet, "starttls")) {
		if(jabber_process_starttls(js, packet))

			return;
	} else if(purple_account_get_bool(js->gc->account, "require_tls", FALSE) && !jabber_stream_is_ssl(js)) {
		purple_connection_error_reason (js->gc,
			 PURPLE_CONNECTION_ERROR_ENCRYPTION_ERROR,
			_("You require encryption, but it is not available on this server."));
		return;
	}

	if(js->registration) {
		jabber_register_start(js);
	} else if(xmlnode_get_child(packet, "mechanisms")) {
		jabber_auth_start(js, packet);
	} else if(xmlnode_get_child(packet, "bind")) {
		xmlnode *bind, *resource;
		char *requested_resource;
		JabberIq *iq = jabber_iq_new(js, JABBER_IQ_SET);
		bind = xmlnode_new_child(iq->node, "bind");
		xmlnode_set_namespace(bind, "urn:ietf:params:xml:ns:xmpp-bind");
		requested_resource = jabber_prep_resource(js->user->resource);

		if (requested_resource != NULL) {
			resource = xmlnode_new_child(bind, "resource");
			xmlnode_insert_data(resource, requested_resource, -1);
			g_free(requested_resource);
		}

		jabber_iq_set_callback(iq, jabber_bind_result_cb, NULL);

		jabber_iq_send(iq);
	} else /* if(xmlnode_get_child_with_namespace(packet, "auth")) */ {
		/* If we get an empty stream:features packet, or we explicitly get
		 * an auth feature with namespace http://jabber.org/features/iq-auth
		 * we should revert back to iq:auth authentication, even though we're
		 * connecting to an XMPP server.  */
		js->auth_type = JABBER_AUTH_IQ_AUTH;
		jabber_stream_set_state(js, JABBER_STREAM_AUTHENTICATING);
	}
}

static void jabber_stream_handle_error(JabberStream *js, xmlnode *packet)
{
	PurpleConnectionError reason = PURPLE_CONNECTION_ERROR_NETWORK_ERROR;
	char *msg = jabber_parse_error(js, packet, &reason);

	purple_connection_error_reason (js->gc, reason, msg);

	g_free(msg);
}

static void tls_init(JabberStream *js);

void jabber_process_packet(JabberStream *js, xmlnode **packet)
{
	const char *xmlns;

	purple_signal_emit(my_protocol, "jabber-receiving-xmlnode", js->gc, packet);

	/* if the signal leaves us with a null packet, we're done */
	if(NULL == *packet)
		return;

	xmlns = xmlnode_get_namespace(*packet);

	if(!strcmp((*packet)->name, "iq")) {
		jabber_iq_parse(js, *packet);
	} else if(!strcmp((*packet)->name, "presence")) {
		jabber_presence_parse(js, *packet);
	} else if(!strcmp((*packet)->name, "message")) {
		jabber_message_parse(js, *packet);
	} else if(!strcmp((*packet)->name, "stream:features")) {
		jabber_stream_features_parse(js, *packet);
	} else if (!strcmp((*packet)->name, "features") && xmlns &&
		   !strcmp(xmlns, "http://etherx.jabber.org/streams")) {
		jabber_stream_features_parse(js, *packet);
	} else if(!strcmp((*packet)->name, "stream:error") ||
			 (!strcmp((*packet)->name, "error") && xmlns &&
				!strcmp(xmlns, "http://etherx.jabber.org/streams")))
	{
		jabber_stream_handle_error(js, *packet);
	} else if(!strcmp((*packet)->name, "challenge")) {
		if(js->state == JABBER_STREAM_AUTHENTICATING)
			jabber_auth_handle_challenge(js, *packet);
	} else if(!strcmp((*packet)->name, "success")) {
		if(js->state == JABBER_STREAM_AUTHENTICATING)
			jabber_auth_handle_success(js, *packet);
	} else if(!strcmp((*packet)->name, "failure")) {
		if(js->state == JABBER_STREAM_AUTHENTICATING)
			jabber_auth_handle_failure(js, *packet);
	} else if(!strcmp((*packet)->name, "proceed")) {
		if(js->state == JABBER_STREAM_AUTHENTICATING && !js->gsc)
			tls_init(js);
	} else {
		purple_debug(PURPLE_DEBUG_WARNING, "jabber", "Unknown packet: %s\n",
				(*packet)->name);
	}
}

static int jabber_do_send(JabberStream *js, const char *data, int len)
{
	int ret;

	if (js->gsc)
		ret = purple_ssl_write(js->gsc, data, len);
	else
		ret = write(js->fd, data, len);

	return ret;
}

static void jabber_send_cb(gpointer data, gint source, PurpleInputCondition cond)
{
	JabberStream *js = data;
	int ret, writelen;
	writelen = purple_circ_buffer_get_max_read(js->write_buffer);

	if (writelen == 0) {
		purple_input_remove(js->writeh);
		js->writeh = 0;
		return;
	}

	ret = jabber_do_send(js, js->write_buffer->outptr, writelen);

	if (ret < 0 && errno == EAGAIN)
		return;
	else if (ret <= 0) {
		purple_connection_error_reason (js->gc,
			PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
			_("Write error"));
		return;
	}

	purple_circ_buffer_mark_read(js->write_buffer, ret);
}

static gboolean do_jabber_send_raw(JabberStream *js, const char *data, int len)
{
	int ret;
	gboolean success = TRUE;

	if (len == -1)
		len = strlen(data);

	if (js->writeh == 0)
		ret = jabber_do_send(js, data, len);
	else {
		ret = -1;
		errno = EAGAIN;
	}

	if (ret < 0 && errno != EAGAIN) {
		purple_connection_error_reason (js->gc,
			PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
			_("Write error"));
		success = FALSE;
	} else if (ret < len) {
		if (ret < 0)
			ret = 0;
		if (js->writeh == 0)
			js->writeh = purple_input_add(
				js->gsc ? js->gsc->fd : js->fd,
				PURPLE_INPUT_WRITE, jabber_send_cb, js);
		purple_circ_buffer_append(js->write_buffer,
			data + ret, len - ret);
	}

	return success;
}

void jabber_send_raw(JabberStream *js, const char *data, int len)
{

	/* because printing a tab to debug every minute gets old */
	if(strcmp(data, "\t")) {
		char *text = NULL, *last_part = NULL, *tag_start = NULL;

		/* Because debug logs with plaintext passwords make me sad */
		if(js->state != JABBER_STREAM_CONNECTED &&
				/* Either <auth> or <query><password>... */
				(((tag_start = strstr(data, "<auth ")) &&
					strstr(data, "xmlns='urn:ietf:params:xml:ns:xmpp-sasl'")) ||
				((tag_start = strstr(data, "<query ")) &&
					strstr(data, "xmlns='jabber:iq:auth'>") &&
					(tag_start = strstr(tag_start, "<password>"))))) {
			char *data_start, *tag_end = strchr(tag_start, '>');
			text = g_strdup(data);

			data_start = text + (tag_end - data) + 1;

			last_part = strchr(data_start, '<');
			*data_start = '\0';
		}

		purple_debug(PURPLE_DEBUG_MISC, "jabber", "Sending%s: %s%s%s\n",
				js->gsc ? " (ssl)" : "", text ? text : data,
				last_part ? "password removed" : "",
				last_part ? last_part : "");

		g_free(text);
	}

	/* If we've got a security layer, we need to encode the data,
	 * splitting it on the maximum buffer length negotiated */

	purple_signal_emit(my_protocol, "jabber-sending-text", js->gc, &data);
	if (data == NULL)
		return;

#ifdef HAVE_CYRUS_SASL
	if (js->sasl_maxbuf>0) {
		int pos = 0;

		if (!js->gsc && js->fd<0)
			return;

		if (len == -1)
			len = strlen(data);

		while (pos < len) {
			int towrite;
			const char *out;
			unsigned olen;

			towrite = MIN((len - pos), js->sasl_maxbuf);

			sasl_encode(js->sasl, &data[pos], towrite, &out, &olen);
			pos += towrite;

			if (!do_jabber_send_raw(js, out, olen))
				break;
		}
		return;
	}
#endif

	if (len == -1)
		len = strlen(data);

	if (js->use_bosh)
		jabber_bosh_connection_send_raw(js->bosh, data);
	else
		do_jabber_send_raw(js, data, len);
}

int jabber_prpl_send_raw(PurpleConnection *gc, const char *buf, int len)
{
	JabberStream *js = (JabberStream*)gc->proto_data;
	jabber_send_raw(js, buf, len);
	return len;
}

void jabber_send(JabberStream *js, xmlnode *packet)
{
	char *txt;
	int len;

	purple_signal_emit(my_protocol, "jabber-sending-xmlnode", js->gc, &packet);

	/* if we get NULL back, we're done processing */
	if(NULL == packet)
		return;

	txt = xmlnode_to_str(packet, &len);
	jabber_send_raw(js, txt, len);
	g_free(txt);
}

static void jabber_pong_cb(JabberStream *js, xmlnode *packet, gpointer unused)
{
	purple_timeout_remove(js->keepalive_timeout);
	js->keepalive_timeout = -1;
}

static gboolean jabber_pong_timeout(PurpleConnection *gc)
{
	JabberStream *js = gc->proto_data;
	purple_connection_error_reason(gc, PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
					_("Ping timeout"));
	js->keepalive_timeout = -1;
	return FALSE;
}

void jabber_keepalive(PurpleConnection *gc)
{
	JabberStream *js = gc->proto_data;

	if (js->keepalive_timeout == -1) {
		JabberIq *iq = jabber_iq_new(js, JABBER_IQ_GET);

		xmlnode *ping = xmlnode_new_child(iq->node, "ping");
		xmlnode_set_namespace(ping, "urn:xmpp:ping");

		js->keepalive_timeout = purple_timeout_add_seconds(120, (GSourceFunc)(jabber_pong_timeout), gc);
		jabber_iq_set_callback(iq, jabber_pong_cb, NULL);
		jabber_iq_send(iq);
	}
}

static void
jabber_recv_cb_ssl(gpointer data, PurpleSslConnection *gsc,
		PurpleInputCondition cond)
{
	PurpleConnection *gc = data;
	JabberStream *js = gc->proto_data;
	int len;
	static char buf[4096];

	/* TODO: It should be possible to make this check unnecessary */
	if(!PURPLE_CONNECTION_IS_VALID(gc)) {
		purple_ssl_close(gsc);
		return;
	}

	while((len = purple_ssl_read(gsc, buf, sizeof(buf) - 1)) > 0) {
		gc->last_received = time(NULL);
		buf[len] = '\0';
		purple_debug(PURPLE_DEBUG_INFO, "jabber", "Recv (ssl)(%d): %s\n", len, buf);
		jabber_parser_process(js, buf, len);
		if(js->reinit)
			jabber_stream_init(js);
	}

	if(len < 0 && errno == EAGAIN)
		return;
	else {
		if (len == 0)
			purple_debug_info("jabber", "Server closed the connection.\n");
		else
			purple_debug_info("jabber", "Disconnected: %s\n", g_strerror(errno));
		purple_connection_error_reason (js->gc,
			PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
			_("Read Error"));
	}
}

static void
jabber_recv_cb(gpointer data, gint source, PurpleInputCondition condition)
{
	PurpleConnection *gc = data;
	JabberStream *js = gc->proto_data;
	int len;
	static char buf[4096];

	if(!PURPLE_CONNECTION_IS_VALID(gc))
		return;

	if((len = read(js->fd, buf, sizeof(buf) - 1)) > 0) {
		gc->last_received = time(NULL);
#ifdef HAVE_CYRUS_SASL
		if (js->sasl_maxbuf>0) {
			const char *out;
			unsigned int olen;
			sasl_decode(js->sasl, buf, len, &out, &olen);
			if (olen>0) {
				purple_debug(PURPLE_DEBUG_INFO, "jabber", "RecvSASL (%u): %s\n", olen, out);
				jabber_parser_process(js,out,olen);
				if(js->reinit)
					jabber_stream_init(js);
			}
			return;
		}
#endif
		buf[len] = '\0';
		purple_debug(PURPLE_DEBUG_INFO, "jabber", "Recv (%d): %s\n", len, buf);
		jabber_parser_process(js, buf, len);
		if(js->reinit)
			jabber_stream_init(js);
	} else if(len < 0 && errno == EAGAIN) {
		return;
	} else {
		if (len == 0)
			purple_debug_info("jabber", "Server closed the connection.\n");
		else
			purple_debug_info("jabber", "Disconnected: %s\n", g_strerror(errno));
		purple_connection_error_reason (js->gc,
			PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
			_("Read Error"));
	}
}

static void
jabber_login_callback_ssl(gpointer data, PurpleSslConnection *gsc,
		PurpleInputCondition cond)
{
	PurpleConnection *gc = data;
	JabberStream *js;

	/* TODO: It should be possible to make this check unnecessary */
	if(!PURPLE_CONNECTION_IS_VALID(gc)) {
		purple_ssl_close(gsc);
		return;
	}

	js = gc->proto_data;

	if(js->state == JABBER_STREAM_CONNECTING)
		jabber_send_raw(js, "<?xml version='1.0' ?>", -1);
	jabber_stream_set_state(js, JABBER_STREAM_INITIALIZING);
	purple_ssl_input_add(gsc, jabber_recv_cb_ssl, gc);

	/* Tell the app that we're doing encryption */
	jabber_stream_set_state(js, JABBER_STREAM_INITIALIZING_ENCRYPTION);
}

static void
jabber_bosh_login_callback(PurpleBOSHConnection *conn) 
{
	purple_debug_info("jabber","YAY...BOSH connection established.\n");
}

static void 
txt_resolved_cb(PurpleTxtResponse *resp, int results, gpointer data)
{
	JabberStream *js = data;
	int n;
	
	js->srv_query_data = NULL;

	if (results == 0) {
		gchar *tmp;
		tmp = g_strdup_printf(_("Could not find alternative XMPP connection methods after failing to connect directly.\n"));
		purple_connection_error_reason (js->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, tmp);
		g_free(tmp);
		return;	
	}
	
	for (n = 0; n < results; n++) {
		gchar **token;
		token = g_strsplit(resp[n].content, "=", 2);
		if (!strcmp(token[0], "_xmpp-client-xbosh")) {
			purple_debug_info("jabber","Found alternative connection method using %s at %s.\n", token[0], token[1]);
			js->bosh = jabber_bosh_connection_init(js, token[1]);
			js->use_bosh = TRUE;
			g_strfreev(token);
			break;
		}
		g_strfreev(token);
	}
	if (js->bosh) {
		jabber_bosh_connection_connect(js->bosh);
	} else {
		purple_debug_info("jabber","Didn't find an alternative connection method.\n");
	}
}

static void
jabber_login_callback(gpointer data, gint source, const gchar *error)
{
	PurpleConnection *gc = data;
	JabberStream *js = gc->proto_data;

	if (source < 0) {
		if (js->srv_rec != NULL) {
			purple_debug_error("jabber", "Unable to connect to server: %s.  Trying next SRV record.\n", error);
			try_srv_connect(js);
		} else {
			purple_debug_info("jabber","Couldn't connect directly to %s. Trying to find alternative connection methods, like BOSH.\n", js->user->domain);
			js->srv_query_data = purple_txt_resolve("_xmppconnect", js->user->domain, txt_resolved_cb, js);
		}
		return;
	}

	g_free(js->srv_rec);
	js->srv_rec = NULL;

	js->fd = source;

	if(js->state == JABBER_STREAM_CONNECTING)
		jabber_send_raw(js, "<?xml version='1.0' ?>", -1);

	jabber_stream_set_state(js, JABBER_STREAM_INITIALIZING);
	gc->inpa = purple_input_add(js->fd, PURPLE_INPUT_READ, jabber_recv_cb, gc);
}

static void
jabber_ssl_connect_failure(PurpleSslConnection *gsc, PurpleSslErrorType error,
		gpointer data)
{
	PurpleConnection *gc = data;
	JabberStream *js;

	/* If the connection is already disconnected, we don't need to do anything else */
	if(!PURPLE_CONNECTION_IS_VALID(gc))
		return;

	js = gc->proto_data;
	js->gsc = NULL;

	purple_connection_ssl_error (gc, error);
}

static void tls_init(JabberStream *js)
{
	purple_input_remove(js->gc->inpa);
	js->gc->inpa = 0;
	js->gsc = purple_ssl_connect_with_host_fd(js->gc->account, js->fd,
			jabber_login_callback_ssl, jabber_ssl_connect_failure, js->certificate_CN, js->gc);
}

static gboolean jabber_login_connect(JabberStream *js, const char *domain, const char *host, int port,
				 gboolean fatal_failure)
{
	/* host should be used in preference to domain to
	 * allow SASL authentication to work with FQDN of the server,
	 * but we use domain as fallback for when users enter IP address
	 * in connect server */
	g_free(js->serverFQDN);
	if (purple_ip_address_is_valid(host))
		js->serverFQDN = g_strdup(domain);
	else
		js->serverFQDN = g_strdup(host);

	if (purple_proxy_connect(js->gc, js->gc->account, host,
			port, jabber_login_callback, js->gc) == NULL) {
		if (fatal_failure) {
			purple_connection_error_reason (js->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
				_("Unable to create socket"));
		}

		return FALSE;
	}

	return TRUE;
}

static void try_srv_connect(JabberStream *js)
{
	while (js->srv_rec != NULL && js->srv_rec_idx < js->max_srv_rec_idx) {
		PurpleSrvResponse *tmp_resp = js->srv_rec + (js->srv_rec_idx++);
		if (jabber_login_connect(js, tmp_resp->hostname, tmp_resp->hostname, tmp_resp->port, FALSE))
			return;
	}

	g_free(js->srv_rec);
	js->srv_rec = NULL;

	/* Fall back to the defaults (I'm not sure if we should actually do this) */
	jabber_login_connect(js, js->user->domain, js->user->domain,
		purple_account_get_int(js->gc->account, "port", 5222), TRUE);
}

static void srv_resolved_cb(PurpleSrvResponse *resp, int results, gpointer data)
{
	JabberStream *js = data;
	js->srv_query_data = NULL;

	if(results) {
		js->srv_rec = resp;
		js->srv_rec_idx = 0;
		js->max_srv_rec_idx = results;
		try_srv_connect(js);
	} else {
		jabber_login_connect(js, js->user->domain, js->user->domain,
			purple_account_get_int(js->gc->account, "port", 5222), TRUE);
	}
}

void
jabber_login(PurpleAccount *account)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	const char *connect_server = purple_account_get_string(account,
			"connect_server", "");
	JabberStream *js;
	JabberBuddy *my_jb = NULL;

	gc->flags |= PURPLE_CONNECTION_HTML |
		PURPLE_CONNECTION_ALLOW_CUSTOM_SMILEY;
	js = gc->proto_data = g_new0(JabberStream, 1);
	js->gc = gc;
	js->fd = -1;
	js->iq_callbacks = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, g_free);
	js->disco_callbacks = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, g_free);
	js->buddies = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, (GDestroyNotify)jabber_buddy_free);
	js->chats = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, (GDestroyNotify)jabber_chat_free);
	js->user = jabber_id_new(purple_account_get_username(account));
	js->next_id = g_random_int();
	js->write_buffer = purple_circ_buffer_new(512);
	js->old_length = 0;
	js->keepalive_timeout = -1;
	/* Set the default protocol version to 1.0. Overridden in parser.c. */
	js->protocol_version = JABBER_PROTO_1_0;
	js->sessions = NULL;
	js->stun_ip = NULL;
	js->stun_port = 0;
	js->stun_query = NULL;

	if(!js->user) {
		purple_connection_error_reason (gc,
			PURPLE_CONNECTION_ERROR_INVALID_SETTINGS,
			_("Invalid XMPP ID"));
		return;
	}

	if (!js->user->domain || *(js->user->domain) == '\0') {
		purple_connection_error_reason (gc,
			PURPLE_CONNECTION_ERROR_INVALID_SETTINGS,
			_("Invalid XMPP ID. Domain must be set."));
		return;
	}

	if((my_jb = jabber_buddy_find(js, purple_account_get_username(account), TRUE)))
		my_jb->subscription |= JABBER_SUB_BOTH;

	jabber_stream_set_state(js, JABBER_STREAM_CONNECTING);

	/* TODO: Just use purple_url_parse? */
	if (!g_ascii_strncasecmp(connect_server, "http://", 7) || !g_ascii_strncasecmp(connect_server, "https://", 8)) {
		js->use_bosh = TRUE;
		js->bosh = jabber_bosh_connection_init(js, connect_server);
		if (!js->bosh) {
			purple_connection_error_reason (js->gc,
				PURPLE_CONNECTION_ERROR_INVALID_SETTINGS,
				_("Malformed BOSH Connect Server"));
			return;
		}
		jabber_bosh_connection_connect(js->bosh);
		return;
	} else {
		js->certificate_CN = g_strdup(connect_server[0] ? connect_server : js->user->domain);
	}

	/* if they've got old-ssl mode going, we probably want to ignore SRV lookups */
	if(purple_account_get_bool(js->gc->account, "old_ssl", FALSE)) {
		if(purple_ssl_is_supported()) {
			js->gsc = purple_ssl_connect(js->gc->account,
					js->certificate_CN,
					purple_account_get_int(account, "port", 5223), jabber_login_callback_ssl,
					jabber_ssl_connect_failure, js->gc);
			if (!js->gsc) {
				purple_connection_error_reason (js->gc,
					PURPLE_CONNECTION_ERROR_NO_SSL_SUPPORT,
					_("Unable to establish SSL connection"));
			}
		} else {
			purple_connection_error_reason (js->gc,
				PURPLE_CONNECTION_ERROR_NO_SSL_SUPPORT,
				_("SSL support unavailable"));
		}

		return;
	}

	/* no old-ssl, so if they've specified a connect server, we'll use that, otherwise we'll
	 * invoke the magic of SRV lookups, to figure out host and port */
	if(connect_server[0]) {
		jabber_login_connect(js, js->user->domain, connect_server, purple_account_get_int(account, "port", 5222), TRUE);
	} else {
		js->srv_query_data = purple_srv_resolve("xmpp-client",
				"tcp", js->user->domain, srv_resolved_cb, js);
	}
}


static gboolean
conn_close_cb(gpointer data)
{
	JabberStream *js = data;
	PurpleAccount *account = purple_connection_get_account(js->gc);

	jabber_parser_free(js);

	purple_account_disconnect(account);

	return FALSE;
}

static void
jabber_connection_schedule_close(JabberStream *js)
{
	purple_timeout_add(0, conn_close_cb, js);
}

static void
jabber_registration_result_cb(JabberStream *js, xmlnode *packet, gpointer data)
{
	PurpleAccount *account = purple_connection_get_account(js->gc);
	const char *type = xmlnode_get_attrib(packet, "type");
	char *buf;
	char *to = data;

	if(!strcmp(type, "result")) {
		if(js->registration) {
		buf = g_strdup_printf(_("Registration of %s@%s successful"),
				js->user->node, js->user->domain);
			if(account->registration_cb)
				(account->registration_cb)(account, TRUE, account->registration_cb_user_data);
		} else {
			g_return_if_fail(to != NULL);
			buf = g_strdup_printf(_("Registration to %s successful"),
				to);
		}
		purple_notify_info(NULL, _("Registration Successful"),
				_("Registration Successful"), buf);
		g_free(buf);
	} else {
		char *msg = jabber_parse_error(js, packet, NULL);

		if(!msg)
			msg = g_strdup(_("Unknown Error"));

		purple_notify_error(NULL, _("Registration Failed"),
				_("Registration Failed"), msg);
		g_free(msg);
		if(account->registration_cb)
			(account->registration_cb)(account, FALSE, account->registration_cb_user_data);
	}
	g_free(to);
	if(js->registration)
	jabber_connection_schedule_close(js);
}

static void
jabber_unregistration_result_cb(JabberStream *js, xmlnode *packet, gpointer data)
{
	const char *type = xmlnode_get_attrib(packet, "type");
	char *buf;
	char *to = data;

	/* This function is never called for unregistering our XMPP account from
	 * the server, so there should always be a 'to' address. */
	g_return_if_fail(to != NULL);

	if(!strcmp(type, "result")) {
		buf = g_strdup_printf(_("Registration from %s successfully removed"),
							  to);
		purple_notify_info(NULL, _("Unregistration Successful"),
						   _("Unregistration Successful"), buf);
		g_free(buf);
	} else {
		char *msg = jabber_parse_error(js, packet, NULL);

		if(!msg)
			msg = g_strdup(_("Unknown Error"));

		purple_notify_error(NULL, _("Unregistration Failed"),
							_("Unregistration Failed"), msg);
		g_free(msg);
	}
	g_free(to);
}

typedef struct _JabberRegisterCBData {
	JabberStream *js;
	char *who;
} JabberRegisterCBData;

static void
jabber_register_cb(JabberRegisterCBData *cbdata, PurpleRequestFields *fields)
{
	GList *groups, *flds;
	xmlnode *query, *y;
	JabberIq *iq;
	char *username;

	iq = jabber_iq_new_query(cbdata->js, JABBER_IQ_SET, "jabber:iq:register");
	query = xmlnode_get_child(iq->node, "query");
	if (cbdata->who)
		xmlnode_set_attrib(iq->node, "to", cbdata->who);

	for(groups = purple_request_fields_get_groups(fields); groups;
			groups = groups->next) {
		for(flds = purple_request_field_group_get_fields(groups->data);
				flds; flds = flds->next) {
			PurpleRequestField *field = flds->data;
			const char *id = purple_request_field_get_id(field);
			if(!strcmp(id,"unregister")) {
				gboolean value = purple_request_field_bool_get_value(field);
				if(value) {
					/* unregister from service. this doesn't include any of the fields, so remove them from the stanza by recreating it
					   (there's no "remove child" function for xmlnode) */
					jabber_iq_free(iq);
					iq = jabber_iq_new_query(cbdata->js, JABBER_IQ_SET, "jabber:iq:register");
					query = xmlnode_get_child(iq->node, "query");
					if (cbdata->who)
						xmlnode_set_attrib(iq->node,"to",cbdata->who);
					xmlnode_new_child(query, "remove");

					jabber_iq_set_callback(iq, jabber_unregistration_result_cb, cbdata->who);

					jabber_iq_send(iq);
					g_free(cbdata);
					return;
				}
			} else {
			const char *value = purple_request_field_string_get_value(field);

			if(!strcmp(id, "username")) {
				y = xmlnode_new_child(query, "username");
			} else if(!strcmp(id, "password")) {
				y = xmlnode_new_child(query, "password");
			} else if(!strcmp(id, "name")) {
				y = xmlnode_new_child(query, "name");
			} else if(!strcmp(id, "email")) {
				y = xmlnode_new_child(query, "email");
			} else if(!strcmp(id, "nick")) {
				y = xmlnode_new_child(query, "nick");
			} else if(!strcmp(id, "first")) {
				y = xmlnode_new_child(query, "first");
			} else if(!strcmp(id, "last")) {
				y = xmlnode_new_child(query, "last");
			} else if(!strcmp(id, "address")) {
				y = xmlnode_new_child(query, "address");
			} else if(!strcmp(id, "city")) {
				y = xmlnode_new_child(query, "city");
			} else if(!strcmp(id, "state")) {
				y = xmlnode_new_child(query, "state");
			} else if(!strcmp(id, "zip")) {
				y = xmlnode_new_child(query, "zip");
			} else if(!strcmp(id, "phone")) {
				y = xmlnode_new_child(query, "phone");
			} else if(!strcmp(id, "url")) {
				y = xmlnode_new_child(query, "url");
			} else if(!strcmp(id, "date")) {
				y = xmlnode_new_child(query, "date");
			} else {
				continue;
			}
			xmlnode_insert_data(y, value, -1);
				if(cbdata->js->registration && !strcmp(id, "username")) {
					g_free(cbdata->js->user->node);
					cbdata->js->user->node = g_strdup(value);
			}
				if(cbdata->js->registration && !strcmp(id, "password"))
					purple_account_set_password(cbdata->js->gc->account, value);
		}
	}
	}

	if(cbdata->js->registration) {
		username = g_strdup_printf("%s@%s/%s", cbdata->js->user->node, cbdata->js->user->domain,
				cbdata->js->user->resource);
		purple_account_set_username(cbdata->js->gc->account, username);
		g_free(username);
	}

	jabber_iq_set_callback(iq, jabber_registration_result_cb, cbdata->who);

	jabber_iq_send(iq);
	g_free(cbdata);
}

static void
jabber_register_cancel_cb(JabberRegisterCBData *cbdata, PurpleRequestFields *fields)
{
	PurpleAccount *account = purple_connection_get_account(cbdata->js->gc);
	if(account && cbdata->js->registration) {
		if(account->registration_cb)
			(account->registration_cb)(account, FALSE, account->registration_cb_user_data);
		jabber_connection_schedule_close(cbdata->js);
	}
	g_free(cbdata->who);
	g_free(cbdata);
}

static void jabber_register_x_data_cb(JabberStream *js, xmlnode *result, gpointer data)
{
	xmlnode *query;
	JabberIq *iq;
	char *to = data;

	iq = jabber_iq_new_query(js, JABBER_IQ_SET, "jabber:iq:register");
	query = xmlnode_get_child(iq->node, "query");
	if (to)
		xmlnode_set_attrib(iq->node,"to",to);

	xmlnode_insert_child(query, result);

	jabber_iq_set_callback(iq, jabber_registration_result_cb, to);
	jabber_iq_send(iq);
}

void jabber_register_parse(JabberStream *js, xmlnode *packet)
{
	PurpleAccount *account = purple_connection_get_account(js->gc);
	const char *type;
	const char *from;
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;
	xmlnode *query, *x, *y;
	char *instructions;
	JabberRegisterCBData *cbdata;
	gboolean registered = FALSE;

	if(!(type = xmlnode_get_attrib(packet, "type")) || strcmp(type, "result"))
		return;

	from = xmlnode_get_attrib(packet, "from");

	if(js->registration) {
		/* get rid of the login thingy */
		purple_connection_set_state(js->gc, PURPLE_CONNECTED);
	}

	query = xmlnode_get_child(packet, "query");

	if(xmlnode_get_child(query, "registered")) {
		registered = TRUE;

		if(js->registration) {
			purple_notify_error(NULL, _("Already Registered"),
								_("Already Registered"), NULL);
			if(account->registration_cb)
				(account->registration_cb)(account, FALSE, account->registration_cb_user_data);
			jabber_connection_schedule_close(js);
			return;
		}
	}

	if((x = xmlnode_get_child_with_namespace(query, "x", "jabber:x:data"))) {
		jabber_x_data_request(js, x, jabber_register_x_data_cb, g_strdup(from));
		return;

	} else if((x = xmlnode_get_child_with_namespace(query, "x", "jabber:x:oob"))) {
		xmlnode *url;

		if((url = xmlnode_get_child(x, "url"))) {
			char *href;
			if((href = xmlnode_get_data(url))) {
				purple_notify_uri(NULL, href);
				g_free(href);

				if(js->registration) {
					js->gc->wants_to_die = TRUE;
					if(account->registration_cb) /* succeeded, but we have no login info */
						(account->registration_cb)(account, TRUE, account->registration_cb_user_data);
					jabber_connection_schedule_close(js);
				}
				return;
			}
		}
	}

	/* as a last resort, use the old jabber:iq:register syntax */

	fields = purple_request_fields_new();
	group = purple_request_field_group_new(NULL);
	purple_request_fields_add_group(fields, group);

	if(js->registration)
		field = purple_request_field_string_new("username", _("Username"), js->user->node, FALSE);
	else
		field = purple_request_field_string_new("username", _("Username"), NULL, FALSE);

	purple_request_field_group_add_field(group, field);

	if(js->registration)
		field = purple_request_field_string_new("password", _("Password"),
									purple_connection_get_password(js->gc), FALSE);
	else
		field = purple_request_field_string_new("password", _("Password"), NULL, FALSE);

	purple_request_field_string_set_masked(field, TRUE);
	purple_request_field_group_add_field(group, field);

	if(xmlnode_get_child(query, "name")) {
		if(js->registration)
			field = purple_request_field_string_new("name", _("Name"),
													purple_account_get_alias(js->gc->account), FALSE);
		else
			field = purple_request_field_string_new("name", _("Name"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "email")) {
		field = purple_request_field_string_new("email", _("Email"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "nick")) {
		field = purple_request_field_string_new("nick", _("Nickname"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "first")) {
		field = purple_request_field_string_new("first", _("First name"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "last")) {
		field = purple_request_field_string_new("last", _("Last name"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "address")) {
		field = purple_request_field_string_new("address", _("Address"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "city")) {
		field = purple_request_field_string_new("city", _("City"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "state")) {
		field = purple_request_field_string_new("state", _("State"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "zip")) {
		field = purple_request_field_string_new("zip", _("Postal code"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "phone")) {
		field = purple_request_field_string_new("phone", _("Phone"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "url")) {
		field = purple_request_field_string_new("url", _("URL"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(xmlnode_get_child(query, "date")) {
		field = purple_request_field_string_new("date", _("Date"), NULL, FALSE);
		purple_request_field_group_add_field(group, field);
	}
	if(registered) {
		field = purple_request_field_bool_new("unregister", _("Unregister"), FALSE);
		purple_request_field_group_add_field(group, field);
	}

	if((y = xmlnode_get_child(query, "instructions")))
		instructions = xmlnode_get_data(y);
	else if(registered)
		instructions = g_strdup(_("Please fill out the information below "
					"to change your account registration."));
	else
		instructions = g_strdup(_("Please fill out the information below "
					"to register your new account."));

	cbdata = g_new0(JabberRegisterCBData, 1);
	cbdata->js = js;
	cbdata->who = g_strdup(from);

	if(js->registration)
		purple_request_fields(js->gc, _("Register New XMPP Account"),
				_("Register New XMPP Account"), instructions, fields,
				_("Register"), G_CALLBACK(jabber_register_cb),
				_("Cancel"), G_CALLBACK(jabber_register_cancel_cb),
				purple_connection_get_account(js->gc), NULL, NULL,
				cbdata);
	else {
		char *title;
		g_return_if_fail(from != NULL);
		title = registered ? g_strdup_printf(_("Change Account Registration at %s"), from)
								:g_strdup_printf(_("Register New Account at %s"), from);
		purple_request_fields(js->gc, title,
			  title, instructions, fields,
			  (registered ? _("Change Registration") : _("Register")), G_CALLBACK(jabber_register_cb),
			  _("Cancel"), G_CALLBACK(jabber_register_cancel_cb),
			  purple_connection_get_account(js->gc), NULL, NULL,
			  cbdata);
		g_free(title);
	}

	g_free(instructions);
}

void jabber_register_start(JabberStream *js)
{
	JabberIq *iq;

	iq = jabber_iq_new_query(js, JABBER_IQ_GET, "jabber:iq:register");
	jabber_iq_send(iq);
}

void jabber_register_gateway(JabberStream *js, const char *gateway) {
	JabberIq *iq;

	iq = jabber_iq_new_query(js, JABBER_IQ_GET, "jabber:iq:register");
	xmlnode_set_attrib(iq->node, "to", gateway);
	jabber_iq_send(iq);
}

void jabber_register_account(PurpleAccount *account)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	JabberStream *js;
	JabberBuddy *my_jb = NULL;
	const char *connect_server = purple_account_get_string(account,
			"connect_server", "");
	const char *server;

	js = gc->proto_data = g_new0(JabberStream, 1);
	js->gc = gc;
	js->registration = TRUE;
	js->iq_callbacks = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, g_free);
	js->disco_callbacks = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, g_free);
	js->user = jabber_id_new(purple_account_get_username(account));
	js->next_id = g_random_int();
	js->old_length = 0;

	if(!js->user) {
		purple_connection_error_reason (gc,
			PURPLE_CONNECTION_ERROR_INVALID_SETTINGS,
			_("Invalid XMPP ID"));
		return;
	}

	js->write_buffer = purple_circ_buffer_new(512);

	if((my_jb = jabber_buddy_find(js, purple_account_get_username(account), TRUE)))
		my_jb->subscription |= JABBER_SUB_BOTH;

	server = connect_server[0] ? connect_server : js->user->domain;
	js->certificate_CN = g_strdup(server);

	js->stun_ip = NULL;
	js->stun_port = 0;
	js->stun_query = NULL;

	jabber_stream_set_state(js, JABBER_STREAM_CONNECTING);

	if(purple_account_get_bool(account, "old_ssl", FALSE)) {
		if(purple_ssl_is_supported()) {
			js->gsc = purple_ssl_connect(account, server,
					purple_account_get_int(account, "port", 5222),
					jabber_login_callback_ssl, jabber_ssl_connect_failure, gc);
		} else {
			purple_connection_error_reason (gc,
				PURPLE_CONNECTION_ERROR_NO_SSL_SUPPORT,
				_("SSL support unavailable"));
		}
	}

	if(!js->gsc) {
		if (connect_server[0]) {
			jabber_login_connect(js, js->user->domain, server,
			                     purple_account_get_int(account,
			                                          "port", 5222), TRUE);
		} else {
			js->srv_query_data = purple_srv_resolve("xmpp-client",
			                                      "tcp",
			                                      js->user->domain,
			                                      srv_resolved_cb,
			                                      js);
		}
	}
}

static void jabber_unregister_account_iq_cb(JabberStream *js, xmlnode *packet, gpointer data) {
	PurpleAccount *account = purple_connection_get_account(js->gc);
	const char *type = xmlnode_get_attrib(packet,"type");
	if(!strcmp(type,"error")) {
		char *msg = jabber_parse_error(js, packet, NULL);

		purple_notify_error(js->gc, _("Error unregistering account"),
							_("Error unregistering account"), msg);
		g_free(msg);
		if(js->unregistration_cb)
			js->unregistration_cb(account, FALSE, js->unregistration_user_data);
	} else if(!strcmp(type,"result")) {
		purple_notify_info(js->gc, _("Account successfully unregistered"),
						   _("Account successfully unregistered"), NULL);
		if(js->unregistration_cb)
			js->unregistration_cb(account, TRUE, js->unregistration_user_data);
	}
}

static void jabber_unregister_account_cb(JabberStream *js) {
	JabberIq *iq;
	xmlnode *query;

	g_return_if_fail(js->unregistration);

	iq = jabber_iq_new_query(js, JABBER_IQ_SET, "jabber:iq:register");

	query = xmlnode_get_child_with_namespace(iq->node, "query", "jabber:iq:register");

	xmlnode_new_child(query, "remove");
	xmlnode_set_attrib(iq->node, "to", js->user->domain);

	jabber_iq_set_callback(iq, jabber_unregister_account_iq_cb, NULL);
	jabber_iq_send(iq);
}

void jabber_unregister_account(PurpleAccount *account, PurpleAccountUnregistrationCb cb, void *user_data) {
	PurpleConnection *gc = purple_account_get_connection(account);
	JabberStream *js;

	if(gc->state != PURPLE_CONNECTED) {
		if(gc->state != PURPLE_CONNECTING)
			jabber_login(account);
		js = gc->proto_data;
		js->unregistration = TRUE;
		js->unregistration_cb = cb;
		js->unregistration_user_data = user_data;
		return;
	}

	js = gc->proto_data;

	if (js->unregistration) {
		purple_debug_error("jabber", "Unregistration in process; ignoring duplicate request.\n");
		return;
	}

	js->unregistration = TRUE;
	js->unregistration_cb = cb;
	js->unregistration_user_data = user_data;

	jabber_unregister_account_cb(js);
}

void jabber_close(PurpleConnection *gc)
{
	JabberStream *js = gc->proto_data;

	/* Close all of the open Jingle sessions on this stream */
	jingle_terminate_sessions(js);

	/* Don't perform any actions on the ssl connection
	 * if we were forcibly disconnected because it will crash
	 * on some SSL backends.
	 */
	if (!gc->disconnect_timeout) {
		if (js->use_bosh)
			jabber_bosh_connection_close(js->bosh);
		else
			jabber_send_raw(js, "</stream:stream>", -1);
	}

	if (js->srv_query_data)
		purple_srv_cancel(js->srv_query_data);

	if(js->gsc) {
#ifdef HAVE_OPENSSL
		if (!gc->disconnect_timeout)
#endif
			purple_ssl_close(js->gsc);
	} else if (js->fd > 0) {
		if(js->gc->inpa)
			purple_input_remove(js->gc->inpa);
		close(js->fd);
	}

	if (js->bosh)
		jabber_bosh_connection_destroy(js->bosh);

	jabber_buddy_remove_all_pending_buddy_info_requests(js);

	jabber_parser_free(js);

	if(js->iq_callbacks)
		g_hash_table_destroy(js->iq_callbacks);
	if(js->disco_callbacks)
		g_hash_table_destroy(js->disco_callbacks);
	if(js->buddies)
		g_hash_table_destroy(js->buddies);
	if(js->chats)
		g_hash_table_destroy(js->chats);

	while(js->chat_servers) {
		g_free(js->chat_servers->data);
		js->chat_servers = g_list_delete_link(js->chat_servers, js->chat_servers);
	}

	while(js->user_directories) {
		g_free(js->user_directories->data);
		js->user_directories = g_list_delete_link(js->user_directories, js->user_directories);
	}

	while(js->bs_proxies) {
		JabberBytestreamsStreamhost *sh = js->bs_proxies->data;
		g_free(sh->jid);
		g_free(sh->host);
		g_free(sh->zeroconf);
		g_free(sh);
		js->bs_proxies = g_list_delete_link(js->bs_proxies, js->bs_proxies);
	}

	while(js->url_datas) {
		purple_util_fetch_url_cancel(js->url_datas->data);
		js->url_datas = g_slist_delete_link(js->url_datas, js->url_datas);
	}

	g_free(js->stream_id);
	if(js->user)
		jabber_id_free(js->user);
	g_free(js->avatar_hash);
	g_free(js->caps_hash);

	purple_circ_buffer_destroy(js->write_buffer);
	if(js->writeh)
		purple_input_remove(js->writeh);
#ifdef HAVE_CYRUS_SASL
	if(js->sasl)
		sasl_dispose(&js->sasl);
	if(js->sasl_mechs)
		g_string_free(js->sasl_mechs, TRUE);
	g_free(js->sasl_cb);
#endif
	g_free(js->serverFQDN);
	while(js->commands) {
		JabberAdHocCommands *cmd = js->commands->data;
		g_free(cmd->jid);
		g_free(cmd->node);
		g_free(cmd->name);
		g_free(cmd);
		js->commands = g_list_delete_link(js->commands, js->commands);
	}
	g_free(js->server_name);
	g_free(js->certificate_CN);
	g_free(js->gmail_last_time);
	g_free(js->gmail_last_tid);
	g_free(js->old_msg);
	g_free(js->old_avatarhash);
	g_free(js->old_artist);
	g_free(js->old_title);
	g_free(js->old_source);
	g_free(js->old_uri);
	g_free(js->old_track);
	g_free(js->expected_rspauth);

	if (js->keepalive_timeout != -1)
		purple_timeout_remove(js->keepalive_timeout);

	g_free(js->srv_rec);
	js->srv_rec = NULL;

	g_free(js->stun_ip);
	js->stun_ip = NULL;

	/* cancel DNS query for STUN, if one is ongoing */
	if (js->stun_query) {
		purple_dnsquery_destroy(js->stun_query);
		js->stun_query = NULL;
	}
		
	g_free(js);

	gc->proto_data = NULL;
}

void jabber_stream_set_state(JabberStream *js, JabberStreamState state)
{
	js->state = state;
	switch(state) {
		case JABBER_STREAM_OFFLINE:
			break;
		case JABBER_STREAM_CONNECTING:
			purple_connection_update_progress(js->gc, _("Connecting"), 1,
					JABBER_CONNECT_STEPS);
			break;
		case JABBER_STREAM_INITIALIZING:
			purple_connection_update_progress(js->gc, _("Initializing Stream"),
					js->gsc ? 5 : 2, JABBER_CONNECT_STEPS);
			jabber_stream_init(js);
			break;
		case JABBER_STREAM_INITIALIZING_ENCRYPTION:
			purple_connection_update_progress(js->gc, _("Initializing SSL/TLS"),
											  6, JABBER_CONNECT_STEPS);
			break;
		case JABBER_STREAM_AUTHENTICATING:
			purple_connection_update_progress(js->gc, _("Authenticating"),
					js->gsc ? 7 : 3, JABBER_CONNECT_STEPS);
			if(js->protocol_version == JABBER_PROTO_0_9 && js->registration) {
				jabber_register_start(js);
			} else if(js->auth_type == JABBER_AUTH_IQ_AUTH) {
				/* with dreamhost's xmpp server at least, you have to
				   specify a resource or you will get a "406: Not
				   Acceptable"
				*/
				if(!js->user->resource || *js->user->resource == '\0') {
					g_free(js->user->resource);
					js->user->resource = g_strdup("Home");
				}

				jabber_auth_start_old(js);
			}
			break;
		case JABBER_STREAM_REINITIALIZING:
			purple_connection_update_progress(js->gc, _("Re-initializing Stream"),
					(js->gsc ? 8 : 4), JABBER_CONNECT_STEPS);

			/* The stream will be reinitialized later, in jabber_recv_cb_ssl() */
			js->reinit = TRUE;

			break;
		case JABBER_STREAM_CONNECTED:
			/* now we can alert the core that we're ready to send status */
			purple_connection_set_state(js->gc, PURPLE_CONNECTED);
			break;
	}
}

char *jabber_get_next_id(JabberStream *js)
{
	return g_strdup_printf("purple%x", js->next_id++);
}


void jabber_idle_set(PurpleConnection *gc, int idle)
{
	JabberStream *js = gc->proto_data;

	js->idle = idle ? time(NULL) - idle : idle;
}

static void jabber_blocklist_parse(JabberStream *js, xmlnode *packet, gpointer data)
{
	xmlnode *blocklist, *item;
	PurpleAccount *account;

	blocklist = xmlnode_get_child_with_namespace(packet,
			"blocklist", "urn:xmpp:blocking");
	account = purple_connection_get_account(js->gc);

	if (blocklist == NULL)
		return;

	item = xmlnode_get_child(blocklist, "item");
	while (item != NULL) {
		const char *jid = xmlnode_get_attrib(item, "jid");

		purple_privacy_deny_add(account, jid, TRUE);
		item = xmlnode_get_next_twin(item);
	}
}

void jabber_request_block_list(JabberStream *js)
{
	JabberIq *iq;
	xmlnode *blocklist;

	iq = jabber_iq_new(js, JABBER_IQ_GET);

	blocklist = xmlnode_new_child(iq->node, "blocklist");
	xmlnode_set_namespace(blocklist, "urn:xmpp:blocking");

	jabber_iq_set_callback(iq, jabber_blocklist_parse, NULL);

	jabber_iq_send(iq);
}

void jabber_add_deny(PurpleConnection *gc, const char *who)
{
	JabberStream *js;
	JabberIq *iq;
	xmlnode *block, *item;

	js = gc->proto_data;
	if (js == NULL)
		return;

	if (js->server_caps & JABBER_CAP_GOOGLE_ROSTER)
	{
		jabber_google_roster_add_deny(gc, who);
		return;
	}

	if (!(js->server_caps & JABBER_CAP_BLOCKING))
	{
		purple_notify_error(NULL, _("Server doesn't support blocking"),
							_("Server doesn't support blocking"), NULL);
		return;
	}

	iq = jabber_iq_new(js, JABBER_IQ_SET);

	block = xmlnode_new_child(iq->node, "block");
	xmlnode_set_namespace(block, "urn:xmpp:blocking");

	item = xmlnode_new_child(block, "item");
	xmlnode_set_attrib(item, "jid", who);

	jabber_iq_send(iq);
}

void jabber_rem_deny(PurpleConnection *gc, const char *who)
{
	JabberStream *js;
	JabberIq *iq;
	xmlnode *unblock, *item;

	js = gc->proto_data;
	if (js == NULL)
		return;

	if (js->server_caps & JABBER_CAP_GOOGLE_ROSTER)
	{
		jabber_google_roster_rem_deny(gc, who);
		return;
	}

	if (!(js->server_caps & JABBER_CAP_BLOCKING))
		return;

	iq = jabber_iq_new(js, JABBER_IQ_SET);

	unblock = xmlnode_new_child(iq->node, "unblock");
	xmlnode_set_namespace(unblock, "urn:xmpp:blocking");

	item = xmlnode_new_child(unblock, "item");
	xmlnode_set_attrib(item, "jid", who);

	jabber_iq_send(iq);
}

void jabber_add_feature(const char *namespace, JabberFeatureEnabled cb) {
	JabberFeature *feat;

	g_return_if_fail(namespace != NULL);

	feat = g_new0(JabberFeature,1);
	feat->namespace = g_strdup(namespace);
	feat->is_enabled = cb;

	/* try to remove just in case it already exists in the list */
	jabber_remove_feature(namespace);

	jabber_features = g_list_append(jabber_features, feat);
}

void jabber_remove_feature(const char *namespace) {
	GList *feature;
	for(feature = jabber_features; feature; feature = feature->next) {
		JabberFeature *feat = (JabberFeature*)feature->data;
		if(!strcmp(feat->namespace, namespace)) {
			g_free(feat->namespace);
			g_free(feature->data);
			jabber_features = g_list_delete_link(jabber_features, feature);
			break;
		}
	}
}

static void jabber_features_destroy(void)
{
	while (jabber_features) {
		JabberFeature *feature = jabber_features->data;
		g_free(feature->namespace);
		g_free(feature);
		jabber_features = g_list_remove_link(jabber_features, jabber_features);
	}
}

void jabber_add_identity(const gchar *category, const gchar *type, const gchar *lang, const gchar *name) {
	GList *identity;
	JabberIdentity *ident;
	/* both required according to XEP-0030 */
	g_return_if_fail(category != NULL);
	g_return_if_fail(type != NULL);
	
	for(identity = jabber_identities; identity; identity = identity->next) {
		JabberIdentity *ident = (JabberIdentity*)identity->data;
		if (!strcmp(ident->category, category) &&
		    !strcmp(ident->type, type) &&
		    ((!ident->lang && !lang) || (ident->lang && lang && !strcmp(ident->lang, lang)))) {
			return;
		}	
	}

	ident = g_new0(JabberIdentity, 1);
	ident->category = g_strdup(category);
	ident->type = g_strdup(type);
	ident->lang = g_strdup(lang);
	ident->name = g_strdup(name);
	jabber_identities = g_list_append(jabber_identities, ident);
}

static void jabber_identities_destroy(void)
{
	while (jabber_identities) {
		JabberIdentity *id = jabber_identities->data;
		g_free(id->category);
		g_free(id->type);
		g_free(id->lang);
		g_free(id->name);
		g_free(id);
		jabber_identities = g_list_remove_link(jabber_identities, jabber_identities);
	}
}

gboolean jabber_stream_is_ssl(JabberStream *js)
{
	return (js->bosh && jabber_bosh_connection_is_ssl(js->bosh)) ||
	       (!js->bosh && js->gsc);
}

const char *jabber_list_icon(PurpleAccount *a, PurpleBuddy *b)
{
	return "jabber";
}

const char* jabber_list_emblem(PurpleBuddy *b)
{
	JabberStream *js;
	JabberBuddy *jb = NULL;
	PurpleConnection *gc = purple_account_get_connection(purple_buddy_get_account(b));

	if(!gc)
		return NULL;

	js = gc->proto_data;
	if(js)
		jb = jabber_buddy_find(js, purple_buddy_get_name(b), FALSE);

	if(!PURPLE_BUDDY_IS_ONLINE(b)) {
		if(jb && (jb->subscription & JABBER_SUB_PENDING ||
					!(jb->subscription & JABBER_SUB_TO)))
			return "not-authorized";
	}
	return NULL;
}

char *jabber_status_text(PurpleBuddy *b)
{
	char *ret = NULL;
	JabberBuddy *jb = NULL;
	PurpleAccount *account = purple_buddy_get_account(b);
	PurpleConnection *gc = purple_account_get_connection(account);

	if (gc && gc->proto_data)
		jb = jabber_buddy_find(gc->proto_data, purple_buddy_get_name(b), FALSE);

	if(jb && !PURPLE_BUDDY_IS_ONLINE(b) && (jb->subscription & JABBER_SUB_PENDING || !(jb->subscription & JABBER_SUB_TO))) {
		ret = g_strdup(_("Not Authorized"));
	} else if(jb && !PURPLE_BUDDY_IS_ONLINE(b) && jb->error_msg) {
		ret = g_strdup(jb->error_msg);
	} else {
		char *stripped;

		if(!(stripped = purple_markup_strip_html(jabber_buddy_get_status_msg(jb)))) {
			PurplePresence *presence = purple_buddy_get_presence(b);
			if (purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_TUNE)) {
				PurpleStatus *status = purple_presence_get_status(presence, "tune");
				stripped = g_strdup(purple_status_get_attr_string(status, PURPLE_TUNE_TITLE));
			}
		}

		if(stripped) {
			ret = g_markup_escape_text(stripped, -1);
			g_free(stripped);
		}
	}

	return ret;
}

void jabber_tooltip_text(PurpleBuddy *b, PurpleNotifyUserInfo *user_info, gboolean full)
{
	JabberBuddy *jb;
	PurpleAccount *account;
	PurpleConnection *gc;

	g_return_if_fail(b != NULL);

	account = purple_buddy_get_account(b);
	g_return_if_fail(account != NULL);

	gc = purple_account_get_connection(account);
	g_return_if_fail(gc != NULL);
	g_return_if_fail(gc->proto_data != NULL);

	jb = jabber_buddy_find(gc->proto_data, purple_buddy_get_name(b), FALSE);

	if(jb) {
		JabberBuddyResource *jbr = NULL;
		PurplePresence *presence = purple_buddy_get_presence(b);
		const char *sub;
		GList *l;
		const char *mood;

		if (full) {
			PurpleStatus *status;

			if(jb->subscription & JABBER_SUB_FROM) {
				if(jb->subscription & JABBER_SUB_TO)
					sub = _("Both");
				else if(jb->subscription & JABBER_SUB_PENDING)
					sub = _("From (To pending)");
				else
					sub = _("From");
			} else {
				if(jb->subscription & JABBER_SUB_TO)
					sub = _("To");
				else if(jb->subscription & JABBER_SUB_PENDING)
					sub = _("None (To pending)");
				else
					sub = _("None");
			}

			purple_notify_user_info_add_pair(user_info, _("Subscription"), sub);

			status = purple_presence_get_active_status(presence);
			mood = purple_status_get_attr_string(status, "mood");
			if(mood != NULL) {
				const char *moodtext;
				moodtext = purple_status_get_attr_string(status, "moodtext");
				if(moodtext != NULL) {
					char *moodplustext = g_strdup_printf("%s (%s)", mood, moodtext);

					purple_notify_user_info_add_pair(user_info, _("Mood"), moodplustext);
					g_free(moodplustext);
				} else
					purple_notify_user_info_add_pair(user_info, _("Mood"), mood);
			}
			if (purple_presence_is_status_primitive_active(presence, PURPLE_STATUS_TUNE)) {
				PurpleStatus *tune = purple_presence_get_status(presence, "tune");
				const char *title = purple_status_get_attr_string(tune, PURPLE_TUNE_TITLE);
				const char *artist = purple_status_get_attr_string(tune, PURPLE_TUNE_ARTIST);
				const char *album = purple_status_get_attr_string(tune, PURPLE_TUNE_ALBUM);
				char *playing = purple_util_format_song_info(title, artist, album, NULL);
				if (playing) {
					purple_notify_user_info_add_pair(user_info, _("Now Listening"), playing);
					g_free(playing);
				}
			}
		}

		for(l=jb->resources; l; l = l->next) {
			char *text = NULL;
			char *res = NULL;
			char *label, *value;
			const char *state;

			jbr = l->data;

			if(jbr->status) {
				char *tmp;
				text = purple_strreplace(jbr->status, "\n", "<br />\n");
				tmp = purple_markup_strip_html(text);
				g_free(text);
				text = g_markup_escape_text(tmp, -1);
				g_free(tmp);
			}

			if(jbr->name)
				res = g_strdup_printf(" (%s)", jbr->name);

			state = jabber_buddy_state_get_name(jbr->state);
			if (text != NULL && !purple_utf8_strcasecmp(state, text)) {
				g_free(text);
				text = NULL;
			}

			label = g_strdup_printf("%s%s",
							_("Status"), (res ? res : ""));
			value = g_strdup_printf("%s%s%s",
							state,
							(text ? ": " : ""),
							(text ? text : ""));

			purple_notify_user_info_add_pair(user_info, label, value);

			g_free(label);
			g_free(value);
			g_free(text);
			g_free(res);
		}

		if(!PURPLE_BUDDY_IS_ONLINE(b) && jb->error_msg) {
			purple_notify_user_info_add_pair(user_info, _("Error"), jb->error_msg);
		}
	}
}

GList *jabber_status_types(PurpleAccount *account)
{
	PurpleStatusType *type;
	GList *types = NULL;
	PurpleValue *priority_value;
	PurpleValue *buzz_enabled;

	priority_value = purple_value_new(PURPLE_TYPE_INT);
	purple_value_set_int(priority_value, 1);
	buzz_enabled = purple_value_new(PURPLE_TYPE_BOOLEAN);
	purple_value_set_boolean(buzz_enabled, TRUE);
	type = purple_status_type_new_with_attrs(PURPLE_STATUS_AVAILABLE,
			jabber_buddy_state_get_status_id(JABBER_BUDDY_STATE_ONLINE),
			NULL, TRUE, TRUE, FALSE,
			"priority", _("Priority"), priority_value,
			"message", _("Message"), purple_value_new(PURPLE_TYPE_STRING),
			"mood", _("Mood"), purple_value_new(PURPLE_TYPE_STRING),
			"moodtext", _("Mood Text"), purple_value_new(PURPLE_TYPE_STRING),
			"nick", _("Nickname"), purple_value_new(PURPLE_TYPE_STRING),
			"buzz", _("Allow Buzz"), buzz_enabled,
			NULL);
	types = g_list_append(types, type);

	priority_value = purple_value_new(PURPLE_TYPE_INT);
	purple_value_set_int(priority_value, 1);
	buzz_enabled = purple_value_new(PURPLE_TYPE_BOOLEAN);
	purple_value_set_boolean(buzz_enabled, TRUE);
	type = purple_status_type_new_with_attrs(PURPLE_STATUS_AVAILABLE,
			jabber_buddy_state_get_status_id(JABBER_BUDDY_STATE_CHAT),
			_("Chatty"), TRUE, TRUE, FALSE,
			"priority", _("Priority"), priority_value,
			"message", _("Message"), purple_value_new(PURPLE_TYPE_STRING),
			"mood", _("Mood"), purple_value_new(PURPLE_TYPE_STRING),
			"moodtext", _("Mood Text"), purple_value_new(PURPLE_TYPE_STRING),
			"nick", _("Nickname"), purple_value_new(PURPLE_TYPE_STRING),
			"buzz", _("Allow Buzz"), buzz_enabled,
			NULL);
	types = g_list_append(types, type);

	priority_value = purple_value_new(PURPLE_TYPE_INT);
	purple_value_set_int(priority_value, 0);
	buzz_enabled = purple_value_new(PURPLE_TYPE_BOOLEAN);
	purple_value_set_boolean(buzz_enabled, TRUE);
	type = purple_status_type_new_with_attrs(PURPLE_STATUS_AWAY,
			jabber_buddy_state_get_status_id(JABBER_BUDDY_STATE_AWAY),
			NULL, TRUE, TRUE, FALSE,
			"priority", _("Priority"), priority_value,
			"message", _("Message"), purple_value_new(PURPLE_TYPE_STRING),
			"mood", _("Mood"), purple_value_new(PURPLE_TYPE_STRING),
			"moodtext", _("Mood Text"), purple_value_new(PURPLE_TYPE_STRING),
			"nick", _("Nickname"), purple_value_new(PURPLE_TYPE_STRING),
			"buzz", _("Allow Buzz"), buzz_enabled,
			NULL);
	types = g_list_append(types, type);

	priority_value = purple_value_new(PURPLE_TYPE_INT);
	purple_value_set_int(priority_value, 0);
	buzz_enabled = purple_value_new(PURPLE_TYPE_BOOLEAN);
	purple_value_set_boolean(buzz_enabled, TRUE);
	type = purple_status_type_new_with_attrs(PURPLE_STATUS_EXTENDED_AWAY,
			jabber_buddy_state_get_status_id(JABBER_BUDDY_STATE_XA),
			NULL, TRUE, TRUE, FALSE,
			"priority", _("Priority"), priority_value,
			"message", _("Message"), purple_value_new(PURPLE_TYPE_STRING),
			"mood", _("Mood"), purple_value_new(PURPLE_TYPE_STRING),
			"moodtext", _("Mood Text"), purple_value_new(PURPLE_TYPE_STRING),
			"nick", _("Nickname"), purple_value_new(PURPLE_TYPE_STRING),
			"buzz", _("Allow Buzz"), buzz_enabled,
			NULL);
	types = g_list_append(types, type);

	priority_value = purple_value_new(PURPLE_TYPE_INT);
	purple_value_set_int(priority_value, 0);
	type = purple_status_type_new_with_attrs(PURPLE_STATUS_UNAVAILABLE,
			jabber_buddy_state_get_status_id(JABBER_BUDDY_STATE_DND),
			_("Do Not Disturb"), TRUE, TRUE, FALSE,
			"priority", _("Priority"), priority_value,
			"message", _("Message"), purple_value_new(PURPLE_TYPE_STRING),
			"mood", _("Mood"), purple_value_new(PURPLE_TYPE_STRING),
			"moodtext", _("Mood Text"), purple_value_new(PURPLE_TYPE_STRING),
			"nick", _("Nickname"), purple_value_new(PURPLE_TYPE_STRING),
			NULL);
	types = g_list_append(types, type);

	/*
	if(js->protocol_version == JABBER_PROTO_0_9)
		m = g_list_append(m, _("Invisible"));
	*/

	type = purple_status_type_new_with_attrs(PURPLE_STATUS_OFFLINE,
			jabber_buddy_state_get_status_id(JABBER_BUDDY_STATE_UNAVAILABLE),
			NULL, TRUE, TRUE, FALSE,
			"message", _("Message"), purple_value_new(PURPLE_TYPE_STRING),
			NULL);
	types = g_list_append(types, type);

	type = purple_status_type_new_with_attrs(PURPLE_STATUS_TUNE,
			"tune", NULL, FALSE, TRUE, TRUE,
			PURPLE_TUNE_ARTIST, _("Tune Artist"), purple_value_new(PURPLE_TYPE_STRING),
			PURPLE_TUNE_TITLE, _("Tune Title"), purple_value_new(PURPLE_TYPE_STRING),
			PURPLE_TUNE_ALBUM, _("Tune Album"), purple_value_new(PURPLE_TYPE_STRING),
			PURPLE_TUNE_GENRE, _("Tune Genre"), purple_value_new(PURPLE_TYPE_STRING),
			PURPLE_TUNE_COMMENT, _("Tune Comment"), purple_value_new(PURPLE_TYPE_STRING),
			PURPLE_TUNE_TRACK, _("Tune Track"), purple_value_new(PURPLE_TYPE_STRING),
			PURPLE_TUNE_TIME, _("Tune Time"), purple_value_new(PURPLE_TYPE_INT),
			PURPLE_TUNE_YEAR, _("Tune Year"), purple_value_new(PURPLE_TYPE_INT),
			PURPLE_TUNE_URL, _("Tune URL"), purple_value_new(PURPLE_TYPE_STRING),
			NULL);
	types = g_list_append(types, type);

	return types;
}

static void
jabber_password_change_result_cb(JabberStream *js, xmlnode *packet,
		gpointer data)
{
	const char *type;

	type = xmlnode_get_attrib(packet, "type");

	if(type && !strcmp(type, "result")) {
		purple_notify_info(js->gc, _("Password Changed"), _("Password Changed"),
				_("Your password has been changed."));

		purple_account_set_password(js->gc->account, (char *)data);
	} else {
		char *msg = jabber_parse_error(js, packet, NULL);

		purple_notify_error(js->gc, _("Error changing password"),
				_("Error changing password"), msg);
		g_free(msg);
	}

	g_free(data);
}

static void jabber_password_change_cb(JabberStream *js,
		PurpleRequestFields *fields)
{
	const char *p1, *p2;
	JabberIq *iq;
	xmlnode *query, *y;

	p1 = purple_request_fields_get_string(fields, "password1");
	p2 = purple_request_fields_get_string(fields, "password2");

	if(strcmp(p1, p2)) {
		purple_notify_error(js->gc, NULL, _("New passwords do not match."), NULL);
		return;
	}

	iq = jabber_iq_new_query(js, JABBER_IQ_SET, "jabber:iq:register");

	xmlnode_set_attrib(iq->node, "to", js->user->domain);

	query = xmlnode_get_child(iq->node, "query");

	y = xmlnode_new_child(query, "username");
	xmlnode_insert_data(y, js->user->node, -1);
	y = xmlnode_new_child(query, "password");
	xmlnode_insert_data(y, p1, -1);

	jabber_iq_set_callback(iq, jabber_password_change_result_cb, g_strdup(p1));

	jabber_iq_send(iq);
}

static void jabber_password_change(PurplePluginAction *action)
{

	PurpleConnection *gc = (PurpleConnection *) action->context;
	JabberStream *js = gc->proto_data;
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;

	fields = purple_request_fields_new();
	group = purple_request_field_group_new(NULL);
	purple_request_fields_add_group(fields, group);

	field = purple_request_field_string_new("password1", _("Password"),
			"", FALSE);
	purple_request_field_string_set_masked(field, TRUE);
	purple_request_field_set_required(field, TRUE);
	purple_request_field_group_add_field(group, field);

	field = purple_request_field_string_new("password2", _("Password (again)"),
			"", FALSE);
	purple_request_field_string_set_masked(field, TRUE);
	purple_request_field_set_required(field, TRUE);
	purple_request_field_group_add_field(group, field);

	purple_request_fields(js->gc, _("Change XMPP Password"),
			_("Change XMPP Password"), _("Please enter your new password"),
			fields, _("OK"), G_CALLBACK(jabber_password_change_cb),
			_("Cancel"), NULL,
			purple_connection_get_account(gc), NULL, NULL,
			js);
}

GList *jabber_actions(PurplePlugin *plugin, gpointer context)
{
	PurpleConnection *gc = (PurpleConnection *) context;
	JabberStream *js = gc->proto_data;
	GList *m = NULL;
	PurplePluginAction *act;

	act = purple_plugin_action_new(_("Set User Info..."),
	                             jabber_setup_set_info);
	m = g_list_append(m, act);

	/* if (js->protocol_options & CHANGE_PASSWORD) { */
		act = purple_plugin_action_new(_("Change Password..."),
		                             jabber_password_change);
		m = g_list_append(m, act);
	/* } */

	act = purple_plugin_action_new(_("Search for Users..."),
	                             jabber_user_search_begin);
	m = g_list_append(m, act);

	purple_debug_info("jabber", "jabber_actions: have pep: %s\n", js->pep?"YES":"NO");

	if(js->pep)
		jabber_pep_init_actions(&m);

	if(js->commands)
		jabber_adhoc_init_server_commands(js, &m);

	return m;
}

PurpleChat *jabber_find_blist_chat(PurpleAccount *account, const char *name)
{
	PurpleBlistNode *gnode, *cnode;
	JabberID *jid;

	if(!(jid = jabber_id_new(name)))
		return NULL;

	for(gnode = purple_blist_get_root(); gnode;
			gnode = purple_blist_node_get_sibling_next(gnode)) {
		for(cnode = purple_blist_node_get_first_child(gnode);
				cnode;
				cnode = purple_blist_node_get_sibling_next(cnode)) {
			PurpleChat *chat = (PurpleChat*)cnode;
			const char *room, *server;
			GHashTable *components;
			if(!PURPLE_BLIST_NODE_IS_CHAT(cnode))
				continue;

			if (purple_chat_get_account(chat) != account)
				continue;

			components = purple_chat_get_components(chat);
			if(!(room = g_hash_table_lookup(components, "room")))
				continue;
			if(!(server = g_hash_table_lookup(components, "server")))
				continue;

			if(jid->node && jid->domain &&
					!g_utf8_collate(room, jid->node) && !g_utf8_collate(server, jid->domain)) {
				jabber_id_free(jid);
				return chat;
			}
		}
	}
	jabber_id_free(jid);
	return NULL;
}

void jabber_convo_closed(PurpleConnection *gc, const char *who)
{
	JabberStream *js = gc->proto_data;
	JabberID *jid;
	JabberBuddy *jb;
	JabberBuddyResource *jbr;

	if(!(jid = jabber_id_new(who)))
		return;

	if((jb = jabber_buddy_find(js, who, TRUE)) &&
			(jbr = jabber_buddy_find_resource(jb, jid->resource))) {
		if(jbr->thread_id) {
			g_free(jbr->thread_id);
			jbr->thread_id = NULL;
		}
		if(jbr->chat_states == JABBER_CHAT_STATES_SUPPORTED)
			jabber_message_conv_closed(js, who);
	}

	jabber_id_free(jid);
}


char *jabber_parse_error(JabberStream *js,
                         xmlnode *packet,
                         PurpleConnectionError *reason)
{
	xmlnode *error;
	const char *code = NULL, *text = NULL;
	const char *xmlns = xmlnode_get_namespace(packet);
	char *cdata = NULL;

#define SET_REASON(x) \
	if(reason != NULL) { *reason = x; }

	if((error = xmlnode_get_child(packet, "error"))) {
		cdata = xmlnode_get_data(error);
		code = xmlnode_get_attrib(error, "code");

		/* Stanza errors */
		if(xmlnode_get_child(error, "bad-request")) {
			text = _("Bad Request");
		} else if(xmlnode_get_child(error, "conflict")) {
			text = _("Conflict");
		} else if(xmlnode_get_child(error, "feature-not-implemented")) {
			text = _("Feature Not Implemented");
		} else if(xmlnode_get_child(error, "forbidden")) {
			text = _("Forbidden");
		} else if(xmlnode_get_child(error, "gone")) {
			text = _("Gone");
		} else if(xmlnode_get_child(error, "internal-server-error")) {
			text = _("Internal Server Error");
		} else if(xmlnode_get_child(error, "item-not-found")) {
			text = _("Item Not Found");
		} else if(xmlnode_get_child(error, "jid-malformed")) {
			text = _("Malformed XMPP ID");
		} else if(xmlnode_get_child(error, "not-acceptable")) {
			text = _("Not Acceptable");
		} else if(xmlnode_get_child(error, "not-allowed")) {
			text = _("Not Allowed");
		} else if(xmlnode_get_child(error, "not-authorized")) {
			text = _("Not Authorized");
		} else if(xmlnode_get_child(error, "payment-required")) {
			text = _("Payment Required");
		} else if(xmlnode_get_child(error, "recipient-unavailable")) {
			text = _("Recipient Unavailable");
		} else if(xmlnode_get_child(error, "redirect")) {
			/* XXX */
		} else if(xmlnode_get_child(error, "registration-required")) {
			text = _("Registration Required");
		} else if(xmlnode_get_child(error, "remote-server-not-found")) {
			text = _("Remote Server Not Found");
		} else if(xmlnode_get_child(error, "remote-server-timeout")) {
			text = _("Remote Server Timeout");
		} else if(xmlnode_get_child(error, "resource-constraint")) {
			text = _("Server Overloaded");
		} else if(xmlnode_get_child(error, "service-unavailable")) {
			text = _("Service Unavailable");
		} else if(xmlnode_get_child(error, "subscription-required")) {
			text = _("Subscription Required");
		} else if(xmlnode_get_child(error, "unexpected-request")) {
			text = _("Unexpected Request");
		} else if(xmlnode_get_child(error, "undefined-condition")) {
			text = _("Unknown Error");
		}
	} else if(xmlns && !strcmp(xmlns, "urn:ietf:params:xml:ns:xmpp-sasl")) {
		/* Most common reason can be the default */
		SET_REASON(PURPLE_CONNECTION_ERROR_NETWORK_ERROR);
		if(xmlnode_get_child(packet, "aborted")) {
			text = _("Authorization Aborted");
		} else if(xmlnode_get_child(packet, "incorrect-encoding")) {
			text = _("Incorrect encoding in authorization");
		} else if(xmlnode_get_child(packet, "invalid-authzid")) {
			text = _("Invalid authzid");
		} else if(xmlnode_get_child(packet, "invalid-mechanism")) {
			text = _("Invalid Authorization Mechanism");
		} else if(xmlnode_get_child(packet, "mechanism-too-weak")) {
			SET_REASON(PURPLE_CONNECTION_ERROR_AUTHENTICATION_IMPOSSIBLE);
			text = _("Authorization mechanism too weak");
		} else if(xmlnode_get_child(packet, "not-authorized")) {
			SET_REASON(PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED);
			/* Clear the pasword if it isn't being saved */
			if (!purple_account_get_remember_password(js->gc->account))
				purple_account_set_password(js->gc->account, NULL);
			text = _("Not Authorized");
		} else if(xmlnode_get_child(packet, "temporary-auth-failure")) {
			text = _("Temporary Authentication Failure");
		} else {
			SET_REASON(PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED);
			text = _("Authentication Failure");
		}
	} else if(!strcmp(packet->name, "stream:error") ||
			 (!strcmp(packet->name, "error") && xmlns &&
				!strcmp(xmlns, "http://etherx.jabber.org/streams"))) {
		/* Most common reason as default: */
		SET_REASON(PURPLE_CONNECTION_ERROR_NETWORK_ERROR);
		if(xmlnode_get_child(packet, "bad-format")) {
			text = _("Bad Format");
		} else if(xmlnode_get_child(packet, "bad-namespace-prefix")) {
			text = _("Bad Namespace Prefix");
		} else if(xmlnode_get_child(packet, "conflict")) {
			SET_REASON(PURPLE_CONNECTION_ERROR_NAME_IN_USE);
			text = _("Resource Conflict");
		} else if(xmlnode_get_child(packet, "connection-timeout")) {
			text = _("Connection Timeout");
		} else if(xmlnode_get_child(packet, "host-gone")) {
			text = _("Host Gone");
		} else if(xmlnode_get_child(packet, "host-unknown")) {
			text = _("Host Unknown");
		} else if(xmlnode_get_child(packet, "improper-addressing")) {
			text = _("Improper Addressing");
		} else if(xmlnode_get_child(packet, "internal-server-error")) {
			text = _("Internal Server Error");
		} else if(xmlnode_get_child(packet, "invalid-id")) {
			text = _("Invalid ID");
		} else if(xmlnode_get_child(packet, "invalid-namespace")) {
			text = _("Invalid Namespace");
		} else if(xmlnode_get_child(packet, "invalid-xml")) {
			text = _("Invalid XML");
		} else if(xmlnode_get_child(packet, "nonmatching-hosts")) {
			text = _("Non-matching Hosts");
		} else if(xmlnode_get_child(packet, "not-authorized")) {
			text = _("Not Authorized");
		} else if(xmlnode_get_child(packet, "policy-violation")) {
			text = _("Policy Violation");
		} else if(xmlnode_get_child(packet, "remote-connection-failed")) {
			text = _("Remote Connection Failed");
		} else if(xmlnode_get_child(packet, "resource-constraint")) {
			text = _("Resource Constraint");
		} else if(xmlnode_get_child(packet, "restricted-xml")) {
			text = _("Restricted XML");
		} else if(xmlnode_get_child(packet, "see-other-host")) {
			text = _("See Other Host");
		} else if(xmlnode_get_child(packet, "system-shutdown")) {
			text = _("System Shutdown");
		} else if(xmlnode_get_child(packet, "undefined-condition")) {
			text = _("Undefined Condition");
		} else if(xmlnode_get_child(packet, "unsupported-encoding")) {
			text = _("Unsupported Encoding");
		} else if(xmlnode_get_child(packet, "unsupported-stanza-type")) {
			text = _("Unsupported Stanza Type");
		} else if(xmlnode_get_child(packet, "unsupported-version")) {
			text = _("Unsupported Version");
		} else if(xmlnode_get_child(packet, "xml-not-well-formed")) {
			text = _("XML Not Well Formed");
		} else {
			text = _("Stream Error");
		}
	}

#undef SET_REASON

	if(text || cdata) {
		char *ret = g_strdup_printf("%s%s%s", code ? code : "",
				code ? ": " : "", text ? text : cdata);
		g_free(cdata);
		return ret;
	} else {
		return NULL;
	}
}

static PurpleCmdRet jabber_cmd_chat_config(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if (!chat)
		return PURPLE_CMD_RET_FAILED;

	jabber_chat_request_room_configure(chat);
	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_register(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if (!chat)
		return PURPLE_CMD_RET_FAILED;

	jabber_chat_register(chat);
	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_topic(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if (!chat)
		return PURPLE_CMD_RET_FAILED;

	jabber_chat_change_topic(chat, args ? args[0] : NULL);
	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_nick(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if(!chat || !args || !args[0])
		return PURPLE_CMD_RET_FAILED;

	jabber_chat_change_nick(chat, args[0]);
	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_part(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if (!chat)
		return PURPLE_CMD_RET_FAILED;

	jabber_chat_part(chat, args ? args[0] : NULL);
	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_ban(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if(!chat || !args || !args[0])
		return PURPLE_CMD_RET_FAILED;

	if(!jabber_chat_ban_user(chat, args[0], args[1])) {
		*error = g_strdup_printf(_("Unable to ban user %s"), args[0]);
		return PURPLE_CMD_RET_FAILED;
	}

	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_affiliate(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if (!chat || !args || !args[0] || !args[1])
		return PURPLE_CMD_RET_FAILED;

	if (strcmp(args[1], "owner") != 0 &&
	    strcmp(args[1], "admin") != 0 &&
	    strcmp(args[1], "member") != 0 &&
	    strcmp(args[1], "outcast") != 0 &&
	    strcmp(args[1], "none") != 0) {
		*error = g_strdup_printf(_("Unknown affiliation: \"%s\""), args[1]);
		return PURPLE_CMD_RET_FAILED;
	}

	if (!jabber_chat_affiliate_user(chat, args[0], args[1])) {
		*error = g_strdup_printf(_("Unable to affiliate user %s as \"%s\""), args[0], args[1]);
		return PURPLE_CMD_RET_FAILED;
	}

	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_role(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if (!chat || !args || !args[0] || !args[1])
		return PURPLE_CMD_RET_FAILED;

	if (strcmp(args[1], "moderator") != 0 &&
	    strcmp(args[1], "participant") != 0 &&
	    strcmp(args[1], "visitor") != 0 &&
	    strcmp(args[1], "none") != 0) {
		*error = g_strdup_printf(_("Unknown role: \"%s\""), args[1]);
		return PURPLE_CMD_RET_FAILED;
	}

	if (!jabber_chat_role_user(chat, args[0], args[1])) {
		*error = g_strdup_printf(_("Unable to set role \"%s\" for user: %s"),
		                         args[1], args[0]);
		return PURPLE_CMD_RET_FAILED;
	}

	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_invite(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	if(!args || !args[0])
		return PURPLE_CMD_RET_FAILED;

	jabber_chat_invite(purple_conversation_get_gc(conv),
			purple_conv_chat_get_id(PURPLE_CONV_CHAT(conv)), args[1] ? args[1] : "",
			args[0]);

	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_join(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);
	GHashTable *components;

	if(!chat || !args || !args[0])
		return PURPLE_CMD_RET_FAILED;

	components = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

	g_hash_table_replace(components, "room", args[0]);
	g_hash_table_replace(components, "server", chat->server);
	g_hash_table_replace(components, "handle", chat->handle);
	if(args[1])
		g_hash_table_replace(components, "password", args[1]);

	jabber_chat_join(purple_conversation_get_gc(conv), components);

	g_hash_table_destroy(components);
	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_kick(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);

	if(!chat || !args || !args[0])
		return PURPLE_CMD_RET_FAILED;

	if(!jabber_chat_kick_user(chat, args[0], args[1])) {
		*error = g_strdup_printf(_("Unable to kick user %s"), args[0]);
		return PURPLE_CMD_RET_FAILED;
	}

	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_chat_msg(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberChat *chat = jabber_chat_find_by_conv(conv);
	char *who;

	if (!chat)
		return PURPLE_CMD_RET_FAILED;

	who = g_strdup_printf("%s@%s/%s", chat->room, chat->server, args[0]);

	jabber_message_send_im(purple_conversation_get_gc(conv), who, args[1], 0);

	g_free(who);
	return PURPLE_CMD_RET_OK;
}

static PurpleCmdRet jabber_cmd_ping(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	if(!args || !args[0])
		return PURPLE_CMD_RET_FAILED;

	if(!jabber_ping_jid(conv, args[0])) {
		*error = g_strdup_printf(_("Unable to ping user %s"), args[0]);
		return PURPLE_CMD_RET_FAILED;
	}

	return PURPLE_CMD_RET_OK;
}

static gboolean _jabber_send_buzz(JabberStream *js, const char *username, char **error) {

	JabberBuddy *jb;
	JabberBuddyResource *jbr;
	PurpleConnection *gc = js->gc;
	PurpleBuddy *buddy =
		purple_find_buddy(purple_connection_get_account(gc), username);
	const gchar *alias =
		buddy ? purple_buddy_get_contact_alias(buddy) : username;

	if(!username)
		return FALSE;

	jb = jabber_buddy_find(js, username, FALSE);
	if(!jb) {
		*error = g_strdup_printf(_("Unable to buzz, because there is nothing "
			"known about %s."), alias);
		return FALSE;
	}

	jbr = jabber_buddy_find_resource(jb, NULL);
	if (!jbr) {
		*error = g_strdup_printf(_("Unable to buzz, because %s might be offline."),
			alias);
		return FALSE;
	}

	if (jabber_resource_has_capability(jbr, XEP_0224_NAMESPACE)) {
		xmlnode *buzz, *msg = xmlnode_new("message");
		gchar *to;

		to = g_strdup_printf("%s/%s", username, jbr->name);
		xmlnode_set_attrib(msg, "to", to);
		g_free(to);

		/* avoid offline storage */
		xmlnode_set_attrib(msg, "type", "headline");

		buzz = xmlnode_new_child(msg, "attention");
		xmlnode_set_namespace(buzz, XEP_0224_NAMESPACE);

		jabber_send(js, msg);
		xmlnode_free(msg);

		return TRUE;
	} else {
		*error = g_strdup_printf(_("Unable to buzz, because %s does "
			"not support it or does not wish to receive buzzes now."), alias);
		return FALSE;
	}
}

static PurpleCmdRet jabber_cmd_buzz(PurpleConversation *conv,
		const char *cmd, char **args, char **error, void *data)
{
	JabberStream *js = conv->account->gc->proto_data;
	const gchar *who;

	if (!args || !args[0]) {
		/* use the buddy from conversation, if it's a one-to-one conversation */
		if (purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_IM) {
			who = purple_conversation_get_name(conv);
		} else {
			return PURPLE_CMD_RET_FAILED;
		}
	} else {
		who = args[0];
	}

	if (_jabber_send_buzz(js, who, error)) {
		const gchar *alias;
		gchar *str;
		PurpleBuddy *buddy =
			purple_find_buddy(purple_connection_get_account(conv->account->gc),
				who);

		if (buddy != NULL)
			alias = purple_buddy_get_contact_alias(buddy);
		else
			alias = who;

		str = g_strdup_printf(_("Buzzing %s..."), alias);
		purple_conversation_write(conv, NULL, str,
			PURPLE_MESSAGE_SYSTEM|PURPLE_MESSAGE_NOTIFY, time(NULL));
		g_free(str);

		return PURPLE_CMD_RET_OK;
	} else {
		return PURPLE_CMD_RET_FAILED;
	}
}

GList *jabber_attention_types(PurpleAccount *account)
{
	static GList *types = NULL;

	if (!types) {
		types = g_list_append(types, purple_attention_type_new("Buzz", _("Buzz"),
				_("%s has buzzed you!"), _("Buzzing %s...")));
	}

	return types;
}

gboolean jabber_send_attention(PurpleConnection *gc, const char *username, guint code)
{
	JabberStream *js = gc->proto_data;
	gchar *error = NULL;

	if (!_jabber_send_buzz(js, username, &error)) {
		purple_debug_error("jabber", "jabber_send_attention: jabber_cmd_buzz failed with error: %s\n", error ? error : "(NULL)");
		g_free(error);
		return FALSE;
	}

	return TRUE;
}


gboolean jabber_offline_message(const PurpleBuddy *buddy)
{
	return TRUE;
}

#ifdef USE_VV
typedef struct {
	PurpleConnection *pc;
	gchar *who;
	PurpleMediaSessionType type;
	
} JabberMediaRequest;

static void
jabber_media_cancel_cb(JabberMediaRequest *request,
		PurpleRequestFields *fields)
{
	g_free(request->who);
	g_free(request);
}

static void
jabber_media_ok_cb(JabberMediaRequest *request, PurpleRequestFields *fields)
{
	PurpleRequestField *field =
			purple_request_fields_get_field(fields, "resource");
	int selected_id = purple_request_field_choice_get_value(field);
	GList *labels = purple_request_field_choice_get_labels(field);
	gchar *who = g_strdup_printf("%s/%s", request->who,
			(gchar*)g_list_nth_data(labels, selected_id));
	jabber_initiate_media(request->pc, who, request->type);

	g_free(who);
	g_free(request->who);
	g_free(request);
}
#endif

gboolean
jabber_initiate_media(PurpleConnection *gc, const char *who, 
		      PurpleMediaSessionType type)
{
#ifdef USE_VV
	JabberStream *js = (JabberStream *) gc->proto_data;
	JabberBuddy *jb;
	JabberBuddyResource *jbr = NULL;
	char *resource;

	if (!js) {
		purple_debug_error("jabber",
				"jabber_initiate_media: NULL stream\n");
		return FALSE;
	}


	if((resource = jabber_get_resource(who)) != NULL) {
		/* they've specified a resource, no need to ask or
		 * default or anything, just do it */

		jb = jabber_buddy_find(js, who, FALSE);
		jbr = jabber_buddy_find_resource(jb, resource);
		g_free(resource);

		if (type & PURPLE_MEDIA_AUDIO &&
				!jabber_resource_has_capability(jbr,
				JINGLE_APP_RTP_SUPPORT_AUDIO) &&
				jabber_resource_has_capability(jbr,
				GOOGLE_VOICE_CAP))
			return jabber_google_session_initiate(
					gc->proto_data, who, type);
		else
			return jingle_rtp_initiate_media(
					gc->proto_data, who, type);
	}

	jb = jabber_buddy_find(js, who, FALSE);

	if(!jb || !jb->resources) {
		/* no resources online, we're trying to initiate with someone
		 * whose presence we're not subscribed to, or
		 * someone who is offline.  Let's inform the user */
		char *msg;

		if(!jb) {
			msg = g_strdup_printf(_("Unable to initiate media with %s, invalid JID"), who);
		} else if(jb->subscription & JABBER_SUB_TO) {
			msg = g_strdup_printf(_("Unable to initiate media with %s, user is not online"), who);
		} else {
			msg = g_strdup_printf(_("Unable to initiate media with %s, not subscribed to user presence"), who);
		}

		purple_notify_error(js->gc, _("Media Initiation Failed"),
				_("Media Initiation Failed"), msg);
		g_free(msg);
		return FALSE;
	} else if(!jb->resources->next) {
		/* only 1 resource online (probably our most common case)
		 * so no need to ask who to initiate with */
		gchar *name;
		gboolean result;
		jbr = jb->resources->data;
		name = g_strdup_printf("%s/%s", who, jbr->name);
		result = jabber_initiate_media(gc, name, type);
		g_free(name);
		return result;
	} else {
		/* we've got multiple resources,
		 * we need to pick one to initiate with */
		GList *l;
		char *msg;
		PurpleRequestFields *fields;
		PurpleRequestField *field = purple_request_field_choice_new(
				"resource", _("Resource"), 0);
		PurpleRequestFieldGroup *group;
		JabberMediaRequest *request;

		for(l = jb->resources; l; l = l->next)
		{
			JabberBuddyResource *ljbr = l->data;
			PurpleMediaCaps caps;
			gchar *name;
			name = g_strdup_printf("%s/%s", who, ljbr->name);
			caps = jabber_get_media_caps(gc, name);
			g_free(name);

			if ((type & PURPLE_MEDIA_AUDIO) &&
					(type & PURPLE_MEDIA_VIDEO)) {
				if (caps & PURPLE_MEDIA_CAPS_AUDIO_VIDEO) {
					jbr = ljbr;
					purple_request_field_choice_add(
							field, jbr->name);
				}
			} else if (type & (PURPLE_MEDIA_AUDIO) &&
					(caps & PURPLE_MEDIA_CAPS_AUDIO)) {
				jbr = ljbr;
				purple_request_field_choice_add(
						field, jbr->name);
			}else if (type & (PURPLE_MEDIA_VIDEO) &&
					(caps & PURPLE_MEDIA_CAPS_VIDEO)) {
				jbr = ljbr;
				purple_request_field_choice_add(
						field, jbr->name);
			}
		}

		if (jbr == NULL) {
			purple_debug_error("jabber",
					"No resources available\n");
			return FALSE;
		}

		if (g_list_length(purple_request_field_choice_get_labels(
				field)) <= 1) {
			gchar *name;
			gboolean result;
			purple_request_field_destroy(field);
			name = g_strdup_printf("%s/%s", who, jbr->name);
			result = jabber_initiate_media(gc, name, type);
			g_free(name);
			return result;
		}

		msg = g_strdup_printf(_("Please select the resource of %s to which you would like to start a media session with."), who);
		fields = purple_request_fields_new();
		group =	purple_request_field_group_new(NULL);
		request = g_new0(JabberMediaRequest, 1);
		request->pc = gc;
		request->who = g_strdup(who);
		request->type = type;

		purple_request_field_group_add_field(group, field);
		purple_request_fields_add_group(fields, group);
		purple_request_fields(gc, _("Select a Resource"), msg, NULL,
				fields,	_("Initiate Media"),
				G_CALLBACK(jabber_media_ok_cb), _("Cancel"),
				G_CALLBACK(jabber_media_cancel_cb),
				gc->account, who, NULL, request);

		g_free(msg);
		return TRUE;
	}
#endif
	return FALSE;
}

PurpleMediaCaps jabber_get_media_caps(PurpleConnection *gc, const char *who)
{
#ifdef USE_VV
	JabberStream *js = (JabberStream *) gc->proto_data;
	JabberBuddy *jb;
	JabberBuddyResource *jbr;
	PurpleMediaCaps caps = PURPLE_MEDIA_CAPS_NONE;
	gchar *resource;

	if (!js) {
		purple_debug_info("jabber",
				"jabber_can_do_media: NULL stream\n");
		return FALSE;
	}

	if ((resource = jabber_get_resource(who)) != NULL) {
		/* they've specified a resource, no need to ask or
		 * default or anything, just do it */

		jb = jabber_buddy_find(js, who, FALSE);
		jbr = jabber_buddy_find_resource(jb, resource);
		g_free(resource);

		if (!jbr) {
			purple_debug_error("jabber", "jabber_get_media_caps:"
					" Can't find resource %s\n", who);
			return caps;
		}

		if (jabber_resource_has_capability(jbr,
				JINGLE_APP_RTP_SUPPORT_AUDIO))
			caps |= PURPLE_MEDIA_CAPS_AUDIO_SINGLE_DIRECTION |
					PURPLE_MEDIA_CAPS_AUDIO;
		if (jabber_resource_has_capability(jbr,
				JINGLE_APP_RTP_SUPPORT_VIDEO))
			caps |= PURPLE_MEDIA_CAPS_VIDEO_SINGLE_DIRECTION |
					PURPLE_MEDIA_CAPS_VIDEO;
		if (caps & PURPLE_MEDIA_CAPS_AUDIO && caps &
				PURPLE_MEDIA_CAPS_VIDEO)
			caps |= PURPLE_MEDIA_CAPS_AUDIO_VIDEO;
		if (caps != PURPLE_MEDIA_CAPS_NONE) {
			if (!jabber_resource_has_capability(jbr,
					JINGLE_TRANSPORT_ICEUDP) &&
					!jabber_resource_has_capability(jbr,
					JINGLE_TRANSPORT_RAWUDP)) {
				purple_debug_info("jingle-rtp", "Buddy doesn't "
						"support the same transport types\n");
				caps = PURPLE_MEDIA_CAPS_NONE;
			} else
				caps |= PURPLE_MEDIA_CAPS_MODIFY_SESSION |
						PURPLE_MEDIA_CAPS_CHANGE_DIRECTION;
		}
		if (jabber_resource_has_capability(jbr, GOOGLE_VOICE_CAP))
			caps |= PURPLE_MEDIA_CAPS_AUDIO;
		return caps;
	}

	jb = jabber_buddy_find(js, who, FALSE);

	if(!jb || !jb->resources) {
		/* no resources online, we're trying to get caps for someone
		 * whose presence we're not subscribed to, or
		 * someone who is offline. */
		return caps;
	} else if(!jb->resources->next) {
		/* only 1 resource online (probably our most common case) */
		gchar *name;
		jbr = jb->resources->data;
		name = g_strdup_printf("%s/%s", who, jbr->name);
		caps = jabber_get_media_caps(gc, name);
		g_free(name);
	} else {
		/* we've got multiple resources, combine their caps */
		GList *l;

		for(l = jb->resources; l; l = l->next)
		{
			gchar *name;
			jbr = l->data;
			name = g_strdup_printf("%s/%s", who, jbr->name);
			caps |= jabber_get_media_caps(gc, name);
			g_free(name);
		}
	}

	return caps;
#else
	return PURPLE_MEDIA_CAPS_NONE;
#endif
}

void jabber_register_commands(void)
{
	purple_cmd_register("config", "", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY,
	                  "prpl-jabber", jabber_cmd_chat_config,
	                  _("config:  Configure a chat room."), NULL);
	purple_cmd_register("configure", "", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY,
	                  "prpl-jabber", jabber_cmd_chat_config,
	                  _("configure:  Configure a chat room."), NULL);
	purple_cmd_register("nick", "s", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY,
	                  "prpl-jabber", jabber_cmd_chat_nick,
	                  _("nick &lt;new nickname&gt;:  Change your nickname."),
	                  NULL);
	purple_cmd_register("part", "s", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_part, _("part [room]:  Leave the room."),
	                  NULL);
	purple_cmd_register("register", "", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY,
	                  "prpl-jabber", jabber_cmd_chat_register,
	                  _("register:  Register with a chat room."), NULL);
	/* XXX: there needs to be a core /topic cmd, methinks */
	purple_cmd_register("topic", "s", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_topic,
	                  _("topic [new topic]:  View or change the topic."),
	                  NULL);
	purple_cmd_register("ban", "ws", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_ban,
	                  _("ban &lt;user&gt; [reason]:  Ban a user from the room."),
	                  NULL);
	purple_cmd_register("affiliate", "ws", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_affiliate,
	                  _("affiliate &lt;user&gt; &lt;owner|admin|member|outcast|none&gt;: Set a user's affiliation with the room."),
	                  NULL);
	purple_cmd_register("role", "ws", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_role,
	                  _("role &lt;user&gt; &lt;moderator|participant|visitor|none&gt;: Set a user's role in the room."),
	                  NULL);
	purple_cmd_register("invite", "ws", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_invite,
	                  _("invite &lt;user&gt; [message]:  Invite a user to the room."),
	                  NULL);
	purple_cmd_register("join", "ws", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_join,
	                  _("join: &lt;room&gt; [password]:  Join a chat on this server."),
	                  NULL);
	purple_cmd_register("kick", "ws", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY |
	                  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS, "prpl-jabber",
	                  jabber_cmd_chat_kick,
	                  _("kick &lt;user&gt; [reason]:  Kick a user from the room."),
	                  NULL);
	purple_cmd_register("msg", "ws", PURPLE_CMD_P_PRPL,
	                  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_PRPL_ONLY,
	                  "prpl-jabber", jabber_cmd_chat_msg,
	                  _("msg &lt;user&gt; &lt;message&gt;:  Send a private message to another user."),
	                  NULL);
	purple_cmd_register("ping", "w", PURPLE_CMD_P_PRPL,
					  PURPLE_CMD_FLAG_CHAT | PURPLE_CMD_FLAG_IM |
					  PURPLE_CMD_FLAG_PRPL_ONLY,
					  "prpl-jabber", jabber_cmd_ping,
					  _("ping &lt;jid&gt;:	Ping a user/component/server."),
					  NULL);
	purple_cmd_register("buzz", "w", PURPLE_CMD_P_PRPL,
					  PURPLE_CMD_FLAG_IM | PURPLE_CMD_FLAG_PRPL_ONLY |
					  PURPLE_CMD_FLAG_ALLOW_WRONG_ARGS,
					  "prpl-jabber", jabber_cmd_buzz,
					  _("buzz: Buzz a user to get their attention"), NULL);
}

/* IPC functions */

/**
 * IPC function for determining if a contact supports a certain feature.
 *
 * @param account   The PurpleAccount
 * @param jid       The full JID of the contact.
 * @param feature   The feature's namespace.
 *
 * @return TRUE if supports feature; else FALSE.
 */
static gboolean
jabber_ipc_contact_has_feature(PurpleAccount *account, const gchar *jid,
                               const gchar *feature)
{
	PurpleConnection *gc = purple_account_get_connection(account);
	JabberStream *js;
	JabberBuddy *jb;
	JabberBuddyResource *jbr;
	gchar *resource;

	if (!purple_account_is_connected(account))
		return FALSE;
	js = gc->proto_data;

	if (!(resource = jabber_get_resource(jid)) || 
	    !(jb = jabber_buddy_find(js, jid, FALSE)) ||
	    !(jbr = jabber_buddy_find_resource(jb, resource))) {
		g_free(resource);
		return FALSE;
	}

	g_free(resource);

	return jabber_resource_has_capability(jbr, feature);
}

static void
jabber_ipc_add_feature(const gchar *feature)
{
	if (!feature)
		return;
	jabber_add_feature(feature, 0);

	/* send presence with new caps info for all connected accounts */
	jabber_caps_broadcast_change();
}

void
jabber_init_plugin(PurplePlugin *plugin)
{
	my_protocol = plugin;

	jabber_add_identity("client", "pc", NULL, PACKAGE);

	/* initialize jabber_features list */
	jabber_add_feature("jabber:iq:last", 0);
	jabber_add_feature("jabber:iq:oob", 0);
	jabber_add_feature("jabber:iq:time", 0);
	jabber_add_feature("urn:xmpp:time", 0);
	jabber_add_feature("jabber:iq:version", 0);
	jabber_add_feature("jabber:x:conference", 0);
	jabber_add_feature("http://jabber.org/protocol/bytestreams", 0);
	jabber_add_feature("http://jabber.org/protocol/caps", 0);
	jabber_add_feature("http://jabber.org/protocol/chatstates", 0);
	jabber_add_feature("http://jabber.org/protocol/disco#info", 0);
	jabber_add_feature("http://jabber.org/protocol/disco#items", 0);
	jabber_add_feature("http://jabber.org/protocol/ibb", 0);
	jabber_add_feature("http://jabber.org/protocol/muc", 0);
	jabber_add_feature("http://jabber.org/protocol/muc#user", 0);
	jabber_add_feature("http://jabber.org/protocol/si", 0);
	jabber_add_feature("http://jabber.org/protocol/si/profile/file-transfer", 0);
	jabber_add_feature("http://jabber.org/protocol/xhtml-im", 0);
	jabber_add_feature("urn:xmpp:ping", 0);

	/* Jingle features! */
	jabber_add_feature("http://www.google.com/xmpp/protocol/session", 0);
	jabber_add_feature("http://www.google.com/xmpp/protocol/voice/v1", 0);
	jabber_add_feature(JINGLE, 0);
	jabber_add_feature(JINGLE_APP_RTP_SUPPORT_AUDIO, 0);
	jabber_add_feature(JINGLE_APP_RTP_SUPPORT_VIDEO, 0);
	jabber_add_feature(JINGLE_TRANSPORT_RAWUDP, 0);
	jabber_add_feature(JINGLE_TRANSPORT_ICEUDP, 0);

	/* IPC functions */
	purple_plugin_ipc_register(plugin, "contact_has_feature", PURPLE_CALLBACK(jabber_ipc_contact_has_feature),
							 purple_marshal_BOOLEAN__POINTER_POINTER_POINTER,
							 purple_value_new(PURPLE_TYPE_BOOLEAN), 3,
							 purple_value_new(PURPLE_TYPE_SUBTYPE, PURPLE_SUBTYPE_ACCOUNT),
							 purple_value_new(PURPLE_TYPE_STRING),
							 purple_value_new(PURPLE_TYPE_STRING));
	purple_plugin_ipc_register(plugin, "add_feature", PURPLE_CALLBACK(jabber_ipc_add_feature),
							 purple_marshal_VOID__POINTER,
							 NULL, 1,
							 purple_value_new(PURPLE_TYPE_STRING));
}

void
jabber_uninit_plugin(void)
{
	jabber_features_destroy();
	jabber_identities_destroy();
}
