/**
 * @file purplerc.c Purple gtk resource control plugin.
 *
 * Copyright (C) 2005 Etan Reisner <deryni@eden.rutgers.edu>
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
#include "gtkplugin.h"
#include "gtkprefs.h"
#include "gtkutils.h"
#include "util.h"
#include "version.h"

static guint pref_callback;

static const char *color_prefs[] = {
	"/plugins/gtk/purplerc/color/GtkWidget::cursor-color",
	"/plugins/gtk/purplerc/color/GtkWidget::secondary-cursor-color",
	"/plugins/gtk/purplerc/color/GtkIMHtml::hyperlink-color"
};
static const char *color_prefs_set[] = {
	"/plugins/gtk/purplerc/set/color/GtkWidget::cursor-color",
	"/plugins/gtk/purplerc/set/color/GtkWidget::secondary-cursor-color",
	"/plugins/gtk/purplerc/set/color/GtkIMHtml::hyperlink-color"
};
static const char *color_names[] = {
	N_("Cursor Color"),
	N_("Secondary Cursor Color"),
	N_("Hyperlink Color")
};
static GtkWidget *color_widgets[G_N_ELEMENTS(color_prefs)];

static const char *widget_size_prefs[] = {
	"/plugins/gtk/purplerc/size/GtkTreeView::expander_size",
	"/plugins/gtk/purplerc/size/GtkTreeView::horizontal_separator"
};
static const char *widget_size_prefs_set[] = {
	"/plugins/gtk/purplerc/set/size/GtkTreeView::expander_size",
	"/plugins/gtk/purplerc/set/size/GtkTreeView::horizontal_separator"
};
static const char *widget_size_names[] = {
	N_("GtkTreeView Expander Size"),
	N_("GtkTreeView Horizontal Separation")
};
static GtkWidget *widget_size_widgets[G_N_ELEMENTS(widget_size_prefs)];

static const char *font_prefs[] = {
	"/plugins/gtk/purplerc/font/*pidgin_conv_entry",
	"/plugins/gtk/purplerc/font/*pidgin_conv_imhtml",
	"/plugins/gtk/purplerc/font/*pidginlog_imhtml",
	"/plugins/gtk/purplerc/font/*pidginrequest_imhtml",
	"/plugins/gtk/purplerc/font/*pidginnotify_imhtml",
};
static const char *font_prefs_set[] = {
	"/plugins/gtk/purplerc/set/font/*pidgin_conv_entry",
	"/plugins/gtk/purplerc/set/font/*pidgin_conv_imhtml",
	"/plugins/gtk/purplerc/set/font/*pidginlog_imhtml",
	"/plugins/gtk/purplerc/set/font/*pidginrequest_imhtml",
	"/plugins/gtk/purplerc/set/font/*pidginnotify_imhtml",
};
static const char *font_names[] = {
	N_("Conversation Entry"),
	N_("Conversation History"),
	N_("Log Viewer"),
	N_("Request Dialog"),
	N_("Notify Dialog")
};
static GtkWidget *font_widgets[G_N_ELEMENTS(font_prefs)];

static const char *widget_bool_prefs[] = {
	"/plugins/gtk/purplerc/bool/GtkTreeView::indent_expanders",
};
static const char *widget_bool_prefs_set[] = {
	"/plugins/gtk/purplerc/set/bool/GtkTreeView::indent_expanders",
};
static const char *widget_bool_names[] = {
	N_("GtkTreeView Indent Expanders"),
};
static GtkWidget *widget_bool_widgets[G_N_ELEMENTS(widget_bool_prefs)];

static void
purplerc_make_changes()
{
	int i;
	char *prefbase = NULL;
#if GTK_CHECK_VERSION(2,4,0)
	GtkSettings *setting = NULL;
#endif
	GString *style_string = g_string_new("");

	if (purple_prefs_get_bool("/plugins/gtk/purplerc/set/gtk-font-name")) {
		const char *pref = purple_prefs_get_string("/plugins/gtk/purplerc/gtk-font-name");

		if (pref != NULL && strcmp(pref, ""))
			g_string_append_printf(style_string, "gtk-font-name = \"%s\"\n", pref);
	}

	if (purple_prefs_get_bool("/plugins/gtk/purplerc/set/gtk-key-theme-name")) {
		const char *pref = purple_prefs_get_string("/plugins/gtk/purplerc/gtk-key-theme-name");

		if (pref != NULL && strcmp(pref, ""))
			g_string_append_printf(style_string, "gtk-key-theme-name = \"%s\"\n", pref);
	}

	g_string_append(style_string, "style \"purplerc_style\" {\n");

	for (i = 0; i < G_N_ELEMENTS(color_prefs); i++) {
		if (purple_prefs_get_bool(color_prefs_set[i])) {
			const char *pref;

			prefbase = g_path_get_basename(color_prefs[i]);
			pref = purple_prefs_get_string(color_prefs[i]);

			if (pref != NULL && strcmp(pref, ""))
			    g_string_append_printf(style_string,
			                           "%s = \"%s\"\n",
			                           prefbase, pref);
			g_free(prefbase);
		}
	}

	for (i = 0; i < G_N_ELEMENTS(widget_size_prefs); i++) {
		if (purple_prefs_get_bool(widget_size_prefs_set[i])) {
			prefbase = g_path_get_basename(widget_size_prefs[i]);
			g_string_append_printf(style_string,
			                       "%s = %d\n", prefbase,
			                       purple_prefs_get_int(widget_size_prefs[i]));
			g_free(prefbase);
		}
	}

	for (i = 0; i < G_N_ELEMENTS(widget_bool_prefs); i++) {
		if (purple_prefs_get_bool(widget_bool_prefs_set[i])) {
			prefbase = g_path_get_basename(widget_bool_prefs[i]);
			g_string_append_printf(style_string,
			                       "%s = %d\n", prefbase,
			                       purple_prefs_get_bool(widget_bool_prefs[i]));
			g_free(prefbase);
		}
	}

	g_string_append(style_string, "}");
	g_string_append(style_string, "widget_class \"*\" style \"purplerc_style\"\n");

	for (i = 0; i < G_N_ELEMENTS(font_prefs); i++) {
		if (purple_prefs_get_bool(font_prefs_set[i])) {
			const char *pref;

			prefbase = g_path_get_basename(font_prefs[i]);
			pref = purple_prefs_get_string(font_prefs[i]);

			if (pref != NULL && strcmp(pref, ""))
				g_string_append_printf(style_string,
				                       "style \"%s_style\"\n"
				                       "{font_name = \"%s\"}\n"
				                       "widget \"%s\""
				                       "style \"%s_style\"\n",
				                       prefbase, pref,
				                       prefbase, prefbase);
			g_free(prefbase);
		}
	}

	gtk_rc_parse_string(style_string->str);
	g_string_free(style_string, TRUE);

#if GTK_CHECK_VERSION(2,4,0)
	setting = gtk_settings_get_default();
	gtk_rc_reset_styles(setting);
#endif
}

static void
purplerc_write(GtkWidget *widget, gpointer data)
{
	int i;
	GString *style_string = g_string_new("");
	char *prefbase = NULL;

	if (purple_prefs_get_bool("/plugins/gtk/purplerc/set/gtk-font-name")) {
		const char *pref = purple_prefs_get_string("/plugins/gtk/purplerc/gtk-font-name");

		if (pref != NULL && strcmp(pref, ""))
			g_string_append_printf(style_string,
			                       "gtk-font-name = \"%s\"\n",
			                       pref);
	}

	if (purple_prefs_get_bool("/plugins/gtk/purplerc/set/gtk-key-theme-name")) {
		const char *pref = purple_prefs_get_string("/plugins/gtk/purplerc/gtk-key-theme-name");

		if (pref != NULL && strcmp(pref, ""))
			g_string_append_printf(style_string,
			                       "gtk-key-theme-name = \"%s\"\n",
			                       pref);
	}

	g_string_append(style_string, "style \"purplerc_style\" {\n");

	for (i = 0; i < G_N_ELEMENTS(color_prefs); i++) {
		if (purple_prefs_get_bool(color_prefs_set[i])) {
			const char *pref;

			prefbase = g_path_get_basename(color_prefs[i]);
			pref = purple_prefs_get_string(color_prefs[i]);

			if (pref != NULL && strcmp(pref, ""))
			    g_string_append_printf(style_string,
			                           "%s = \"%s\"\n",
			                           prefbase, pref);
			g_free(prefbase);
		}
	}

	for (i = 0; i < G_N_ELEMENTS(widget_size_prefs); i++) {
		if (purple_prefs_get_bool(widget_size_prefs_set[i])) {
			prefbase = g_path_get_basename(widget_size_prefs[i]);
			g_string_append_printf(style_string,
			                       "%s = %d\n", prefbase,
			                       purple_prefs_get_int(widget_size_prefs[i]));
			g_free(prefbase);
		}
	}

	for (i = 0; i < G_N_ELEMENTS(widget_bool_prefs); i++) {
		if (purple_prefs_get_bool(widget_bool_prefs_set[i])) {
			prefbase = g_path_get_basename(widget_bool_prefs[i]);
			g_string_append_printf(style_string,
			                       "%s = %d\n", prefbase,
			                       purple_prefs_get_bool(widget_bool_prefs[i]));
			g_free(prefbase);
		}
	}

	g_string_append(style_string, "}");
	g_string_append(style_string, "widget_class \"*\" style \"purplerc_style\"\n");

	for (i = 0; i < G_N_ELEMENTS(font_prefs); i++) {
		if (purple_prefs_get_bool(font_prefs_set[i])) {
			const char *pref;

			prefbase = g_path_get_basename(font_prefs[i]);
			pref = purple_prefs_get_string(font_prefs[i]);

			if (pref != NULL && strcmp(pref, ""))
				g_string_append_printf(style_string,
				                       "style \"%s_style\"\n"
				                       "{font_name = \"%s\"}\n"
				                       "widget \"%s\""
				                       "style \"%s_style\"\n",
				                       prefbase, pref,
				                       prefbase, prefbase);
			g_free(prefbase);
		}
	}

	purple_util_write_data_to_file("gtkrc-2.0", style_string->str, -1);

	g_string_free(style_string, TRUE);
}

static void
purplerc_reread(GtkWidget *widget, gpointer data)
{
	gtk_rc_reparse_all();
	/* I don't know if this is necessary but if not it shouldn't hurt. */
	purplerc_make_changes();
}

