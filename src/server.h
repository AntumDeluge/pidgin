/**
 * @file server.h Server API
 * @ingroup core
 *
 * gaim
 *
 * Copyright (C) 2003 Christian Hammond <chipx86@gnupdate.org>
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
 */
#ifndef _GAIM_SERVER_H_
#define _GAIM_SERVER_H_

#define WFLAG_SEND	0x01
#define WFLAG_RECV	0x02
#define WFLAG_AUTO	0x04
#define WFLAG_WHISPER	0x08
#define WFLAG_FILERECV	0x10
#define WFLAG_SYSTEM	0x20
#define WFLAG_NICK	0x40
#define WFLAG_NOLOG	0x80
#define WFLAG_COLORIZE  0x100

#define IM_FLAG_AWAY     0x01
#define IM_FLAG_CHECKBOX 0x02
#define IM_FLAG_GAIMUSER 0x04

/*
 * Really user states are controlled by the PRPLs now. We just
 * use this for event_away
 */
#define UC_UNAVAILABLE  1

#include "account.h"
#include "conversation.h"

#ifdef __cplusplus
extern "C" {
#endif

void serv_login(GaimAccount *);
void serv_close(GaimConnection *);
void serv_touch_idle(GaimConnection *);
int  serv_send_im(GaimConnection *, char *, char *, int, int);
void serv_get_info(GaimConnection *, char *);
void serv_get_dir(GaimConnection *, char *);
void serv_set_idle(GaimConnection *, int);
void serv_set_info(GaimConnection *, char *);
void serv_set_away(GaimConnection *, char *, char *);
void serv_set_away_all(char *);
int  serv_send_typing(GaimConnection *, char *, int);
void serv_change_passwd(GaimConnection *, const char *, const char *);
void serv_add_buddy(GaimConnection *, const char *);
void serv_add_buddies(GaimConnection *, GList *);
void serv_remove_buddy(GaimConnection *, char *, char *);
void serv_remove_buddies(GaimConnection *, GList *, char *);
void serv_add_permit(GaimConnection *, const char *);
void serv_add_deny(GaimConnection *, const char *);
void serv_rem_permit(GaimConnection *, const char *);
void serv_rem_deny(GaimConnection *, const char *);
void serv_set_permit_deny(GaimConnection *);
void serv_warn(GaimConnection *, char *, int);
void serv_set_dir(GaimConnection *, const char *, const char *,
				  const char *, const char *, const char *,
				  const char *, const char *, int);
void serv_dir_search(GaimConnection *, const char *, const char *,
					 const char *, const char *, const char *, const char *,
					 const char *, const char *);
void serv_join_chat(GaimConnection *, GHashTable *);
void serv_chat_invite(GaimConnection *, int, const char *, const char *);
void serv_chat_leave(GaimConnection *, int);
void serv_chat_whisper(GaimConnection *, int, char *, char *);
int  serv_chat_send(GaimConnection *, int, char *);
void serv_got_popup(char *, char *, int, int);
void serv_get_away(GaimConnection *, const char *);
void serv_alias_buddy(struct buddy *);
void serv_got_alias(GaimConnection *gc, const char *who, const char *alias);
void serv_move_buddy(struct buddy *, struct group *, struct group *);
void serv_rename_group(GaimConnection *, struct group *, const char *);
void serv_got_eviled(GaimConnection *gc, const char *name, int lev);
void serv_got_typing(GaimConnection *gc, const char *name, int timeout,
					 GaimTypingState state);
void serv_set_buddyicon(GaimConnection *gc, const char *filename);
void serv_got_typing_stopped(GaimConnection *gc, const char *name);
void serv_got_im(GaimConnection *gc, const char *who, const char *msg,
				 guint32 flags, time_t mtime, gint len);
void serv_got_update(GaimConnection *gc, const char *name, int loggedin,
					 int evil, time_t signon, time_t idle, int type);
void serv_finish_login(GaimConnection *gc);
void serv_got_chat_invite(GaimConnection *gc, const char *name,
						  const char *who, const char *message,
						  GHashTable *data);
GaimConversation *serv_got_joined_chat(GaimConnection *gc,
									   int id, const char *name);
void serv_got_chat_left(GaimConnection *g, int id);
void serv_got_chat_in(GaimConnection *g, int id, char *who,
					  int whisper, char *message, time_t mtime);

#ifdef __cplusplus
}
#endif

#endif /* _GAIM_SERVER_H_ */
