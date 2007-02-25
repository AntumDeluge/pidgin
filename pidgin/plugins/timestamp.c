/*
 * Gaim - iChat-style timestamps
 *
 * Copyright (C) 2002-2003, Sean Egan
 * Copyright (C) 2003, Chris J. Friesen <Darth_Sebulba04@yahoo.com>
 * Copyright (C) 2007, Andrew Gaul <andrew@gaul.org>
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

#include "conversation.h"
#include "debug.h"
#include "prefs.h"
#include "signals.h"
#include "version.h"

#include "gtkimhtml.h"
#include "gtkplugin.h"
#include "gtkutils.h"

#define TIMESTAMP_PLUGIN_ID "gtk-timestamp"

/* minutes externally, seconds internally, and milliseconds in preferences */
static int interval = 5 * 60;

static void
timestamp_display(GaimConversation *conv, time_t then, time_t now)
{
	PidginConversation *gtk_conv = PIDGIN_CONVERSATION(conv);
	GtkWidget *imhtml = gtk_conv->imhtml;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(imhtml));
	GtkTextIter iter;
	const char *mdate;
	int y, height;
	GdkRectangle rect;
	
	/* display timestamp */
	mdate = gaim_utf8_strftime(then == 0 ? "%H:%M" : "\n%H:%M",
		localtime(&now));
	gtk_text_buffer_get_end_iter(buffer, &iter);
	gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, mdate,
		strlen(mdate), "TIMESTAMP", NULL);

	/* scroll view if necessary */
	gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(imhtml), &rect);
	gtk_text_view_get_line_yrange(
		GTK_TEXT_VIEW(imhtml), &iter, &y, &height);
	if (((y + height) - (rect.y + rect.height)) > height &&
	    gtk_text_buffer_get_char_count(buffer)) {
		gboolean smooth = gaim_prefs_get_bool(
			"/gaim/gtk/conversations/use_smooth_scrolling");
		gtk_imhtml_scroll_to_end(GTK_IMHTML(imhtml), smooth);
	}
}

static gboolean
timestamp_displaying_conv_msg(GaimAccount *account, const char *who,
			      char **buffer, GaimConversation *conv,
			      GaimMessageFlags flags, void *data)
{
	time_t now = time(NULL) / interval * interval;
	time_t then;

	if (!g_list_find(gaim_get_conversations(), conv))
		return FALSE;

	then = GPOINTER_TO_INT(gaim_conversation_get_data(
		conv, "timestamp-last"));

	if (now - then >= interval) {
		timestamp_display(conv, then, now);
		gaim_conversation_set_data(
			conv, "timestamp-last", GINT_TO_POINTER(now));
	}

	return FALSE;
}

static void
timestamp_new_convo(GaimConversation *conv)
{
	PidginConversation *gtk_conv = PIDGIN_CONVERSATION(conv);
	GtkTextBuffer *buffer;

	if (!g_list_find(gaim_get_conversations(), conv))
		return;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_conv->imhtml));
	gtk_text_buffer_create_tag(buffer, "TIMESTAMP",
		"foreground", "#888888", "justification", GTK_JUSTIFY_CENTER,
		"weight", PANGO_WEIGHT_BOLD, NULL);

	gaim_conversation_set_data(conv, "timestamp-last", GINT_TO_POINTER(0));
}

static void
set_timestamp(GtkWidget *spinner, void *null)
{
	int tm;

	tm = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinner));
	gaim_debug(GAIM_DEBUG_MISC, "timestamp",
		"setting interval to %d minutes\n", tm);

	interval = tm * 60;
	gaim_prefs_set_int("/plugins/gtk/timestamp/interval", interval * 1000);
}

static GtkWidget *
get_config_frame(GaimPlugin *plugin)
{
	GtkWidget *ret;
	GtkWidget *frame, *label;
	GtkWidget *vbox, *hbox;
	GtkObject *adj;
	GtkWidget *spinner;

	ret = gtk_vbox_new(FALSE, 18);
	gtk_container_set_border_width (GTK_CONTAINER (ret), 12);

	frame = pidgin_make_frame(ret, _("Display Timestamps Every"));
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

	/* XXX limit to divisors of 60? */
	adj = gtk_adjustment_new(interval / 60, 1, 60, 1, 0, 0);
	spinner = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), spinner, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(spinner), "value-changed",
		G_CALLBACK(set_timestamp), NULL);
	label = gtk_label_new(_("minutes"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	gtk_widget_show_all(ret);
	return ret;
}

static gboolean
plugin_load(GaimPlugin *plugin)
{
	void *conv_handle = gaim_conversations_get_handle();
	void *gtkconv_handle = pidgin_conversations_get_handle();

	/* lower priority to display initial timestamp after logged messages */
	gaim_signal_connect_priority(conv_handle, "conversation-created",
		plugin, GAIM_CALLBACK(timestamp_new_convo), NULL,
		GAIM_SIGNAL_PRIORITY_DEFAULT + 1);

	gaim_signal_connect(gtkconv_handle, "displaying-chat-msg",
		plugin, GAIM_CALLBACK(timestamp_displaying_conv_msg), NULL);
	gaim_signal_connect(gtkconv_handle, "displaying-im-msg",
		plugin, GAIM_CALLBACK(timestamp_displaying_conv_msg), NULL);

	interval = gaim_prefs_get_int("/plugins/gtk/timestamp/interval") / 1000;

	return TRUE;
}

static PidginPluginUiInfo ui_info =
{
	get_config_frame,
	0 /* page_num (Reserved) */
};

static GaimPluginInfo info =
{
	GAIM_PLUGIN_MAGIC,
	GAIM_MAJOR_VERSION,
	GAIM_MINOR_VERSION,
	GAIM_PLUGIN_STANDARD,                             /**< type           */
	PIDGIN_PLUGIN_TYPE,                             /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	GAIM_PRIORITY_DEFAULT,                            /**< priority       */

	TIMESTAMP_PLUGIN_ID,                              /**< id             */
	N_("Timestamp"),                                  /**< name           */
	VERSION,                                          /**< version        */
	                                                  /**  summary        */
	N_("Display iChat-style timestamps"),
	                                                  /**  description    */
	N_("Display iChat-style timestamps every N minutes."),
	"Sean Egan <seanegan@gmail.com>",                 /**< author         */
	GAIM_WEBSITE,                                     /**< homepage       */

	plugin_load,                                      /**< load           */
	NULL,                                             /**< unload         */
	NULL,                                             /**< destroy        */

	&ui_info,                                         /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,
	NULL
};

static void
init_plugin(GaimPlugin *plugin)
{
	gaim_prefs_add_none("/plugins/gtk/timestamp");
	gaim_prefs_add_int("/plugins/gtk/timestamp/interval", interval * 1000);
}

GAIM_INIT_PLUGIN(interval, init_plugin, info)