static void
purplerc_pref_changed_cb(const char *name, PurplePrefType type,
                       gconstpointer value, gpointer data)
{
	purplerc_make_changes();
}

static void
purplerc_color_response(GtkDialog *color_dialog, gint response, gpointer data)
{
	int subscript = GPOINTER_TO_INT(data);

	if (response == GTK_RESPONSE_OK) {
		GtkWidget *colorsel = GTK_COLOR_SELECTION_DIALOG(color_dialog)->colorsel;
		GdkColor color;
		char colorstr[8];

		gtk_color_selection_get_current_color(GTK_COLOR_SELECTION(colorsel), &color);

		g_snprintf(colorstr, sizeof(colorstr), "#%02X%02X%02X",
		           color.red/256, color.green/256, color.blue/256);

		purple_prefs_set_string(color_prefs[subscript], colorstr);
	}
	gtk_widget_destroy(GTK_WIDGET(color_dialog));
}

static void
purplerc_set_color(GtkWidget *widget, gpointer data)
{
	GtkWidget *color_dialog = NULL;
	GdkColor color;
	char title[128];
	const char *pref = NULL;
	int subscript = GPOINTER_TO_INT(data);

	g_snprintf(title, sizeof(title), _("Select Color for %s"),
	           _(color_names[GPOINTER_TO_INT(data)]));
	color_dialog = gtk_color_selection_dialog_new(_("Select Color"));
	g_signal_connect(G_OBJECT(color_dialog), "response",
	                 G_CALLBACK(purplerc_color_response), data);

	pref = purple_prefs_get_string(color_prefs[subscript]);

	if (pref != NULL && strcmp(pref, "")) {
		if (gdk_color_parse(pref, &color)) {
			gtk_color_selection_set_current_color(GTK_COLOR_SELECTION(GTK_COLOR_SELECTION_DIALOG(color_dialog)->colorsel), &color);
		}
	}

	gtk_window_present(GTK_WINDOW(color_dialog));
}

