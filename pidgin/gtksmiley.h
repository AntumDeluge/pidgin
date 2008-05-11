/**
 * @file gtksmiley.h GTK+ Custom Smiley API
 * @ingroup pidgin
 */

/* pidgin
 *
 * Pidgin is the legal property of its developers, whose names are too numerous
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

#ifndef _PIDGIN_GTKSMILEY_H_
#define _PIDGIN_GTKSMILEY_H_

#include "smiley.h"

/**
 * Add a PurpleSmiley to the GtkIMHtmlSmiley's list to be able to use it
 * in pidgin
 *
 * @param smiley	The smiley to be added.
 */
void pidgin_smiley_add_to_list(PurpleSmiley *smiley);

/**
 * Delete a PurpleSmiley from the GtkIMHtmlSmiley's list
 *
 * @param smiley	The smiley to be deleted.
 */
void pidgin_smiley_del_from_list(PurpleSmiley *smiley);

/**
 * Load the GtkIMHtml list
 */
void pidgin_smileys_init(void);

/**
 * Uninit the GtkIMHtml list
 */
void pidgin_smileys_uninit(void);

/**
 * Returns a GSList with the GtkIMHtmlSmiley of each custom smiley
 *
 * @constreturn A GtkIMHmlSmiley list
 */
GSList* pidgin_smileys_get_all(void);

/******************************************************************************
 * Smiley Manager
 *****************************************************************************/
/**
 * Displays the Smiley Manager Window
 */
void pidgin_smiley_manager_show(void);

/**
 * Shows an editor for a smiley.
 *
 * @param widget	The parent widget to be linked or @c NULL
 * @param smiley    The PurpleSmiley to be edited, or @c NULL for a new smiley
 */
void pidgin_smiley_edit(GtkWidget *widget, PurpleSmiley *smiley);

#endif /* _PIDGIN_GTKSMILEY_H_*/