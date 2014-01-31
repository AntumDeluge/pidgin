/**
 * @file keyring.h Keyring API
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301 USA
 */

#ifndef _PURPLE_KEYRING_H_
#define _PURPLE_KEYRING_H_

#include "account.h"
#include "request.h"

#define PURPLE_TYPE_KEYRING  (purple_keyring_get_type())

/**
 * Default keyring ID.
 */
#define PURPLE_DEFAULT_KEYRING "keyring-internal"

/**
 * Keyring subsystem error domain.
 */
#define PURPLE_KEYRING_ERROR purple_keyring_error_domain()

/**************************************************************************/
/** @name Data structures and types                                       */
/**************************************************************************/
/*@{*/

typedef struct _PurpleKeyring PurpleKeyring;

/*@}*/

/**************************************************************************/
/** @name Callbacks for keyrings access functions                         */
/**************************************************************************/
/*@{*/

/**
 * Callback for once a password is read.
 *
 * If there was a problem, the password will be NULL, and the error set.
 *
 * @account:  The account.
 * @password: The password.
 * @error:    Error that may have occurred.
 * @data:     Data passed to the callback.
 */
typedef void (*PurpleKeyringReadCallback)(PurpleAccount *account,
	const gchar *password, GError *error, gpointer data);

/**
 * Callback for once a password has been stored.
 *
 * If there was a problem, the error will be set.
 *
 * @account: The account.
 * @error:   Error that may have occurred.
 * @data:    Data passed to the callback.
 */
typedef void (*PurpleKeyringSaveCallback)(PurpleAccount *account, GError *error,
	gpointer data);

/**
 * Callback for once the master password for a keyring has been changed.
 *
 * @error:  Error that has occurred.
 * @data:   Data passed to the callback.
 */
typedef void (*PurpleKeyringChangeMasterCallback)(GError *error, gpointer data);

/**
 * Callback for when we change the keyring.
 *
 * @error:   An error that might have occurred.
 * @data:    A pointer to user supplied data.
 */
typedef void (*PurpleKeyringSetInUseCallback)(GError *error, gpointer data);

/*@}*/

/**************************************************************************/
/** @name Keyrings access functions                                       */
/**************************************************************************/
/*@{*/

/**
 * Read the password for an account.
 *
 * @account: The account.
 * @cb:      A callback for once the password is found.
 * @data:    Data to be passed to the callback.
 */
typedef void (*PurpleKeyringRead)(PurpleAccount *account,
	PurpleKeyringReadCallback cb, gpointer data);

/**
 * Store a password in the keyring.
 *
 * @account:  The account.
 * @password: The password to be stored. If the password is NULL, this
 *                 means that the keyring should forget about that password.
 * @cb:       A callback for once the password is saved.
 * @data:     Data to be passed to the callback.
 */
typedef void (*PurpleKeyringSave)(PurpleAccount *account, const gchar *password,
	PurpleKeyringSaveCallback cb, gpointer data);

/**
 * Cancel all running requests.
 *
 * After calling that, all queued requests should run their callbacks (most
 * probably, with failure result).
 */
typedef void (*PurpleKeyringCancelRequests)(void);

/**
 * Close the keyring.
 *
 * This will be called so the keyring can do any cleanup it needs.
 */
typedef void (*PurpleKeyringClose)(void);

/**
 * Import serialized (and maybe encrypted) password.
 *
 * This is not async because it is not meant to prompt for a master password and
 * decrypt passwords.
 *
 * @account: The account.
 * @mode:    A keyring specific option that was stored. Can be NULL.
 * @data:    Data that was stored. Can be NULL.
 *
 * Returns: TRUE on success, FALSE on failure.
 */
typedef gboolean (*PurpleKeyringImportPassword)(PurpleAccount *account,
	const gchar *mode, const gchar *data, GError **error);

/**
 * Export serialized (and maybe encrypted) password.
 *
 * @account: The account.
 * @mode:    An option field that can be used by the plugin. This is
 *                expected to be a static string.
 * @data:    The data to be stored in the XML node. This string will be
 *                freed using destroy() once not needed anymore.
 * @error:   Will be set if a problem occured.
 * @destroy: A function to be called, if non NULL, to free data.
 *
 * Returns: TRUE on success, FALSE on failure.
 */
typedef gboolean (*PurpleKeyringExportPassword)(PurpleAccount *account,
	const gchar **mode, gchar **data, GError **error,
	GDestroyNotify *destroy);

/**
 * Read keyring settings.
 *
 * Returns: New copy of current settings (must be free'd with
 *         purple_request_fields_destroy).
 */
typedef PurpleRequestFields * (*PurpleKeyringReadSettings)(void);

/**
 * Applies modified keyring settings.
 *
 * @notify_handle: A handle that can be passed to purple_notify_message.
 * @fields:        Modified settings (originally taken from
 *                      PurpleKeyringReadSettings).
 * Returns: TRUE, if succeeded, FALSE otherwise.
 */
typedef gboolean (*PurpleKeyringApplySettings)(void *notify_handle,
	PurpleRequestFields *fields);

