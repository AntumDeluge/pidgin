/**
 * @file gtksmiley.c GTK+ Smiley Manager API
 * @ingroup pidgin
 */

/*
 * pidgin
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

#include "internal.h"
#include "pidgin.h"

#include "debug.h"
#include "notify.h"
#include "smiley.h"

#include "gtkimhtml.h"
#include "gtksmiley.h"
#include "gtkutils.h"
#include "pidginstock.h"

typedef struct
{
	PurpleSmiley *smiley;
	GtkWidget *parent;
	GtkWidget *smile;
	GtkWidget *smiley_image;
	gchar *filename;
} PidginSmiley;

typedef struct
{
	GtkWidget *window;

	GtkWidget *treeview;
	GtkListStore *model;
} SmileyManager;

enum
{
	ICON,
	SHORTCUT,
	SMILEY,
	N_COL
};

static SmileyManager *smiley_manager = NULL;
static GSList *gtk_smileys = NULL;

static void
pidgin_smiley_destroy(PidginSmiley *smiley)
{
	gtk_widget_destroy(smiley->parent);
	g_free(smiley->filename);
	g_free(smiley);
}

/******************************************************************************
 * GtkIMHtmlSmileys stuff
 *****************************************************************************/
/* Perhaps these should be in gtkimhtml.c instead. -- sadrul */
static void add_gtkimhtml_to_list(GtkIMHtmlSmiley *gtksmiley)
{
	gtk_smileys = g_slist_prepend(gtk_smileys, gtksmiley);

	purple_debug_info("gtksmiley", "adding %s to gtk_smileys\n", gtksmiley->smile);
}

static void
shortcut_changed_cb(PurpleSmiley *smiley, gpointer dontcare, GtkIMHtmlSmiley *gtksmiley)
{
	g_free(gtksmiley->smile);
	gtksmiley->smile = g_strdup(purple_smiley_get_shortcut(smiley));
}

static GtkIMHtmlSmiley *smiley_purple_to_gtkimhtml(PurpleSmiley *smiley)
{
	GtkIMHtmlSmiley *gtksmiley;
	gchar *filename;
	const gchar *file;

	file = purple_imgstore_get_filename(purple_smiley_get_stored_image(smiley));

	filename = g_build_filename(purple_smileys_get_storing_dir(), file, NULL);

	gtksmiley = gtk_imhtml_smiley_create(filename, purple_smiley_get_shortcut(smiley),
			FALSE, GTK_IMHTML_SMILEY_CUSTOM);
	g_free(filename);

	/* Make sure the shortcut for the GtkIMHtmlSmiley is updated with the PurpleSmiley */
	g_signal_connect(G_OBJECT(smiley), "notify::shortcut",
			G_CALLBACK(shortcut_changed_cb), gtksmiley);

	return gtksmiley;
}

void pidgin_smiley_del_from_list(PurpleSmiley *smiley)
{
	GSList *list = NULL;
	GtkIMHtmlSmiley *gtksmiley;

	if (gtk_smileys == NULL)
		return;

	list = gtk_smileys;

	for (; list; list = list->next) {
		gtksmiley = (GtkIMHtmlSmiley*)list->data;

		if (strcmp(gtksmiley->smile, purple_smiley_get_shortcut(smiley)))
			continue;

		gtk_imhtml_smiley_destroy(gtksmiley);
		g_signal_handlers_disconnect_matched(G_OBJECT(smiley), G_SIGNAL_MATCH_DATA,
				0, 0, NULL, NULL, gtksmiley);
		break;
	}

	if (list)
		gtk_smileys = g_slist_delete_link(gtk_smileys, list);
}

void pidgin_smiley_add_to_list(PurpleSmiley *smiley)
{
	GtkIMHtmlSmiley *gtksmiley;

	gtksmiley = smiley_purple_to_gtkimhtml(smiley);
	add_gtkimhtml_to_list(gtksmiley);
	g_signal_connect(G_OBJECT(smiley), "destroy", G_CALLBACK(pidgin_smiley_del_from_list), NULL);
}

void pidgin_smileys_init(void)
{
	GList *smileys;
	PurpleSmiley *smiley;

	if (gtk_smileys != NULL)
		return;

	smileys = purple_smileys_get_all();

	for (; smileys; smileys = g_list_delete_link(smileys, smileys)) {
		smiley = (PurpleSmiley*)smileys->data;

		pidgin_smiley_add_to_list(smiley);
	}
}

void pidgin_smileys_uninit(void)
{
	GSList *list;
	GtkIMHtmlSmiley *gtksmiley;

	list = gtk_smileys;

	if (list == NULL)
		return;

	for (; list; list = g_slist_delete_link(list, list)) {
		gtksmiley = (GtkIMHtmlSmiley*)list->data;
		gtk_imhtml_smiley_destroy(gtksmiley);
	}

	gtk_smileys = NULL;
}

