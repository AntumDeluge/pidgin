/*
 * gaim
 *
 * Gaim is the legal property of its developers, whose names are too numerous
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "internal.h"
#include "debug.h"

#include "yahoo.h"
#include "yahoo_packet.h"

struct yahoo_packet *yahoo_packet_new(enum yahoo_service service, enum yahoo_status status, int id)
{
	struct yahoo_packet *pkt = g_new0(struct yahoo_packet, 1);

	pkt->service = service;
	pkt->status = status;
	pkt->id = id;

	return pkt;
}

void yahoo_packet_hash(struct yahoo_packet *pkt, int key, const char *value)
{
	struct yahoo_pair *pair = g_new0(struct yahoo_pair, 1);
	pair->key = key;
	pair->value = g_strdup(value);
	pkt->hash = g_slist_append(pkt->hash, pair);
}

int yahoo_packet_length(struct yahoo_packet *pkt)
{
	GSList *l;

	int len = 0;

	l = pkt->hash;
	while (l) {
		struct yahoo_pair *pair = l->data;
		int tmp = pair->key;
		do {
			tmp /= 10;
			len++;
		} while (tmp);
		len += 2;
		len += strlen(pair->value);
		len += 2;
		l = l->next;
	}

	return len;
}

void yahoo_packet_read(struct yahoo_packet *pkt, guchar *data, int len)
{
	int pos = 0;

	while (pos + 1 < len) {
		char key[64], *value = NULL, *esc;
		int accept;
		int x;

		struct yahoo_pair *pair = g_new0(struct yahoo_pair, 1);

		/* this is weird, and in one of the chat packets, and causes us
		 * think all the values are keys and all the keys are values after
		 * this point if we don't handle it */
		if (data[pos] == '\0') {
			while (pos + 1 < len) {
				if (data[pos] == 0xc0 && data[pos + 1] == 0x80)
					break;
				pos++;
			}
			pos += 2;
			g_free(pair);
			continue;
		}

		x = 0;
		while (pos + 1 < len) {
			if (data[pos] == 0xc0 && data[pos + 1] == 0x80)
				break;
			if (x >= sizeof(key)-1) {
				x++;
				pos++;
				continue;
			}
			key[x++] = data[pos++];
		}
		if (x >= sizeof(key)-1) {
			x = 0;
		}
		key[x] = 0;
		pos += 2;
		pair->key = strtol(key, NULL, 10);
		accept = x; /* if x is 0 there was no key, so don't accept it */

		if (len - pos + 1 <= 0) {
			/* Truncated. Garbage or something. */
			accept = 0;
		}

		if (accept) {
			value = g_malloc(len - pos + 1);
			x = 0;
			while (pos + 1 < len) {
				if (data[pos] == 0xc0 && data[pos + 1] == 0x80)
					break;
				value[x++] = data[pos++];
			}
			value[x] = 0;
			pair->value = g_strdup(value);
			g_free(value);
			pkt->hash = g_slist_append(pkt->hash, pair);
			esc = g_strescape(pair->value, NULL);
			gaim_debug(GAIM_DEBUG_MISC, "yahoo",
					   "Key: %d  \tValue: %s\n", pair->key, esc);
			g_free(esc);
		} else {
			g_free(pair);
		}
		pos += 2;

		/* Skip over garbage we've noticed in the mail notifications */
		if (data[0] == '9' && data[pos] == 0x01)
			pos++;
	}
}

void yahoo_packet_write(struct yahoo_packet *pkt, guchar *data)
{
	GSList *l = pkt->hash;
	int pos = 0;

	while (l) {
		struct yahoo_pair *pair = l->data;
		guchar buf[100];

		g_snprintf(buf, sizeof(buf), "%d", pair->key);
		strcpy(data + pos, buf);
		pos += strlen(buf);
		data[pos++] = 0xc0;
		data[pos++] = 0x80;

		strcpy(data + pos, pair->value);
		pos += strlen(pair->value);
		data[pos++] = 0xc0;
		data[pos++] = 0x80;

		l = l->next;
	}
}