static void
purplerc_font_response(GtkDialog *font_dialog, gint response, gpointer data)
{
	int subscript = GPOINTER_TO_INT(data);

	if (response == GTK_RESPONSE_OK) {
		char *fontname = NULL;

		fontname = gtk_font_selection_dialog_get_font_name(GTK_FONT_SELECTION_DIALOG(font_dialog));

		purple_prefs_set_string(font_prefs[subscript], fontname);
		g_free(fontname);
	}
	gtk_widget_destroy(GTK_WIDGET(font_dialog));
}

static void
purplerc_set_font(GtkWidget *widget, gpointer data)
{
	GtkWidget *font_dialog = NULL;
	char title[128];
	const char *pref = NULL;
	int subscript = GPOINTER_TO_INT(data);

	g_snprintf(title, sizeof(title), _("Select Font for %s"),
	           _(font_names[subscript]));
	font_dialog = gtk_font_selection_dialog_new(title);
	g_signal_connect(G_OBJECT(font_dialog), "response",
	                 G_CALLBACK(purplerc_font_response), data);

	pref = purple_prefs_get_string(font_prefs[subscript]);

	if (pref != NULL && strcmp(pref, "")) {
		gtk_font_selection_set_font_name(GTK_FONT_SELECTION(GTK_FONT_SELECTION_DIALOG(font_dialog)->fontsel), pref);
	}

	gtk_window_present(GTK_WINDOW(font_dialog));
}

