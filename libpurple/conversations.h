/**
 * @file conversations.h Conversations subsystem API
 * @ingroup core
 * @see @ref conversation-signals
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
#ifndef _PURPLE_CONVERSATIONS_H_
#define _PURPLE_CONVERSATIONS_H_

#include "conversationtypes.h"
#include "server.h"

G_BEGIN_DECLS

/**************************************************************************/
/** @name Conversations Subsystem                                         */
/**************************************************************************/
/*@{*/

/**
 * Adds a conversation to the list of conversations.
 *
 * @conv: The conversation.
 */
void purple_conversations_add(PurpleConversation *conv);

/**
 * Removes a conversation from the list of conversations.
 *
 * @conv: The conversation.
 */
void purple_conversations_remove(PurpleConversation *conv);

/**
 * Returns a list of all conversations.
 *
 * This list includes both IMs and chats.
 *
 * Returns: (transfer none): A GList of all conversations.
 */
GList *purple_conversations_get_all(void);

/**
 * Returns a list of all IMs.
 *
 * Returns: (transfer none): A GList of all IMs.
 */
GList *purple_conversations_get_ims(void);

/**
 * Returns a list of all chats.
 *
 * Returns: (transfer none): A GList of all chats.
 */
GList *purple_conversations_get_chats(void);

/**
 * Finds a conversation of any type with the specified name and Purple account.
 *
 * @name: The name of the conversation.
 * @account: The account associated with the conversation.
 *
 * Returns: The conversation if found, or %NULL otherwise.
 */
PurpleConversation *purple_conversations_find_with_account(const char *name,
		const PurpleAccount *account);

/**
 * Finds an IM with the specified name and Purple account.
 *
 * @name: The name of the conversation.
 * @account: The account associated with the conversation.
 *
 * Returns: The conversation if found, or %NULL otherwise.
 */
PurpleIMConversation *purple_conversations_find_im_with_account(const char *name,
		const PurpleAccount *account);

/**
 * Finds a chat with the specified name and Purple account.
 *
 * @name: The name of the conversation.
 * @account: The account associated with the conversation.
 *
 * Returns: The conversation if found, or %NULL otherwise.
 */
PurpleChatConversation *purple_conversations_find_chat_with_account(const char *name,
		const PurpleAccount *account);

/**
 * Finds a chat with the specified chat ID.
 *
 * @gc: The purple_connection.
 * @id: The chat ID.
 *
 * Returns: The chat conversation.
 */
PurpleChatConversation *purple_conversations_find_chat(const PurpleConnection *gc, int id);

/**
 * Sets the default conversation UI operations structure.
 *
 * @ops:  The UI conversation operations structure.
 */
void purple_conversations_set_ui_ops(PurpleConversationUiOps *ops);

/**
 * Gets the default conversation UI operations structure.
 *
 * Returns:  The UI conversation operations structure.
 */
PurpleConversationUiOps *purple_conversations_get_ui_ops(void);

/**
 * Returns the conversation subsystem handle.
 *
 * Returns: The conversation subsystem handle.
 */
void *purple_conversations_get_handle(void);

/**
 * Initializes the conversation subsystem.
 */
void purple_conversations_init(void);

/**
 * Uninitializes the conversation subsystem.
 */
void purple_conversations_uninit(void);

/*@}*/

G_END_DECLS

#endif /* _PURPLE_CONVERSATIONS_H_ */