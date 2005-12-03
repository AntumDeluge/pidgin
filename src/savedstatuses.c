/**
 * @file savedstatus.c Saved Status API
 * @ingroup core
 *
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
 */
#include "internal.h"

#include "debug.h"
#include "notify.h"
#include "savedstatuses.h"
#include "status.h"
#include "util.h"
#include "xmlnode.h"

/**
 * The information stores a snap-shot of the statuses of all
 * your accounts.  Basically these are your saved away messages.
 * There is an overall status and message that applies to
 * all your accounts, and then each individual account can
 * optionally have a different custom status and message.
 *
 * The changes to status.xml caused by the new status API
 * are fully backward compatible.  The new status API just
 * adds the optional sub-statuses to the XML file.
 */
struct _GaimSavedStatus
{
	char *title;
	GaimStatusPrimitive type;
	char *message;

	/** The timestamp when this saved status was created. This must be unique. */
	time_t creation_time;

	time_t lastused;

	GList *substatuses;      /**< A list of GaimSavedStatusSub's. */
};

/*
 * TODO: If a GaimStatusType is deleted, need to also delete any
 *       associated GaimSavedStatusSub's?
 */
struct _GaimSavedStatusSub
{
	GaimAccount *account;
	const GaimStatusType *type;
	char *message;
};

static GList      *saved_statuses = NULL;
static guint       save_timer = 0;
static gboolean    statuses_loaded = FALSE;

/*
 * This hash table keeps track of which timestamps we've
 * used so that we don't have two saved statuses with the
 * same 'creation_time' timestamp.  The 'created' timestamp
 * is used as a unique identifier.
 *
 * So the key in this hash table is the creation_time and
 * the value is a pointer to the GaimSavedStatus.
 */
static GHashTable *creation_times;


/*********************************************************************
 * Private utility functions                                         *
 *********************************************************************/

static void
free_statussavedsub(GaimSavedStatusSub *substatus)
{
	g_return_if_fail(substatus != NULL);

	g_free(substatus->message);
	g_free(substatus);
}

static void
free_statussaved(GaimSavedStatus *status)
{
	g_return_if_fail(status != NULL);

	g_free(status->title);
	g_free(status->message);

	while (status->substatuses != NULL)
	{
		GaimSavedStatusSub *substatus = status->substatuses->data;
		status->substatuses = g_list_remove(status->substatuses, substatus);
		free_statussavedsub(substatus);
	}

	g_free(status);
}

/*
 * Set the timestamp for when this saved status was created, and
 * make sure it is unique.
 */
static void
set_creation_time(GaimSavedStatus *status, time_t creation_time)
{
	g_return_if_fail(status != NULL);

	/* Avoid using 0 because it's an invalid hash key */
	status->creation_time = creation_time != 0 ? creation_time : 1;

	while (g_hash_table_lookup(creation_times, &status->creation_time) != NULL)
		status->creation_time++;

	g_hash_table_insert(creation_times,
						&status->creation_time,
						status);
}

/*********************************************************************
 * Writing to disk                                                   *
 *********************************************************************/

static xmlnode *
substatus_to_xmlnode(GaimSavedStatusSub *substatus)
{
	xmlnode *node, *child;

	node = xmlnode_new("substatus");

	child = xmlnode_new_child(node, "account");
	xmlnode_set_attrib(child, "protocol", gaim_account_get_protocol_id(substatus->account));
	xmlnode_insert_data(child, gaim_account_get_username(substatus->account), -1);

	child = xmlnode_new_child(node, "state");
	xmlnode_insert_data(child, gaim_status_type_get_id(substatus->type), -1);

	if (substatus->message != NULL)
	{
		child = xmlnode_new_child(node, "message");
		xmlnode_insert_data(child, substatus->message, -1);
	}

	return node;
}