static void
purplerc_font_response_special(GtkDialog *font_dialog, gint response,
                             gpointer data)
{
	if (response == GTK_RESPONSE_OK) {
		char *fontname = NULL;

		fontname = gtk_font_selection_dialog_get_font_name(GTK_FONT_SELECTION_DIALOG(font_dialog));

		purple_prefs_set_string("/plugins/gtk/purplerc/gtk-font-name",
		                      fontname);
		g_free(fontname);
	}
	gtk_widget_destroy(GTK_WIDGET(font_dialog));
}

static void
purplerc_set_font_special(GtkWidget *widget, gpointer data)
{
	GtkWidget *font_dialog = NULL;
	const char *pref = NULL;

	font_dialog = gtk_font_selection_dialog_new(_("Select Interface Font"));
	g_signal_connect(G_OBJECT(font_dialog), "response",
	                 G_CALLBACK(purplerc_font_response_special), NULL);

	pref = purple_prefs_get_string("/plugins/gtk/purplerc/gtk-font-name");

	if (pref != NULL && strcmp(pref, "")) {
		gtk_font_selection_set_font_name(GTK_FONT_SELECTION(GTK_FONT_SELECTION_DIALOG(font_dialog)->fontsel), pref);
	}

	gtk_window_present(GTK_WINDOW(font_dialog));
}

static gboolean
purplerc_plugin_load(PurplePlugin *plugin)
{
	purplerc_make_changes();

	pref_callback = purple_prefs_connect_callback(plugin, "/plugins/gtk/purplerc",
	                                            purplerc_pref_changed_cb, NULL);

	return TRUE;
}

