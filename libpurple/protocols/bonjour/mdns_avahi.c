/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "internal.h"

#include "mdns_interface.h"
#include "debug.h"
#include "buddy.h"
#include "bonjour.h"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>

#include <avahi-common/address.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/strlst.h>

#include <avahi-glib/glib-malloc.h>
#include <avahi-glib/glib-watch.h>

/* For some reason, this is missing from the Avahi type defines */
#ifndef AVAHI_DNS_TYPE_NULL
#define AVAHI_DNS_TYPE_NULL 0x0A
#endif

/* data used by avahi bonjour implementation */
typedef struct _avahi_session_impl_data {
	AvahiClient *client;
	AvahiGLibPoll *glib_poll;
	AvahiServiceBrowser *sb;
	AvahiEntryGroup *group;
} AvahiSessionImplData;

typedef struct _avahi_buddy_impl_data {
	AvahiRecordBrowser *record_browser;
} AvahiBuddyImplData;

static void
_resolver_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol,
		  AvahiResolverEvent event, const char *name, const char *type, const char *domain,
		  const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt,
		  AvahiLookupResultFlags flags, void *userdata) {

	BonjourBuddy *buddy;
	PurpleAccount *account = userdata;
	AvahiStringList *l;
	size_t size;
	char *key, *value;
	int ret;

	g_return_if_fail(r != NULL);

	switch (event) {
		case AVAHI_RESOLVER_FAILURE:
			purple_debug_error("bonjour", "_resolve_callback - Failure: %s\n",
				avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
			break;
		case AVAHI_RESOLVER_FOUND:
			/* create a buddy record */
			buddy = bonjour_buddy_new(name, account);

			/* Get the ip as a string */
			buddy->ip = g_malloc(AVAHI_ADDRESS_STR_MAX);
			avahi_address_snprint(buddy->ip, AVAHI_ADDRESS_STR_MAX, a);

			buddy->port_p2pj = port;

			/* Obtain the parameters from the text_record */
			l = txt;
			while (l != NULL) {
				ret = avahi_string_list_get_pair(l, &key, &value, &size);
				l = l->next;
				if (ret < 0)
					continue;
				set_bonjour_buddy_value(buddy, key, value, size);
				/* TODO: Since we're using the glib allocator, I think we
				 * can use the values instead of re-copying them */
				avahi_free(key);
				avahi_free(value);
			}

			if (!bonjour_buddy_check(buddy))
				bonjour_buddy_delete(buddy);
			else
				/* Add or update the buddy in our buddy list */
				bonjour_buddy_add_to_purple(buddy);

			break;
		default:
			purple_debug_info("bonjour", "Unrecognized Service Resolver event: %d.\n", event);
	}

	avahi_service_resolver_free(r);
}

static void
_browser_callback(AvahiServiceBrowser *b, AvahiIfIndex interface,
		  AvahiProtocol protocol, AvahiBrowserEvent event,
		  const char *name, const char *type, const char *domain,
		  AvahiLookupResultFlags flags, void *userdata) {

	PurpleAccount *account = userdata;
	PurpleBuddy *gb = NULL;

	switch (event) {
		case AVAHI_BROWSER_FAILURE:
			purple_debug_error("bonjour", "_browser_callback - Failure: %s\n",
				avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
			/* TODO: This is an error that should be handled. */
			break;
		case AVAHI_BROWSER_NEW:
			/* A new peer has joined the network and uses iChat bonjour */
			purple_debug_info("bonjour", "_browser_callback - new service\n");
			/* Make sure it isn't us */
			if (g_ascii_strcasecmp(name, account->username) != 0) {
				if (!avahi_service_resolver_new(avahi_service_browser_get_client(b),
						interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC,
						0, _resolver_callback, account)) {
					purple_debug_warning("bonjour", "_browser_callback -- Error initiating resolver: %s\n",
						avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
				}
			}
			break;
		case AVAHI_BROWSER_REMOVE:
			purple_debug_info("bonjour", "_browser_callback - Remove service\n");
			gb = purple_find_buddy(account, name);
			if (gb != NULL) {
				bonjour_buddy_delete(gb->proto_data);
				purple_blist_remove_buddy(gb);
			}
			break;
		case AVAHI_BROWSER_ALL_FOR_NOW:
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
			purple_debug_warning("bonjour", "(Browser) %s\n",
				event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
			break;
		default:
			purple_debug_info("bonjour", "Unrecognized Service browser event: %d.\n", event);
	}
}

static void
_entry_group_cb(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
	AvahiSessionImplData *idata = userdata;

	g_return_if_fail(g == idata->group || idata->group == NULL);

	switch(state) {
		case AVAHI_ENTRY_GROUP_ESTABLISHED:
			purple_debug_info("bonjour", "Successfully registered service.\n");
			break;
		case AVAHI_ENTRY_GROUP_COLLISION:
			purple_debug_error("bonjour", "Collision registering entry group.\n");
			/* TODO: Handle error - this should log out the account. (Possibly with "wants to die")*/
			break;
		case AVAHI_ENTRY_GROUP_FAILURE:
			purple_debug_error("bonjour", "Error registering entry group: %s\n.",
				avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
			/* TODO: Handle error - this should log out the account.*/
			break;
		case AVAHI_ENTRY_GROUP_UNCOMMITED:
		case AVAHI_ENTRY_GROUP_REGISTERING:
			break;
	}

}

static void
_buddy_icon_record_cb(AvahiRecordBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
		      AvahiBrowserEvent event, const char *name, uint16_t clazz, uint16_t type,
		      const void *rdata, size_t size, AvahiLookupResultFlags flags, void *userdata) {
	BonjourBuddy *buddy = userdata;
	AvahiBuddyImplData *idata = buddy->mdns_impl_data;

	purple_buddy_icons_set_for_user(buddy->account, buddy->name,
					g_memdup(rdata, size), size, buddy->phsh);

	avahi_record_browser_free(idata->record_browser);
	idata->record_browser = NULL;
}

/****************************
 * mdns_interface functions *
 ****************************/

gboolean _mdns_init_session(BonjourDnsSd *data) {
	AvahiSessionImplData *idata = g_new0(AvahiSessionImplData, 1);
	const AvahiPoll *poll_api;
	int error;

	/* Tell avahi to use g_malloc and g_free */
	avahi_set_allocator (avahi_glib_allocator ());

	/* This currently depends on the glib mainloop,
	 * we should make it use the libpurple abstraction */

	idata->glib_poll = avahi_glib_poll_new(NULL, G_PRIORITY_DEFAULT);

	poll_api = avahi_glib_poll_get(idata->glib_poll);

	idata->client = avahi_client_new(poll_api, 0, NULL, data, &error);

	if (idata->client == NULL) {
		purple_debug_error("bonjour", "Error initializing Avahi: %s", avahi_strerror(error));
		avahi_glib_poll_free(idata->glib_poll);
		g_free(idata);
		return FALSE;
	}

	data->mdns_impl_data = idata;

	return TRUE;
}

gboolean _mdns_publish(BonjourDnsSd *data, PublishType type) {
	int publish_result = 0;
	char portstring[6];
	const char *jid, *aim, *email;
	AvahiSessionImplData *idata = data->mdns_impl_data;
	AvahiStringList *lst = NULL;

	g_return_val_if_fail(idata != NULL, FALSE);

	if (!idata->group) {
		idata->group = avahi_entry_group_new(idata->client,
						     _entry_group_cb, idata);
		if (!idata->group) {
			purple_debug_error("bonjour",
				"Unable to initialize the data for the mDNS (%s).\n",
				avahi_strerror(avahi_client_errno(idata->client)));
			return FALSE;
		}
	}

	/* Convert the port to a string */
	snprintf(portstring, sizeof(portstring), "%d", data->port_p2pj);

	jid = purple_account_get_string(data->account, "jid", NULL);
	aim = purple_account_get_string(data->account, "AIM", NULL);
	email = purple_account_get_string(data->account, "email", NULL);

	/* We should try to follow XEP-0174, but some clients have "issues", so we humor them.
	 * See http://telepathy.freedesktop.org/wiki/SalutInteroperability
	 */

	/* Needed by iChat */
	lst = avahi_string_list_add_pair(lst,"txtvers", "1");
	/* Needed by Gaim/Pidgin <= 2.0.1 (remove at some point) */
	lst = avahi_string_list_add_pair(lst, "1st", data->first);
	/* Needed by Gaim/Pidgin <= 2.0.1 (remove at some point) */
	lst = avahi_string_list_add_pair(lst, "last", data->last);
	/* Needed by Adium */
	lst = avahi_string_list_add_pair(lst, "port.p2pj", portstring);
	/* Needed by iChat, Gaim/Pidgin <= 2.0.1 */
	lst = avahi_string_list_add_pair(lst, "status", data->status);
	/* Currently always set to "!" since we don't support AV and wont ever be in a conference */
	lst = avahi_string_list_add_pair(lst, "vc", data->vc);
	lst = avahi_string_list_add_pair(lst, "ver", VERSION);
	if (email != NULL && *email != '\0')
		lst = avahi_string_list_add_pair(lst, "email", email);
	if (jid != NULL && *jid != '\0')
		lst = avahi_string_list_add_pair(lst, "jid", jid);
	/* Nonstandard, but used by iChat */
	if (aim != NULL && *aim != '\0')
		lst = avahi_string_list_add_pair(lst, "AIM", aim);
	if (data->msg != NULL && *data->msg != '\0')
		lst = avahi_string_list_add_pair(lst, "msg", data->msg);
	if (data->phsh != NULL && *data->phsh != '\0')
		lst = avahi_string_list_add_pair(lst, "phsh", data->phsh);

	/* TODO: ext, nick, node */


	/* Publish the service */
	switch (type) {
		case PUBLISH_START:
			publish_result = avahi_entry_group_add_service_strlst(
				idata->group, AVAHI_IF_UNSPEC,
				AVAHI_PROTO_UNSPEC, 0,
				purple_account_get_username(data->account),
				ICHAT_SERVICE, NULL, NULL, data->port_p2pj, lst);
			break;
		case PUBLISH_UPDATE:
			publish_result = avahi_entry_group_update_service_txt_strlst(
				idata->group, AVAHI_IF_UNSPEC,
				AVAHI_PROTO_UNSPEC, 0,
				purple_account_get_username(data->account),
				ICHAT_SERVICE, NULL, lst);
			break;
	}

	/* Free the memory used by temp data */
	avahi_string_list_free(lst);

	if (publish_result < 0) {
		purple_debug_error("bonjour",
			"Failed to add the " ICHAT_SERVICE " service. Error: %s\n",
			avahi_strerror(publish_result));
		return FALSE;
	}

	if ((publish_result = avahi_entry_group_commit(idata->group)) < 0) {
		purple_debug_error("bonjour",
			"Failed to commit " ICHAT_SERVICE " service. Error: %s\n",
			avahi_strerror(publish_result));
		return FALSE;
	}

	return TRUE;
}

gboolean _mdns_browse(BonjourDnsSd *data) {
	AvahiSessionImplData *idata = data->mdns_impl_data;

	g_return_val_if_fail(idata != NULL, FALSE);

	idata->sb = avahi_service_browser_new(idata->client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, ICHAT_SERVICE, NULL, 0, _browser_callback, data->account);
	if (!idata->sb) {

		purple_debug_error("bonjour",
			"Unable to initialize service browser.  Error: %s\n.",
			avahi_strerror(avahi_client_errno(idata->client)));
		return FALSE;
	}

	return TRUE;
}

/* This is done differently than with Howl/Apple Bonjour */
guint _mdns_register_to_mainloop(BonjourDnsSd *data) {
	return 0;
}

void _mdns_stop(BonjourDnsSd *data) {
	AvahiSessionImplData *idata = data->mdns_impl_data;

	if (idata == NULL || idata->client == NULL)
		return;

	if (idata->sb != NULL)
		avahi_service_browser_free(idata->sb);

	avahi_client_free(idata->client);
	avahi_glib_poll_free(idata->glib_poll);

	g_free(idata);

	data->mdns_impl_data = NULL;
}

void _mdns_init_buddy(BonjourBuddy *buddy) {
	buddy->mdns_impl_data = g_new0(AvahiBuddyImplData, 1);
}

void _mdns_delete_buddy(BonjourBuddy *buddy) {
	AvahiBuddyImplData *idata = buddy->mdns_impl_data;

	g_return_if_fail(idata != NULL);

	if (idata->record_browser != NULL)
		avahi_record_browser_free(idata->record_browser);

	g_free(idata);

	buddy->mdns_impl_data = NULL;
}

void bonjour_dns_sd_retrieve_buddy_icon(BonjourBuddy* buddy) {
	PurpleConnection *conn = purple_account_get_connection(buddy->account);
	BonjourData *bd = conn->proto_data;
	AvahiSessionImplData *session_idata = bd->dns_sd_data->mdns_impl_data;
	AvahiBuddyImplData *idata = buddy->mdns_impl_data;
	gchar *name;

	g_return_if_fail(idata != NULL);

	if (idata->record_browser != NULL)
		avahi_record_browser_free(idata->record_browser);

	name = g_strdup_printf("%s." ICHAT_SERVICE "local", buddy->name);
	idata->record_browser = avahi_record_browser_new(session_idata->client, AVAHI_IF_UNSPEC,
		AVAHI_PROTO_UNSPEC, name, AVAHI_DNS_CLASS_IN, AVAHI_DNS_TYPE_NULL, 0,
		_buddy_icon_record_cb, buddy);
	g_free(name);

	if (!idata->record_browser) {
		purple_debug_error("bonjour",
			"Unable to initialize record browser.  Error: %s\n.",
			avahi_strerror(avahi_client_errno(session_idata->client)));
	}

}