GSList *pidgin_smileys_get_all(void)
{
	return gtk_smileys;
}

/******************************************************************************
 * Manager stuff
 *****************************************************************************/

static void refresh_list(void);

/******************************************************************************
 * The Add dialog
 ******************************************************************************/

static void do_add(GtkWidget *widget, PidginSmiley *s)
{
	const gchar *entry;
	PurpleSmiley *emoticon;

	entry = gtk_entry_get_text(GTK_ENTRY(s->smile));
	emoticon = purple_smileys_find_by_shortcut(entry);
	if (emoticon && emoticon != s->smiley) {
		purple_notify_error(s->parent, _("Custom Smiley"),
				_("Duplicate Shortcut"),
				_("A custom smiley for the selected shortcut already exists. Please specify a different shortcut."));
		return;
	}

	if (s->smiley) {
		if (s->filename) {
			gchar *data = NULL;
			size_t len;
			GError *err = NULL;

			if (!g_file_get_contents(s->filename, &data, &len, &err)) {
				purple_debug_error("gtksmiley", "Error reading %s: %s\n",
						s->filename, err->message);
				g_error_free(err);

				return;
			}
			purple_smiley_set_data(s->smiley, (guchar*)data, len, FALSE);
		}
		purple_smiley_set_shortcut(s->smiley, entry);
	} else {
		if ((s->filename == NULL || *entry == 0)) {
			purple_notify_error(s->parent, _("Custom Smiley"),
					_("More Data needed"),
					s->filename ? _("Please provide a shortcut to associate with the smiley.")
					: _("Please select an image for the smiley."));
			return;
		}

		purple_debug_info("gtksmiley", "adding a new smiley\n");
		emoticon = purple_smiley_new_from_file(entry, s->filename);
		pidgin_smiley_add_to_list(emoticon);
	}

	if (smiley_manager != NULL)
		refresh_list();

	gtk_widget_destroy(s->parent);
}

static void do_add_select_cb(GtkWidget *widget, gint resp, PidginSmiley *s)
{
	switch (resp) {
		case GTK_RESPONSE_ACCEPT:
			do_add(widget, s);
			break;
		case GTK_RESPONSE_DELETE_EVENT:
		case GTK_RESPONSE_CANCEL:
			gtk_widget_destroy(s->parent);
			break;
		default:
			purple_debug_error("gtksmiley", "no valid response\n");
			break;
	}
}

static void do_add_file_cb(const char *filename, gpointer data)
{
	PidginSmiley *s = data;
	GdkPixbuf *pixbuf;

	if (!filename)
		return;

	g_free(s->filename);
	s->filename = g_strdup(filename);
	pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 64, 64, FALSE, NULL);
	gtk_image_set_from_pixbuf(GTK_IMAGE(s->smiley_image), pixbuf);
	if (pixbuf)
		gdk_pixbuf_unref(pixbuf);
	gtk_widget_grab_focus(s->smile);
}

static void
open_image_selector(GtkWidget *widget, PidginSmiley *psmiley)
{
	GtkWidget *file_chooser;
	file_chooser = pidgin_buddy_icon_chooser_new(GTK_WINDOW(gtk_widget_get_toplevel(widget)),
			do_add_file_cb, psmiley);
	gtk_window_set_title(GTK_WINDOW(file_chooser), _("Custom Smiley"));
	gtk_window_set_role(GTK_WINDOW(file_chooser), "file-selector-custom-smiley");
	gtk_widget_show_all(file_chooser);
}