static gboolean
purplerc_plugin_unload(PurplePlugin *plugin)
{
	purple_prefs_disconnect_callback(pref_callback);

	return TRUE;
}

static GtkWidget *
purplerc_get_config_frame(PurplePlugin *plugin)
{
	/* Note: Intentionally not using the size group argument to the
	 * pidgin_prefs_labeled_* functions they only add the text label to
	 * the size group not the whole thing, which isn't what I want. */
	int i;
	char *tmp;
	GtkWidget *check = NULL, *widget = NULL;
	GtkSizeGroup *labelsg = NULL, *widgetsg = NULL;
	GtkWidget *ret = NULL, *frame = NULL, *hbox = NULL, *vbox = NULL;

	ret = gtk_vbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
	gtk_container_set_border_width(GTK_CONTAINER(ret), PIDGIN_HIG_BORDER);

	labelsg  = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	widgetsg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	frame = pidgin_make_frame(ret, _("General"));
	/* interface font */
	hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
	gtk_box_pack_start(GTK_BOX(frame), hbox, FALSE, FALSE, 0);

	check = pidgin_prefs_checkbox(_("GTK+ Interface Font"),
	                                "/plugins/gtk/purplerc/set/gtk-font-name",
	                                hbox);
	gtk_size_group_add_widget(labelsg, check);

	widget = pidgin_pixbuf_button_from_stock("", GTK_STOCK_SELECT_FONT,
	                                       PIDGIN_BUTTON_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
	gtk_size_group_add_widget(widgetsg, widget);
	gtk_widget_set_sensitive(widget,
	                         purple_prefs_get_bool("/plugins/gtk/purplerc/set/gtk-font-name"));
	g_signal_connect(G_OBJECT(check), "toggled",
	                 G_CALLBACK(pidgin_toggle_sensitive), widget);
	g_signal_connect(G_OBJECT(widget), "clicked",
	                 G_CALLBACK(purplerc_set_font_special), NULL);

	/* key theme name */
	hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
	gtk_box_pack_start(GTK_BOX(frame), hbox, FALSE, FALSE, 0);

	check = pidgin_prefs_checkbox(_("GTK+ Text Shortcut Theme"),
	                                "/plugins/gtk/purplerc/set/gtk-key-theme-name",
	                                hbox);
	gtk_size_group_add_widget(labelsg, check);

	widget = pidgin_prefs_labeled_entry(hbox, "",
	                                      "/plugins/gtk/purplerc/gtk-key-theme-name",
	                                      NULL);
	/*
	gtk_size_group_add_widget(widgetsg, widget);
	*/
	gtk_widget_set_sensitive(widget,
	                         purple_prefs_get_bool("/plugins/gtk/purplerc/set/gtk-key-theme-name"));
	g_signal_connect(G_OBJECT(check), "toggled",
	                 G_CALLBACK(pidgin_toggle_sensitive), widget);

	for (i = 0; i < G_N_ELEMENTS(widget_bool_prefs); i++) {
		hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
		gtk_box_pack_start(GTK_BOX(frame), hbox, FALSE, FALSE, 0);

		check = pidgin_prefs_checkbox(_(widget_bool_names[i]),
		                                widget_bool_prefs_set[i], hbox);
		gtk_size_group_add_widget(labelsg, check);

		widget_bool_widgets[i] = pidgin_prefs_checkbox("", widget_bool_prefs[i], hbox);
		/*
		gtk_size_group_add_widget(widgetsb, widget_bool_widgets[i]);
		*/
		gtk_widget_set_sensitive(widget_bool_widgets[i],
		                         purple_prefs_get_bool(widget_bool_prefs_set[i]));
		g_signal_connect(G_OBJECT(check), "toggled",
		                 G_CALLBACK(pidgin_toggle_sensitive),
		                 widget_bool_widgets[i]);
	}

	frame = pidgin_make_frame(ret, _("Interface colors"));
	/* imhtml stuff */
	for (i = 0; i < G_N_ELEMENTS(color_prefs); i++) {
		hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
		gtk_box_pack_start(GTK_BOX(frame), hbox, FALSE, FALSE, 0);

		check = pidgin_prefs_checkbox(_(color_names[i]),
		                                color_prefs_set[i], hbox);
		gtk_size_group_add_widget(labelsg, check);

		color_widgets[i] = pidgin_pixbuf_button_from_stock("", GTK_STOCK_SELECT_COLOR, PIDGIN_BUTTON_HORIZONTAL);
		gtk_size_group_add_widget(widgetsg, color_widgets[i]);
		gtk_box_pack_start(GTK_BOX(hbox), color_widgets[i], FALSE,
		                   FALSE, 0);
		gtk_widget_set_sensitive(color_widgets[i],
		                         purple_prefs_get_bool(color_prefs_set[i]));
		g_signal_connect(G_OBJECT(check), "toggled",
		                 G_CALLBACK(pidgin_toggle_sensitive),
		                 color_widgets[i]);
		g_signal_connect(G_OBJECT(color_widgets[i]), "clicked",
		                 G_CALLBACK(purplerc_set_color),
		                 GINT_TO_POINTER(i));
	}

	frame = pidgin_make_frame(ret, _("Widget Sizes"));
	/* widget size stuff */
	for (i = 0; i < G_N_ELEMENTS(widget_size_prefs); i++) {
		hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
		gtk_box_pack_start(GTK_BOX(frame), hbox, FALSE, FALSE, 0);

		check = pidgin_prefs_checkbox(_(widget_size_names[i]),
		                                widget_size_prefs_set[i], hbox);
		gtk_size_group_add_widget(labelsg, check);

		widget_size_widgets[i] = pidgin_prefs_labeled_spin_button(hbox, "", widget_size_prefs[i], 0, 50, NULL);
		/*
		gtk_size_group_add_widget(widgetsg, widget_size_widgets[i]);
		*/
		gtk_widget_set_sensitive(widget_size_widgets[i],
		                         purple_prefs_get_bool(widget_size_prefs_set[i]));
		g_signal_connect(G_OBJECT(check), "toggled",
		                 G_CALLBACK(pidgin_toggle_sensitive),
		                 widget_size_widgets[i]);
	}

	frame = pidgin_make_frame(ret, _("Fonts"));
	/* imhtml font stuff */
	for (i = 0; i < G_N_ELEMENTS(font_prefs); i++) {
		hbox = gtk_hbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
		gtk_box_pack_start(GTK_BOX(frame), hbox, FALSE, FALSE, 0);

		check = pidgin_prefs_checkbox(_(font_names[i]),
		                                font_prefs_set[i], hbox);
		gtk_size_group_add_widget(labelsg, check);

		font_widgets[i] = pidgin_pixbuf_button_from_stock("", GTK_STOCK_SELECT_FONT, PIDGIN_BUTTON_HORIZONTAL);
		gtk_size_group_add_widget(widgetsg, font_widgets[i]);
		gtk_box_pack_start(GTK_BOX(hbox), font_widgets[i], FALSE,
		                   FALSE, 0);
		gtk_widget_set_sensitive(font_widgets[i],
		                         purple_prefs_get_bool(font_prefs_set[i]));
		g_signal_connect(G_OBJECT(check), "toggled",
		                 G_CALLBACK(pidgin_toggle_sensitive),
		                 font_widgets[i]);
		g_signal_connect(G_OBJECT(font_widgets[i]), "clicked",
		                 G_CALLBACK(purplerc_set_font), GINT_TO_POINTER(i));
	}

	frame = pidgin_make_frame(ret, _("Tools"));

	vbox = gtk_vbox_new(FALSE, PIDGIN_HIG_CAT_SPACE);
	gtk_box_pack_start(GTK_BOX(frame), vbox, FALSE, FALSE, 0);

	tmp = g_strdup_printf(_("Write settings to %s%sgtkrc-2.0"), purple_user_dir(), G_DIR_SEPARATOR_S);
	check = gtk_button_new_with_label(tmp);
	g_free(tmp);
	gtk_box_pack_start(GTK_BOX(vbox), check, FALSE, FALSE, 0);
	gtk_size_group_add_widget(labelsg, check);
	g_signal_connect(G_OBJECT(check), "clicked", G_CALLBACK(purplerc_write),
	                 NULL);

	check = gtk_button_new_with_label(_("Re-read gtkrc files"));
	gtk_box_pack_start(GTK_BOX(vbox), check, FALSE, FALSE, 0);
	gtk_size_group_add_widget(labelsg, check);
	g_signal_connect(G_OBJECT(check), "clicked", G_CALLBACK(purplerc_reread),
	                 NULL);

	gtk_widget_show_all(ret);
	return ret;
}

static PidginPluginUiInfo purplerc_ui_info =
{
	purplerc_get_config_frame,
	0 /* page_num (Reserved) */
};

static PurplePluginInfo purplerc_info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	PIDGIN_PLUGIN_TYPE,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,
	"purplerc",
	N_("Pidgin GTK+ Theme Control"),
	VERSION,
	N_("Provides access to commonly used gtkrc settings."),
	N_("Provides access to commonly used gtkrc settings."),
	"Etan Reisner <deryni@eden.rutgers.edu>",
	PURPLE_WEBSITE,
	purplerc_plugin_load,
	purplerc_plugin_unload,
	NULL,
	&purplerc_ui_info,
	NULL,
	NULL,
	NULL
};