static xmlnode *
status_to_xmlnode(GaimSavedStatus *status)
{
	xmlnode *node, *child;
	char buf[21];
	GList *cur;

	node = xmlnode_new("status");
	if (status->title != NULL)
	{
		xmlnode_set_attrib(node, "name", status->title);
	}
	else
	{
		/*
		 * Gaim 1.5.0 and earlier require a name to be set, so we
		 * do this little hack to maintain backward compatability
		 * in the status.xml file.  Eventually this should be removed
		 * and we should determine if a status is transient by
		 * whether the "name" attribute is set to something or if
		 * it does not exist at all.
		 */
		xmlnode_set_attrib(node, "name", "Auto-Cached");
		xmlnode_set_attrib(node, "transient", "true");
	}

	snprintf(buf, sizeof(buf), "%lu", status->creation_time);
	xmlnode_set_attrib(node, "created", buf);

	snprintf(buf, sizeof(buf), "%lu", status->lastused);
	xmlnode_set_attrib(node, "lastused", buf);

	child = xmlnode_new_child(node, "state");
	xmlnode_insert_data(child, gaim_primitive_get_id_from_type(status->type), -1);

	if (status->message != NULL)
	{
		child = xmlnode_new_child(node, "message");
		xmlnode_insert_data(child, status->message, -1);
	}

	for (cur = status->substatuses; cur != NULL; cur = cur->next)
	{
		child = substatus_to_xmlnode(cur->data);
		xmlnode_insert_child(node, child);
	}

	return node;
}

static xmlnode *
statuses_to_xmlnode(void)
{
	xmlnode *node, *child;
	GList *cur;

	node = xmlnode_new("statuses");
	xmlnode_set_attrib(node, "version", "1.0");

	for (cur = saved_statuses; cur != NULL; cur = cur->next)
	{
		child = status_to_xmlnode(cur->data);
		xmlnode_insert_child(node, child);
	}

	return node;
}

static void
sync_statuses(void)
{
	xmlnode *node;
	char *data;

	if (!statuses_loaded)
	{
		gaim_debug_error("status", "Attempted to save statuses before they "
						 "were read!\n");
		return;
	}

	node = statuses_to_xmlnode();
	data = xmlnode_to_formatted_str(node, NULL);
	gaim_util_write_data_to_file("status.xml", data, -1);
	g_free(data);
	xmlnode_free(node);
}

static gboolean
save_cb(gpointer data)
{
	sync_statuses();
	save_timer = 0;
	return FALSE;
}

static void
schedule_save(void)
{
	if (save_timer == 0)
		save_timer = gaim_timeout_add(5000, save_cb, NULL);
}


/*********************************************************************
 * Reading from disk                                                 *
 *********************************************************************/

static GaimSavedStatusSub *
parse_substatus(xmlnode *substatus)
{
	GaimSavedStatusSub *ret;
	xmlnode *node;
	char *data;

	ret = g_new0(GaimSavedStatusSub, 1);

	/* Read the account */
	node = xmlnode_get_child(substatus, "account");
	if (node != NULL)
	{
		char *acct_name;
		const char *protocol;
		acct_name = xmlnode_get_data(node);
		protocol = xmlnode_get_attrib(node, "protocol");
		if ((acct_name != NULL) && (protocol != NULL))
			ret->account = gaim_accounts_find(acct_name, protocol);
		g_free(acct_name);
	}

	if (ret->account == NULL)
	{
		g_free(ret);
		return NULL;
	}

	/* Read the state */
	node = xmlnode_get_child(substatus, "state");
	if ((node != NULL) && ((data = xmlnode_get_data(node)) != NULL))
	{
		ret->type = gaim_status_type_find_with_id(
							ret->account->status_types, data);
		g_free(data);
	}

	/* Read the message */
	node = xmlnode_get_child(substatus, "message");
	if ((node != NULL) && ((data = xmlnode_get_data(node)) != NULL))
	{
		ret->message = data;
	}

	return ret;
}

/**
 * Parse a saved status and add it to the saved_statuses linked list.
 *
 * Here's an example of the XML for a saved status:
 *   <status name="Girls">
 *       <state>away</state>
 *       <message>I like the way that they walk
 *   And it's chill to hear them talk
 *   And I can always make them smile
 *   From White Castle to the Nile</message>
 *       <substatus>
 *           <account protocol='prpl-oscar'>markdoliner</account>
 *           <state>available</state>
 *           <message>The ladies man is here to answer your queries.</message>
 *       </substatus>
 *       <substatus>
 *           <account protocol='prpl-oscar'>giantgraypanda</account>
 *           <state>away</state>
 *           <message>A.C. ain't in charge no more.</message>
 *       </substatus>
 *   </status>
 *
 * I know.  Moving, huh?
 */
