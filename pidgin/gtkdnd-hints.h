/**
 * @file gtkdnd-hints.h GTK+ Drag-and-Drop arrow hints
 * @ingroup pidgin
 */

/* Pidgin is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301, USA.
 */
#ifndef _PIDGIN_DND_HINTS_H_
#define _PIDGIN_DND_HINTS_H_

#include <glib.h>
#include <gtk/gtk.h>

/**
 * Conversation drag-and-drop arrow types.
 */
typedef enum
{
	HINT_ARROW_UP,    /**< Up arrow.    */
	HINT_ARROW_DOWN,  /**< Down arrow.  */
	HINT_ARROW_LEFT,  /**< Left arrow.  */
	HINT_ARROW_RIGHT  /**< Right arrow. */

} DndHintWindowId;

/**
 * Conversation drag-and-drop arrow positions.
 */
typedef enum {

	HINT_POSITION_RIGHT,  /**< Position to the right of a tab.  */
	HINT_POSITION_LEFT,   /**< Position to the left of a tab.   */
	HINT_POSITION_TOP,    /**< Position above a tab.            */
	HINT_POSITION_BOTTOM, /**< Position below a tab.            */
	HINT_POSITION_CENTER  /**< Position in the center of a tab. */

} DndHintPosition;

G_BEGIN_DECLS

/**
 * Shows a drag-and-drop hint at the specified location.
 *
 * @id: The ID of the hint to show.
 * @param x  The X location to show it at.
 * @param y  The Y location to show it at.
 */
void dnd_hints_show(DndHintWindowId id, gint x, gint y);

/**
 * Hides the specified drag-and-drop hint.
 *
 * @id: The ID of the hint to hide.
 */
void dnd_hints_hide(DndHintWindowId id);

/**
 * Hides all drag-and-drop hints.
 */
void dnd_hints_hide_all(void);

/**
 * Shows a drag-and-drop hint relative to a widget.
 *
 * @id:     The ID of the hint.
 * @widget: The widget that the hint is relative to.
 * @horiz:  The horizontal relative position.
 * @vert:   The vertical relative position.
 */
void dnd_hints_show_relative(DndHintWindowId id, GtkWidget *widget,
							 DndHintPosition horiz, DndHintPosition vert);

G_END_DECLS

#endif /* _PIDGIN_DND_HINTS_H_ */