/*@}*/

G_BEGIN_DECLS

/**************************************************************************/
/** @name Setting used keyrings                                           */
/**************************************************************************/
/*@{*/

/**
 * Find a keyring by an id.
 *
 * @id: The id for the keyring.
 *
 * Returns: The keyring, or NULL if not found.
 */
PurpleKeyring *
purple_keyring_find_keyring_by_id(const gchar *id);

/**
 * Get the keyring being used.
 */
PurpleKeyring *
purple_keyring_get_inuse(void);

/**
 * Set the keyring to use. This function will move all passwords from
 * the old keyring to the new one.
 *
 * If it fails, it will cancel all changes, close the new keyring, and notify
 * the callback. If it succeeds, it will remove all passwords from the old safe
 * and close that safe.
 *
 * @newkeyring: The new keyring to use.
 * @force:      FALSE if the change can be cancelled. If this is TRUE and
 *                   an error occurs, data might be lost.
 * @cb:         A callback for once the change is complete.
 * @data:       Data to be passed to the callback.
 */
void
purple_keyring_set_inuse(PurpleKeyring *newkeyring, gboolean force,
	PurpleKeyringSetInUseCallback cb, gpointer data);

/**
 * Register a keyring plugin.
 *
 * @keyring: The keyring to register.
 */
void
purple_keyring_register(PurpleKeyring *keyring);

/**
 * Unregister a keyring plugin.
 *
 * In case the keyring is in use, passwords will be moved to a fallback safe,
 * and the keyring to unregister will be properly closed.
 *
 * @keyring: The keyring to unregister.
 */
void
purple_keyring_unregister(PurpleKeyring *keyring);

/**
 * Returns a GList containing the IDs and names of the registered
 * keyrings.
 *
 * Returns: The list of IDs and names.
 */
GList *
purple_keyring_get_options(void);

/*@}*/

/**************************************************************************/
/** @name Keyring plugin wrappers                                         */
/**************************************************************************/
/*@{*/

/**
 * Import serialized (and maybe encrypted) password into current keyring.
 *
 * It's used by account.c while reading a password from xml.
 *
 * @account:    The account.
 * @keyring_id: The plugin ID that was stored in the xml file. Can be NULL.
 * @mode:       A keyring specific option that was stored. Can be NULL.
 * @data:       Data that was stored, can be NULL.
 *
 * Returns: TRUE if the input was accepted, FALSE otherwise.
 */
gboolean
purple_keyring_import_password(PurpleAccount *account, const gchar *keyring_id,
	const gchar *mode, const gchar *data, GError **error);

/**
 * Export serialized (and maybe encrypted) password out of current keyring.
 *
 * It's used by account.c while syncing accounts to xml.
 *
 * @account:    The account for which we want the info.
 * @keyring_id: The plugin id to be stored in the XML node. This will be
 *                   NULL or a string that can be considered static.
 * @mode:       An option field that can be used by the plugin. This will
 *                   be NULL or a string that can be considered static.
 * @data:       The data to be stored in the XML node. This string must be
 *                   freed using destroy() once not needed anymore if it is not
 *                   NULL.
 * @error:      Will be set if a problem occured.
 * @destroy:    A function to be called, if non NULL, to free data.
 *
 * Returns: TRUE if the info was exported successfully, FALSE otherwise.
 */
gboolean
purple_keyring_export_password(PurpleAccount *account, const gchar **keyring_id,
	const gchar **mode, gchar **data, GError **error,
	GDestroyNotify *destroy);

/**
 * Read a password from the current keyring.
 *
 * @account: The account.
 * @cb:      A callback for once the password is read.
 * @data:    Data passed to the callback.
 */
void
purple_keyring_get_password(PurpleAccount *account,
	PurpleKeyringReadCallback cb, gpointer data);

/**
 * Save a password to the current keyring.
 *
 * @account:  The account.
 * @password: The password to save.
 * @cb:       A callback for once the password is saved.
 * @data:     Data to be passed to the callback.
 */
void
purple_keyring_set_password(PurpleAccount *account, const gchar *password,
	PurpleKeyringSaveCallback cb, gpointer data);

/**
 * Reads settings from current keyring.
 *
 * Returns: New copy of current settings (must be free'd with
 *         purple_request_fields_destroy).
 */
PurpleRequestFields *
purple_keyring_read_settings(void);

/**
 * Applies modified settings to current keyring.
 *
 * @notify_handle: A handle that can be passed to purple_notify_message.
 * @fields:        Modified settings (originally taken from
 *                      PurpleKeyringReadSettings).
 * Returns: TRUE, if succeeded, FALSE otherwise.
 */
gboolean
purple_keyring_apply_settings(void *notify_handle, PurpleRequestFields *fields);

/*@}*/

/**************************************************************************/
/** @name PurpleKeyring accessors                                         */
/**************************************************************************/
/*@{*/

/**
 * Returns the GType for the PurpleKeyring boxed structure.
 */
GType purple_keyring_get_type(void);