void pidgin_smiley_edit(GtkWidget *widget, PurpleSmiley *smiley)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *filech;
	GtkWidget *window;
	GdkPixbuf *pixbuf = NULL;
	PurpleStoredImage *stored_img;

	PidginSmiley *s = g_new0(PidginSmiley, 1);
	s->smiley = smiley;

	window = gtk_dialog_new_with_buttons(smiley ? _("Edit Smiley") : _("Add Smiley"),
			GTK_WINDOW(widget),
			GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
			smiley ? GTK_STOCK_SAVE : GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			NULL);
	s->parent = window;

	gtk_container_set_border_width(GTK_CONTAINER(window), PIDGIN_HIG_BORDER);

	g_signal_connect(window, "response", G_CALLBACK(do_add_select_cb), s);

	/* The vbox */
	vbox = gtk_vbox_new(FALSE, PIDGIN_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(window)->vbox), vbox);
	gtk_widget_show(vbox);

	/* The hbox */
	hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_VBOX(vbox)), hbox);

	label = gtk_label_new_with_mnemonic(_("Smiley _Image"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	filech = gtk_button_new();
	gtk_box_pack_end(GTK_BOX(hbox), filech, FALSE, FALSE, 0);
	pidgin_set_accessible_label(filech, label);

	s->smiley_image = gtk_image_new();
	gtk_container_add(GTK_CONTAINER(filech), s->smiley_image);
	if (smiley && (stored_img = purple_smiley_get_stored_image(smiley))) {
		pixbuf = pidgin_pixbuf_from_imgstore(stored_img);
		purple_imgstore_unref(stored_img);
	} else {
		GtkIconSize icon_size = gtk_icon_size_from_name(PIDGIN_ICON_SIZE_TANGO_SMALL);
		pixbuf = gtk_widget_render_icon(window, PIDGIN_STOCK_TOOLBAR_SELECT_AVATAR,
				icon_size, "PidginSmiley");
	}

	gtk_image_set_from_pixbuf(GTK_IMAGE(s->smiley_image), pixbuf);
	if (pixbuf != NULL)
		g_object_unref(G_OBJECT(pixbuf));
	g_signal_connect(G_OBJECT(filech), "clicked", G_CALLBACK(open_image_selector), s);

	gtk_widget_show_all(hbox);

	/* info */
	hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_VBOX(vbox)),hbox);

	/* Smiley shortcut */
	label = gtk_label_new_with_mnemonic(_("Smiley S_hortcut"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	s->smile = gtk_entry_new();
	gtk_entry_set_activates_default(GTK_ENTRY(s->smile), TRUE);
	pidgin_set_accessible_label(s->smile, label);
	if (smiley)
		gtk_entry_set_text(GTK_ENTRY(s->smile), purple_smiley_get_shortcut(smiley));

	g_signal_connect(s->smile, "activate", G_CALLBACK(do_add), s);

	gtk_box_pack_end(GTK_BOX(hbox), s->smile, FALSE, FALSE, 0);
	gtk_widget_show(s->smile);

	gtk_widget_show(hbox);

	gtk_widget_show(GTK_WIDGET(window));
	g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(pidgin_smiley_destroy), s);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(purple_notify_close_with_handle), s);
}

/******************************************************************************
 * Delete smiley
 *****************************************************************************/
static void delete_foreach(GtkTreeModel *model, GtkTreePath *path,
		GtkTreeIter *iter, gpointer data)
{
	PurpleSmiley *smiley = NULL;
	SmileyManager *dialog;

	dialog = (SmileyManager*)data;

	gtk_tree_model_get(model, iter,
			SMILEY, &smiley,
			-1);

	if(smiley != NULL) {
		g_object_unref(G_OBJECT(smiley));
		pidgin_smiley_del_from_list(smiley);
		purple_smiley_delete(smiley);
	}
}

static void append_to_list(GtkTreeModel *model, GtkTreePath *path,
		GtkTreeIter *iter, gpointer data)
{
	GList **list = data;
	*list = g_list_prepend(*list, gtk_tree_path_copy(path));
}

static void smiley_delete(SmileyManager *dialog)
{
	GtkTreeSelection *selection;
	GList *list = NULL;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dialog->treeview));
	gtk_tree_selection_selected_foreach(selection, delete_foreach, dialog);
	gtk_tree_selection_selected_foreach(selection, append_to_list, &list);

	while (list) {
		GtkTreeIter iter;
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(dialog->model), &iter, list->data))
			gtk_list_store_remove(GTK_LIST_STORE(dialog->model), &iter);
		gtk_tree_path_free(list->data);
		list = g_list_delete_link(list, list);
	}
}
/******************************************************************************
 * The Smiley Manager
 *****************************************************************************/
static void add_columns(GtkWidget *treeview, SmileyManager *dialog)
{
	GtkCellRenderer *rend;
	GtkTreeViewColumn *column;

	/* Icon */
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Smiley"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	rend = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(column, rend, FALSE);
	gtk_tree_view_column_add_attribute(column, rend, "pixbuf", ICON);

	/* Shortcut */
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Shortcut"));
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, rend, TRUE);
	gtk_tree_view_column_add_attribute(column, rend, "text", SHORTCUT);
}

static void store_smiley_add(PurpleSmiley *smiley)
{
	GtkTreeIter iter;
	PurpleStoredImage *img;
	GdkPixbuf *sized_smiley = NULL;

	if (smiley_manager == NULL)
		return;

	img = purple_smiley_get_stored_image(smiley);

	if (img != NULL) {
		GdkPixbuf *smiley_image = pidgin_pixbuf_from_imgstore(img);
		purple_imgstore_unref(img);

		if (smiley_image != NULL)
			sized_smiley = gdk_pixbuf_scale_simple(smiley_image,
					22, 22, GDK_INTERP_HYPER);
		g_object_unref(G_OBJECT(smiley_image));
	}


	gtk_list_store_append(smiley_manager->model, &iter);

	gtk_list_store_set(smiley_manager->model, &iter,
			ICON, sized_smiley,
			SHORTCUT, purple_smiley_get_shortcut(smiley),
			SMILEY, smiley,
			-1);

	if (sized_smiley != NULL)
		g_object_unref(G_OBJECT(sized_smiley));
}