static GaimSavedStatus *
parse_status(xmlnode *status)
{
	GaimSavedStatus *ret;
	xmlnode *node;
	const char *attrib;
	char *data;
	int i;

	ret = g_new0(GaimSavedStatus, 1);

	attrib = xmlnode_get_attrib(status, "transient");
	if ((attrib == NULL) || (strcmp(attrib, "true")))
	{
		/* Read the title */
		attrib = xmlnode_get_attrib(status, "name");
		ret->title = g_strdup(attrib);
	}

	if (ret->title != NULL)
	{
		/* Ensure the title is unique */
		i = 2;
		while (gaim_savedstatus_find(ret->title) != NULL)
		{
			g_free(ret->title);
			ret->title = g_strdup_printf("%s %d", attrib, i);
			i++;
		}
	}

	/* Read the creation time */
	attrib = xmlnode_get_attrib(status, "created");
	set_creation_time(ret, (attrib != NULL ? atol(attrib) : 0));

	/* Read the last used time */
	attrib = xmlnode_get_attrib(status, "lastused");
	ret->lastused = (attrib != NULL ? atol(attrib) : 0);

	/* Read the primitive status type */
	node = xmlnode_get_child(status, "state");
	if ((node != NULL) && ((data = xmlnode_get_data(node)) != NULL))
	{
		ret->type = gaim_primitive_get_type_from_id(data);
		g_free(data);
	}

	/* Read the message */
	node = xmlnode_get_child(status, "message");
	if ((node != NULL) && ((data = xmlnode_get_data(node)) != NULL))
	{
		ret->message = data;
	}

	/* Read substatuses */
	for (node = xmlnode_get_child(status, "substatus"); node != NULL;
			node = xmlnode_get_next_twin(node))
	{
		GaimSavedStatusSub *new;
		new = parse_substatus(node);
		if (new != NULL)
			ret->substatuses = g_list_prepend(ret->substatuses, new);
	}

	return ret;
}

/**
 * Read the saved statuses from a file in the Gaim user dir.
 *
 * @return TRUE on success, FALSE on failure (if the file can not
 *         be opened, or if it contains invalid XML).
 */
static void
load_statuses(void)
{
	xmlnode *statuses, *status;

	statuses_loaded = TRUE;

	statuses = gaim_util_read_xml_from_file("status.xml", _("saved statuses"));

	if (statuses == NULL)
		return;

	for (status = xmlnode_get_child(statuses, "status"); status != NULL;
			status = xmlnode_get_next_twin(status))
	{
		GaimSavedStatus *new;
		new = parse_status(status);
		saved_statuses = g_list_prepend(saved_statuses, new);
	}

	xmlnode_free(statuses);
}


/**************************************************************************
* Saved status API
**************************************************************************/
GaimSavedStatus *
gaim_savedstatus_new(const char *title, GaimStatusPrimitive type)
{
	GaimSavedStatus *status;

	/* Make sure we don't already have a saved status with this title. */
	if (title != NULL)
		g_return_val_if_fail(gaim_savedstatus_find(title) == NULL, NULL);

	status = g_new0(GaimSavedStatus, 1);
	status->title = g_strdup(title);
	status->type = type;
	set_creation_time(status, time(NULL));

	saved_statuses = g_list_prepend(saved_statuses, status);

	schedule_save();

	return status;
}

void
gaim_savedstatus_set_title(GaimSavedStatus *status, const char *title)
{
	g_return_if_fail(status != NULL);

	/* Make sure we don't already have a saved status with this title. */
	g_return_if_fail(gaim_savedstatus_find(title) == NULL);

	g_free(status->title);
	status->title = g_strdup(title);

	schedule_save();
}

void
gaim_savedstatus_set_type(GaimSavedStatus *status, GaimStatusPrimitive type)
{
	g_return_if_fail(status != NULL);

	status->type = type;

	schedule_save();
}

void
gaim_savedstatus_set_message(GaimSavedStatus *status, const char *message)
{
	g_return_if_fail(status != NULL);

	g_free(status->message);
	status->message = g_strdup(message);

	schedule_save();
}

void
gaim_savedstatus_set_substatus(GaimSavedStatus *saved_status,
							   const GaimAccount *account,
							   const GaimStatusType *type,
							   const char *message)
{
	GaimSavedStatusSub *substatus;

	g_return_if_fail(saved_status != NULL);
	g_return_if_fail(account      != NULL);
	g_return_if_fail(type         != NULL);

	/* Find an existing substatus or create a new one */
	substatus = gaim_savedstatus_get_substatus(saved_status, account);
	if (substatus == NULL)
	{
		substatus = g_new0(GaimSavedStatusSub, 1);
		substatus->account = (GaimAccount *)account;
		saved_status->substatuses = g_list_prepend(saved_status->substatuses, substatus);
	}

	substatus->type = type;
	g_free(substatus->message);
	substatus->message = g_strdup(message);

	schedule_save();
}