/**
 * Creates a new keyring wrapper.
 */
PurpleKeyring *
purple_keyring_new(void);

/**
 * Frees all data allocated with purple_keyring_new.
 *
 * @keyring: Keyring wrapper struct.
 */
void
purple_keyring_free(PurpleKeyring *keyring);

/**
 * Gets friendly user name.
 *
 * @keyring: The keyring.
 * Returns: Friendly user name.
 */
const gchar *
purple_keyring_get_name(const PurpleKeyring *keyring);

/**
 * Gets keyring ID.
 *
 * @keyring: The keyring.
 * Returns: Keyring ID.
 */
const gchar *
purple_keyring_get_id(const PurpleKeyring *keyring);

PurpleKeyringRead
purple_keyring_get_read_password(const PurpleKeyring *keyring);

PurpleKeyringSave
purple_keyring_get_save_password(const PurpleKeyring *keyring);

PurpleKeyringCancelRequests
purple_keyring_get_cancel_requests(const PurpleKeyring *keyring);

PurpleKeyringClose
purple_keyring_get_close_keyring(const PurpleKeyring *keyring);

PurpleKeyringImportPassword
purple_keyring_get_import_password(const PurpleKeyring *keyring);

PurpleKeyringExportPassword
purple_keyring_get_export_password(const PurpleKeyring *keyring);

PurpleKeyringReadSettings
purple_keyring_get_read_settings(const PurpleKeyring *keyring);

PurpleKeyringApplySettings
purple_keyring_get_apply_settings(const PurpleKeyring *keyring);

/**
 * Sets friendly user name.
 *
 * This field is required.
 *
 * @keyring: The keyring.
 * @name:    Friendly user name.
 */
void
purple_keyring_set_name(PurpleKeyring *keyring, const gchar *name);

/**
 * Sets keyring ID.
 *
 * This field is required.
 *
 * @keyring: The keyring.
 * @name:    Keyring ID.
 */
void
purple_keyring_set_id(PurpleKeyring *keyring, const gchar *id);

/**
 * Sets read password method.
 *
 * This field is required.
 *
 * @keyring: The keyring.
 * @read_cb: Read password method.
 */
void
purple_keyring_set_read_password(PurpleKeyring *keyring,
	PurpleKeyringRead read_cb);

/**
 * Sets save password method.
 *
 * This field is required.
 *
 * @keyring: The keyring.
 * @save_cb: Save password method.
 */
void
purple_keyring_set_save_password(PurpleKeyring *keyring,
	PurpleKeyringSave save_cb);

void
purple_keyring_set_cancel_requests(PurpleKeyring *keyring,
	PurpleKeyringCancelRequests cancel_requests);

void
purple_keyring_set_close_keyring(PurpleKeyring *keyring,
	PurpleKeyringClose close_cb);

void
purple_keyring_set_import_password(PurpleKeyring *keyring,
	PurpleKeyringImportPassword import_password);

void
purple_keyring_set_export_password(PurpleKeyring *keyring,
	PurpleKeyringExportPassword export_password);

void
purple_keyring_set_read_settings(PurpleKeyring *keyring,
PurpleKeyringReadSettings read_settings);

void
purple_keyring_set_apply_settings(PurpleKeyring *keyring,
PurpleKeyringApplySettings apply_settings);

/*@}*/

/**************************************************************************/
/** @name Error Codes                                                     */
/**************************************************************************/
/*@{*/

/**
 * Gets keyring subsystem error domain.
 *
 * Returns: keyring subsystem error domain.
 */
GQuark
purple_keyring_error_domain(void);

/**
 * Error codes for keyring subsystem.
 */
enum PurpleKeyringError
{
	PURPLE_KEYRING_ERROR_UNKNOWN = 0,     /**< Unknown error. */

	PURPLE_KEYRING_ERROR_NOKEYRING = 10,  /**< No keyring configured. */
	PURPLE_KEYRING_ERROR_INTERNAL,        /**< Internal keyring system error. */
	PURPLE_KEYRING_ERROR_BACKENDFAIL,     /**< Failed to communicate with the backend or internal backend error. */

	PURPLE_KEYRING_ERROR_NOPASSWORD = 20, /**< No password stored for the specified account. */
	PURPLE_KEYRING_ERROR_ACCESSDENIED,    /**< Access denied for the specified keyring or entry. */
	PURPLE_KEYRING_ERROR_CANCELLED        /**< Operation was cancelled. */
};

/*}@*/

/**************************************************************************/
/** @name Keyring Subsystem                                               */
/**************************************************************************/
/*@{*/

/**
 * Initializes the keyring subsystem.
 */
void
purple_keyring_init(void);

/**
 * Uninitializes the keyring subsystem.
 */
void
purple_keyring_uninit(void);

/**
 * Returns the keyring subsystem handle.
 *
 * Returns: The keyring subsystem handle.
 */
void *
purple_keyring_get_handle(void);

/*}@*/

G_END_DECLS

#endif /* _PURPLE_KEYRING_H_ */