void yahoo_packet_dump(guchar *data, int len)
{
#ifdef YAHOO_DEBUG
	int i;

	gaim_debug(GAIM_DEBUG_MISC, "yahoo", "");

	for (i = 0; i + 1 < len; i += 2) {
		if ((i % 16 == 0) && i) {
			gaim_debug(GAIM_DEBUG_MISC, NULL, "\n");
			gaim_debug(GAIM_DEBUG_MISC, "yahoo", "");
		}

		gaim_debug(GAIM_DEBUG_MISC, NULL, "%02x%02x ", data[i], data[i + 1]);
	}
	if (i < len)
		gaim_debug(GAIM_DEBUG_MISC, NULL, "%02x", data[i]);

	gaim_debug(GAIM_DEBUG_MISC, NULL, "\n");
	gaim_debug(GAIM_DEBUG_MISC, "yahoo", "");

	for (i = 0; i < len; i++) {
		if ((i % 16 == 0) && i) {
			gaim_debug(GAIM_DEBUG_MISC, NULL, "\n");
			gaim_debug(GAIM_DEBUG_MISC, "yahoo", "");
		}

		if (g_ascii_isprint(data[i]))
			gaim_debug(GAIM_DEBUG_MISC, NULL, "%c ", data[i]);
		else
			gaim_debug(GAIM_DEBUG_MISC, NULL, ". ");
	}

	gaim_debug(GAIM_DEBUG_MISC, NULL, "\n");
#endif
}

int yahoo_packet_send(struct yahoo_packet *pkt, struct yahoo_data *yd)
{
	int pktlen = yahoo_packet_length(pkt);
	int len = YAHOO_PACKET_HDRLEN + pktlen;
	int ret;

	guchar *data;
	int pos = 0;

	if (yd->fd < 0)
		return -1;

	data = g_malloc0(len + 1);

	memcpy(data + pos, "YMSG", 4); pos += 4;

	if (yd->wm)
		pos += yahoo_put16(data + pos, YAHOO_WEBMESSENGER_PROTO_VER);
	else
		pos += yahoo_put16(data + pos, YAHOO_PROTO_VER);

	pos += yahoo_put16(data + pos, 0x0000);
	pos += yahoo_put16(data + pos, pktlen);
	pos += yahoo_put16(data + pos, pkt->service);
	pos += yahoo_put32(data + pos, pkt->status);
	pos += yahoo_put32(data + pos, pkt->id);

	yahoo_packet_write(pkt, data + pos);

	yahoo_packet_dump(data, len);
	ret = write(yd->fd, data, len);
	if (ret != len)
		gaim_debug_warning("yahoo", "Only wrote %d of %d bytes!", ret, len);
	g_free(data);

	return ret;
}

int yahoo_packet_send_and_free(struct yahoo_packet *pkt, struct yahoo_data *yd)
{
	int ret;
	
	ret = yahoo_packet_send(pkt, yd);
	yahoo_packet_free(pkt);
	return ret;
}

int yahoo_packet_send_special(struct yahoo_packet *pkt, int fd, int pad)
{
	int pktlen = yahoo_packet_length(pkt);
	int len = YAHOO_PACKET_HDRLEN + pktlen;
	int ret;

	guchar *data;
	int pos = 0;

	if (fd < 0)
		return -1;

	data = g_malloc0(len + 1);

	memcpy(data + pos, "YMSG", 4); pos += 4;

	pos += yahoo_put16(data + pos, YAHOO_PROTO_VER);

	pos += yahoo_put16(data + pos, 0x0000);
	pos += yahoo_put16(data + pos, pktlen + pad);
	pos += yahoo_put16(data + pos, pkt->service);
	pos += yahoo_put32(data + pos, pkt->status);
	pos += yahoo_put32(data + pos, pkt->id);

	yahoo_packet_write(pkt, data + pos);

	ret = write(fd, data, len);
	g_free(data);

	return ret;
}

void yahoo_packet_free(struct yahoo_packet *pkt)
{
	while (pkt->hash) {
		struct yahoo_pair *pair = pkt->hash->data;
		g_free(pair->value);
		g_free(pair);
		pkt->hash = g_slist_remove(pkt->hash, pair);
	}
	g_free(pkt);
}