void
gaim_savedstatus_unset_substatus(GaimSavedStatus *saved_status,
								 const GaimAccount *account)
{
	GList *iter;
	GaimSavedStatusSub *substatus;

	g_return_if_fail(saved_status != NULL);
	g_return_if_fail(account      != NULL);

	for (iter = saved_status->substatuses; iter != NULL; iter = iter->next)
	{
		substatus = iter->data;
		if (substatus->account == account)
		{
			saved_status->substatuses = g_list_delete_link(saved_status->substatuses, iter);
			g_free(substatus->message);
			g_free(substatus);
			return;
		}
	}
}

gboolean
gaim_savedstatus_delete(const char *title)
{
	GaimSavedStatus *status;
	time_t creation_time, current, idleaway;

	status = gaim_savedstatus_find(title);

	if (status == NULL)
		return FALSE;

	saved_statuses = g_list_remove(saved_statuses, status);
	creation_time = gaim_savedstatus_get_creation_time(status);
	g_hash_table_remove(creation_times, &creation_time);
	free_statussaved(status);

	schedule_save();

	/*
	 * If we just deleted our current status or our idleaway status,
	 * then set the appropriate pref back to 0.
	 */
	current = gaim_prefs_get_int("/core/savedstatus/current");
	if (current == creation_time)
		gaim_prefs_set_int("/core/savedstatus/current", 0);

	idleaway = gaim_prefs_get_int("/core/savedstatus/idleaway");
	if (idleaway == creation_time)
		gaim_prefs_set_int("/core/savedstatus/idleaway", 0);

	return TRUE;
}

const GList *
gaim_savedstatuses_get_all(void)
{
	return saved_statuses;
}

GaimSavedStatus *
gaim_savedstatus_get_current()
{
	int creation_time;
	GaimSavedStatus *saved_status = NULL;

	creation_time = gaim_prefs_get_int("/core/savedstatus/current");

	if (creation_time != 0)
		saved_status = g_hash_table_lookup(creation_times, &creation_time);

	if (saved_status == NULL)
	{
		/*
		 * We don't have a current saved statuses!  This is either a new
		 * Gaim user or someone upgrading from Gaim 1.5.0 or older, or
		 * possibly someone who deleted the status they were currently
		 * using?  In any case, add a default status.
		 */
		saved_status = gaim_savedstatus_new(NULL, GAIM_STATUS_AVAILABLE);
		gaim_savedstatus_set_message(saved_status, _("Hello!"));
	}

	return saved_status;
}

GaimSavedStatus *
gaim_savedstatus_get_idleaway()
{
	int creation_time;
	GaimSavedStatus *saved_status;

	creation_time = gaim_prefs_get_int("/core/savedstatus/idleaway");

	if (creation_time == 0)
	{
		/*
		 * We don't have a current saved statuses!  This is either a new
		 * Gaim user or someone upgrading from Gaim 1.5.0 or older.  Add
		 * a default status.
		 */
		saved_status = gaim_savedstatus_new(NULL, GAIM_STATUS_AWAY);
		gaim_savedstatus_set_message(saved_status, _("I'm not here right now"));
	}
	else
	{
		saved_status = g_hash_table_lookup(creation_times, &creation_time);
	}

	return saved_status;
}

GaimSavedStatus *
gaim_savedstatus_find(const char *title)
{
	GList *iter;
	GaimSavedStatus *status;

	g_return_val_if_fail(title != NULL, NULL);

	for (iter = saved_statuses; iter != NULL; iter = iter->next)
	{
		status = (GaimSavedStatus *)iter->data;
		if ((status->title != NULL) && !strcmp(status->title, title))
			return status;
	}

	return NULL;
}

gboolean
gaim_savedstatus_is_transient(const GaimSavedStatus *saved_status)
{
	g_return_val_if_fail(saved_status != NULL, TRUE);

	return (saved_status->title == NULL);
}

const char *
gaim_savedstatus_get_title(const GaimSavedStatus *saved_status)
{
	g_return_val_if_fail(saved_status != NULL, NULL);

	return saved_status->title;
}

GaimStatusPrimitive
gaim_savedstatus_get_type(const GaimSavedStatus *saved_status)
{
	g_return_val_if_fail(saved_status != NULL, GAIM_STATUS_OFFLINE);

	return saved_status->type;
}