static void populate_smiley_list(SmileyManager *dialog)
{
	GList *list;
	PurpleSmiley *emoticon;

	gtk_list_store_clear(dialog->model);

	for(list = purple_smileys_get_all(); list != NULL;
			list = g_list_delete_link(list, list)) {
		emoticon = (PurpleSmiley*)list->data;

		store_smiley_add(emoticon);
	}
}

static void smile_selected_cb(GtkTreeSelection *sel, SmileyManager *dialog)
{
	gint selected;

	selected = gtk_tree_selection_count_selected_rows(sel);

	gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog->window),
			GTK_RESPONSE_NO, selected > 0);
}

static void smiley_edit_cb(GtkTreeView *treeview, GtkTreePath *path, GtkTreeViewColumn *col, gpointer data)
{
	GtkTreeIter iter;
	SmileyManager *dialog = data;
	PurpleSmiley *smiley = NULL;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(dialog->model), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(dialog->model), &iter, SMILEY, &smiley, -1);
	pidgin_smiley_edit(gtk_widget_get_toplevel(GTK_WIDGET(treeview)), smiley);
	g_object_unref(G_OBJECT(smiley));
}

static GtkWidget *smiley_list_create(SmileyManager *dialog)
{
	GtkWidget *sw;
	GtkWidget *treeview;
	GtkTreeSelection *sel;

	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
			GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
			GTK_SHADOW_IN);
	gtk_widget_show(sw);

	/* Create the list model */
	dialog->model = gtk_list_store_new(N_COL,
			GDK_TYPE_PIXBUF,	/* ICON */
			G_TYPE_STRING,		/* SHORTCUT */
			G_TYPE_OBJECT		/* SMILEY */
			);

	/* the actual treeview */
	treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dialog->model));
	dialog->treeview = treeview;
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(treeview), TRUE);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(dialog->model), SHORTCUT, GTK_SORT_ASCENDING);
	g_object_unref(G_OBJECT(dialog->model));

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
	gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
	gtk_container_add(GTK_CONTAINER(sw), treeview);

	g_signal_connect(G_OBJECT(sel), "changed", G_CALLBACK(smile_selected_cb), dialog);
	g_signal_connect(G_OBJECT(treeview), "row_activated", G_CALLBACK(smiley_edit_cb), dialog);

	gtk_widget_show(treeview);

	add_columns(treeview, dialog);
	populate_smiley_list(dialog);

	return sw;
}

static void refresh_list()
{
	populate_smiley_list(smiley_manager);
}

static void smiley_manager_select_cb(GtkWidget *widget, gint resp, SmileyManager *dialog)
{
	switch (resp) {
		case GTK_RESPONSE_YES:
			pidgin_smiley_edit(dialog->window, NULL);
			break;
		case GTK_RESPONSE_NO:
			smiley_delete(dialog);
			break;
		case GTK_RESPONSE_DELETE_EVENT:
		case GTK_RESPONSE_CLOSE:
			gtk_widget_destroy(dialog->window);
			g_free(smiley_manager);
			smiley_manager = NULL;
			break;
		default:
			purple_debug_info("gtksmiley", "No valid selection\n");
			break;
	}
}

void pidgin_smiley_manager_show(void)
{
	SmileyManager *dialog;
	GtkWidget *win;
	GtkWidget *sw;
	GtkWidget *vbox;

	if (smiley_manager) {
		gtk_window_present(GTK_WINDOW(smiley_manager->window));
		return;
	}

	dialog = g_new0(SmileyManager, 1);
	smiley_manager = dialog;

	dialog->window = win = gtk_dialog_new_with_buttons(
			_("Custom Smiley Manager"),
			NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_STOCK_ADD, GTK_RESPONSE_YES,
			GTK_STOCK_DELETE, GTK_RESPONSE_NO,
			GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
			NULL);

	gtk_window_set_default_size(GTK_WINDOW(win), 50, 400);
	gtk_window_set_role(GTK_WINDOW(win), "custom_smiley_manager");
	gtk_container_set_border_width(GTK_CONTAINER(win),PIDGIN_HIG_BORDER);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(win), GTK_RESPONSE_NO, FALSE);

	g_signal_connect(win, "response", G_CALLBACK(smiley_manager_select_cb),
			dialog);

	/* The vbox */
	vbox = gtk_vbox_new(FALSE, PIDGIN_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(win)->vbox), vbox);
	gtk_widget_show(vbox);

	/* get the scrolled window with all stuff */
	sw = smiley_list_create(dialog);
	gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
	gtk_widget_show(sw);

	gtk_widget_show(win);
}