static void
purplerc_init(PurplePlugin *plugin)
{
	int i;

	purple_prefs_add_none("/plugins");
	purple_prefs_add_none("/plugins/gtk");
	purple_prefs_add_none("/plugins/gtk/purplerc");
	purple_prefs_add_none("/plugins/gtk/purplerc/set");

	purple_prefs_add_string("/plugins/gtk/purplerc/gtk-font-name", "");
	purple_prefs_add_bool("/plugins/gtk/purplerc/set/gtk-font-name", FALSE);

	purple_prefs_add_string("/plugins/gtk/purplerc/gtk-key-theme-name", "");
	purple_prefs_add_bool("/plugins/gtk/purplerc/set/gtk-key-theme-name", FALSE);

	purple_prefs_add_none("/plugins/gtk/purplerc/color");
	purple_prefs_add_none("/plugins/gtk/purplerc/set/color");
	for (i = 0; i < G_N_ELEMENTS(color_prefs); i++) {
		purple_prefs_add_string(color_prefs[i], "");
		purple_prefs_add_bool(color_prefs_set[i], FALSE);
	}

	purple_prefs_add_none("/plugins/gtk/purplerc/size");
	purple_prefs_add_none("/plugins/gtk/purplerc/set/size");
	for (i = 0; i < G_N_ELEMENTS(widget_size_prefs); i++) {
		purple_prefs_add_int(widget_size_prefs[i], 0);
		purple_prefs_add_bool(widget_size_prefs_set[i], FALSE);
	}

	purple_prefs_add_none("/plugins/gtk/purplerc/font");
	purple_prefs_add_none("/plugins/gtk/purplerc/set/font");
	for (i = 0; i < G_N_ELEMENTS(font_prefs); i++) {
		purple_prefs_add_string(font_prefs[i], "");
		purple_prefs_add_bool(font_prefs_set[i], FALSE);
	}

	purple_prefs_add_none("/plugins/gtk/purplerc/bool");
	purple_prefs_add_none("/plugins/gtk/purplerc/set/bool");
	for (i = 0; i < G_N_ELEMENTS(widget_bool_prefs); i++) {
		purple_prefs_add_bool(widget_bool_prefs[i], TRUE);
		purple_prefs_add_bool(widget_bool_prefs_set[i], FALSE);
	}
}

PURPLE_INIT_PLUGIN(purplerc, purplerc_init, purplerc_info)