const char *
gaim_savedstatus_get_message(const GaimSavedStatus *saved_status)
{
	g_return_val_if_fail(saved_status != NULL, NULL);

	return saved_status->message;
}

time_t
gaim_savedstatus_get_creation_time(const GaimSavedStatus *saved_status)
{
	g_return_val_if_fail(saved_status != NULL, 0);

	return saved_status->creation_time;
}

gboolean
gaim_savedstatus_has_substatuses(const GaimSavedStatus *saved_status)
{
	g_return_val_if_fail(saved_status != NULL, FALSE);

	return (saved_status->substatuses != NULL);
}

GaimSavedStatusSub *
gaim_savedstatus_get_substatus(const GaimSavedStatus *saved_status,
							   const GaimAccount *account)
{
	GList *iter;
	GaimSavedStatusSub *substatus;

	g_return_val_if_fail(saved_status != NULL, NULL);
	g_return_val_if_fail(account      != NULL, NULL);

	for (iter = saved_status->substatuses; iter != NULL; iter = iter->next)
	{
		substatus = iter->data;
		if (substatus->account == account)
			return substatus;
	}

	return NULL;
}

const GaimStatusType *
gaim_savedstatus_substatus_get_type(const GaimSavedStatusSub *substatus)
{
	g_return_val_if_fail(substatus != NULL, NULL);

	return substatus->type;
}

const char *
gaim_savedstatus_substatus_get_message(const GaimSavedStatusSub *substatus)
{
	g_return_val_if_fail(substatus != NULL, NULL);

	return substatus->message;
}

void
gaim_savedstatus_activate(GaimSavedStatus *saved_status)
{
	GList *accounts, *node;

	g_return_if_fail(saved_status != NULL);

	accounts = gaim_accounts_get_all_active();

	for (node = accounts; node != NULL; node = node->next)
	{
		GaimAccount *account;

		account = node->data;
		gaim_savedstatus_activate_for_account(saved_status, account);
	}

	g_list_free(accounts);

	/*
	 * TODO: Need to rotate the old status out of here so we
	 *       can keep track of recently used statuses.
	 */
	saved_status->lastused = time(NULL);
	gaim_prefs_set_int("/core/savedstatus/current",
					   gaim_savedstatus_get_creation_time(saved_status));
}

void
gaim_savedstatus_activate_for_account(const GaimSavedStatus *saved_status,
									  GaimAccount *account)
{
	const GaimStatusType *status_type;
	const GaimSavedStatusSub *substatus;
	const char *message = NULL;

	g_return_if_fail(saved_status != NULL);
	g_return_if_fail(account != NULL);

	substatus = gaim_savedstatus_get_substatus(saved_status, account);
	if (substatus != NULL)
	{
		status_type = substatus->type;
		message = substatus->message;
	}
	else
	{
		status_type = gaim_account_get_status_type_with_primitive(account, saved_status->type);
		if (status_type == NULL)
			return;
		message = saved_status->message;
	}

	if ((message != NULL) &&
		(gaim_status_type_get_attr(status_type, "message")))
	{
		gaim_account_set_status(account, gaim_status_type_get_id(status_type),
								TRUE, "message", message, NULL);
	}
	else
	{
		gaim_account_set_status(account, gaim_status_type_get_id(status_type),
								TRUE, NULL);
	}
}

void *
gaim_savedstatuses_get_handle(void)
{
	static int handle;

	return &handle;
}

void
gaim_savedstatuses_init(void)
{
	creation_times = g_hash_table_new(g_int_hash, g_int_equal);

	/*
	 * Using 0 as the creation_time is a special case.
	 * If someone calls gaim_savedstatus_get_current() or
	 * gaim_savedstatus_get_idleaway() and either of those functions
	 * sees a creation_time of 0, then it will create a default
	 * saved status and return that to the user.
	 */
	gaim_prefs_add_none("/core/savedstatus");
	gaim_prefs_add_int("/core/savedstatus/current", 0);
	gaim_prefs_add_int("/core/savedstatus/idleaway", 0);

	load_statuses();
}

void
gaim_savedstatuses_uninit(void)
{
	if (save_timer != 0)
	{
		gaim_timeout_remove(save_timer);
		save_timer = 0;
		sync_statuses();
	}

	while (saved_statuses != NULL) {
		GaimSavedStatus *saved_status = saved_statuses->data;
		saved_statuses = g_list_remove(saved_statuses, saved_status);
		free_statussaved(saved_status);
	}

	g_hash_table_destroy(creation_times);
}

