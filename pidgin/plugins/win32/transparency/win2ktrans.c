/*
 * gaim - Transparency plugin
 *
 * copyright (c) 1998-2002, rob flynn <rob@marko.net>
 * copyright (c) 2002-2003, Herman Bloggs <hermanator12002@yahoo.com>
 * copyright (c) 2005,      Daniel Atallah <daniel_atallah@yahoo.com>
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2 of the license, or
 * (at your option) any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program; if not, write to the free software
 * foundation, inc., 59 temple place, suite 330, boston, ma  02111-1307  usa
 *
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <gdk/gdkwin32.h>
#include "internal.h"

#include "core.h"
#include "prefs.h"
#include "debug.h"

#include "gtkconv.h"
#include "gtkplugin.h"
#include "gtkprefs.h"
#include "gtkblist.h"
#include "gtkutils.h"
#include "signals.h"
#include "version.h"

/*
 *  MACROS & DEFINES
 */
#define WINTRANS_PLUGIN_ID	"gtk-win-trans"

#define blist	(gaim_get_blist() \
		? (GAIM_GTK_BLIST(gaim_get_blist()) \
			? ((GAIM_GTK_BLIST(gaim_get_blist()))->window) \
			: NULL) \
		: NULL)

/*
 *  DATA STRUCTS
 */
typedef struct {
	GtkWidget *win;
	GtkWidget *slider;
} slider_win;

/*
 *  LOCALS
 */
static const char *OPT_WINTRANS_IM_ENABLED= "/plugins/gtk/win32/wintrans/im_enabled";
static const char *OPT_WINTRANS_IM_ALPHA  = "/plugins/gtk/win32/wintrans/im_alpha";
static const char *OPT_WINTRANS_IM_SLIDER = "/plugins/gtk/win32/wintrans/im_slider";
static const char *OPT_WINTRANS_IM_ONFOCUS= "/plugins/gtk/win32/wintrans/im_solid_onfocus";
static const char *OPT_WINTRANS_IM_ONTOP  = "/plugins/gtk/win32/wintrans/im_always_on_top";
static const char *OPT_WINTRANS_BL_ENABLED= "/plugins/gtk/win32/wintrans/bl_enabled";
static const char *OPT_WINTRANS_BL_ALPHA  = "/plugins/gtk/win32/wintrans/bl_alpha";
static const char *OPT_WINTRANS_BL_ONFOCUS= "/plugins/gtk/win32/wintrans/bl_solid_onfocus";
static const char *OPT_WINTRANS_BL_ONTOP  = "/plugins/gtk/win32/wintrans/bl_always_on_top";
static GSList *window_list = NULL;

static BOOL (*MySetLayeredWindowAttributes)(HWND hwnd, COLORREF crKey, BYTE bAlpha, DWORD dwFlags) = NULL;

/*
 *  CODE
 */
static GtkWidget *wgaim_button(const char *text, const char *pref, GtkWidget *page) {
	GtkWidget *button;
	button = gtk_check_button_new_with_mnemonic(text);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),
		gaim_prefs_get_bool(pref));
	gtk_box_pack_start(GTK_BOX(page), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	return button;
}

/* Set window transparency level */
static void set_wintrans(GtkWidget *window, int alpha, gboolean enabled,
		gboolean always_on_top) {
	if (MySetLayeredWindowAttributes) {
		HWND hWnd = GDK_WINDOW_HWND(window->window);
		LONG style = GetWindowLong(hWnd, GWL_EXSTYLE);
		if (enabled) {
			style |= WS_EX_LAYERED;
		} else {
			style &= ~WS_EX_LAYERED;
		}
		SetWindowLong(hWnd, GWL_EXSTYLE, style);


		if (enabled) {
			SetWindowPos(hWnd,
				always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
				0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			MySetLayeredWindowAttributes(hWnd, 0, alpha, LWA_ALPHA);
		} else {
			/* Ask the window and its children to repaint */
			SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

			RedrawWindow(hWnd, NULL, NULL,
				RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
		}
	}
}

/* When a conv window is focused, if we're only transparent when unfocused,
 * deal with transparency */
static gboolean focus_conv_win_cb(GtkWidget *w, GdkEventFocus *e, gpointer d) {
	if (gaim_prefs_get_bool(OPT_WINTRANS_IM_ENABLED)
			&& gaim_prefs_get_bool(OPT_WINTRANS_IM_ONFOCUS)) {
		GtkWidget *window = (GtkWidget *) d;
		if (e->in) { /* Focused */
			set_wintrans(window, 0, FALSE,
				gaim_prefs_get_bool(OPT_WINTRANS_IM_ONTOP));
		} else {
			set_wintrans(window,
				gaim_prefs_get_int(OPT_WINTRANS_IM_ALPHA),
				TRUE,
				gaim_prefs_get_bool(OPT_WINTRANS_IM_ONTOP));
		}
	}
	return FALSE;
}

/* When buddy list window is focused,
 * if we're only transparent when unfocused, deal with transparency */
static gboolean focus_blist_win_cb(GtkWidget *w, GdkEventFocus *e, gpointer d) {
	if (gaim_prefs_get_bool(OPT_WINTRANS_BL_ENABLED)
			&& gaim_prefs_get_bool(OPT_WINTRANS_BL_ONFOCUS)) {
		GtkWidget *window = (GtkWidget *) d;
		if (e->in) { /* Focused */
			set_wintrans(window, 0, FALSE,
				gaim_prefs_get_bool(OPT_WINTRANS_BL_ONTOP));
		} else {
			set_wintrans(window,
				gaim_prefs_get_int(OPT_WINTRANS_BL_ALPHA),
				TRUE,
				gaim_prefs_get_bool(OPT_WINTRANS_BL_ONTOP));
		}
	}
	return FALSE;
}

static void change_alpha(GtkWidget *w, gpointer data) {
	int alpha = gtk_range_get_value(GTK_RANGE(w));
	gaim_prefs_set_int(OPT_WINTRANS_IM_ALPHA, alpha);

	/* If we're in no-transparency on focus mode,
	 * don't take effect immediately */
	if (!gaim_prefs_get_bool(OPT_WINTRANS_IM_ONFOCUS))
		set_wintrans(GTK_WIDGET(data), alpha, TRUE,
			gaim_prefs_get_bool(OPT_WINTRANS_IM_ONTOP));
}


static GtkWidget *wintrans_slider(GtkWidget *win) {
	GtkWidget *hbox;
	GtkWidget *label, *slider;
	GtkWidget *frame;

	int imalpha = gaim_prefs_get_int(OPT_WINTRANS_IM_ALPHA);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	gtk_widget_show(frame);

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(frame), hbox);

	label = gtk_label_new(_("Opacity:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
	gtk_widget_show(hbox);

	slider = gtk_hscale_new_with_range(50, 255, 1);
	gtk_range_set_value(GTK_RANGE(slider), imalpha);
	gtk_widget_set_usize(GTK_WIDGET(slider), 200, -1);

	/* On slider val change, update window's transparency level */
	g_signal_connect(GTK_OBJECT(slider), "value-changed",
		GTK_SIGNAL_FUNC(change_alpha), win);

	gtk_box_pack_start(GTK_BOX(hbox), slider, FALSE, TRUE, 5);

	/* Set the initial transparency level */
	set_wintrans(win, imalpha, TRUE,
		gaim_prefs_get_bool(OPT_WINTRANS_IM_ONTOP));

	gtk_widget_show_all(hbox);

	return frame;
}

static slider_win* find_slidwin(GtkWidget *win) {
	GSList *tmp = window_list;

	while (tmp) {
		if (((slider_win*) (tmp->data))->win == win)
			return (slider_win*) tmp->data;
		tmp = tmp->next;
	}
	return NULL;
}

/* Clean up transparency stuff for the conv window */
static void cleanup_conv_window(GaimGtkWindow *win) {
	GtkWidget *window = win->window;
	slider_win *slidwin = NULL;

	/* Remove window from the window list */
	gaim_debug_info(WINTRANS_PLUGIN_ID,
		"Conv window destroyed... removing from list\n");

	if ((slidwin = find_slidwin(window))) {
		window_list = g_slist_remove(window_list, slidwin);
		g_free(slidwin);
	}

	/* Remove the focus cbs */
	g_signal_handlers_disconnect_by_func(G_OBJECT(window),
		G_CALLBACK(focus_conv_win_cb), window);
}

static void gaim_conversation_delete(GaimConversation *conv) {
	GaimGtkWindow *win = gaim_gtkconv_get_window(GAIM_GTK_CONVERSATION(conv));
	/* If it is the last conversation in the window, cleanup */
	if (gaim_gtk_conv_window_get_gtkconv_count(win) == 1)
		cleanup_conv_window(win);
}

static void set_blist_trans(GtkWidget *w, const char *pref) {
	gboolean enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
	gaim_prefs_set_bool(pref, enabled);
	if (blist) {
		set_wintrans(blist, gaim_prefs_get_int(OPT_WINTRANS_BL_ALPHA),
			gaim_prefs_get_bool(OPT_WINTRANS_BL_ENABLED),
			gaim_prefs_get_bool(OPT_WINTRANS_IM_ONTOP));
	}
}

static void add_slider(GtkWidget *win) {
	GList *wl, *wl1;
	GtkWidget *vbox = NULL;

	/* Look up this window to see if it already has a slider */
	if (!find_slidwin(win)) {
		GtkWidget *slider_box = NULL;
		slider_win *slidwin = NULL;
		GtkRequisition slidereq;
		gint width, height;

		/* Get top vbox */
		for (wl1 = wl = gtk_container_get_children(
					GTK_CONTAINER(win));
				wl != NULL;
				wl = wl->next) {
			if (GTK_IS_VBOX(GTK_OBJECT(wl->data)))
				vbox = GTK_WIDGET(wl->data);
			else {
				gaim_debug_error(WINTRANS_PLUGIN_ID,
					"no vbox found\n");
				return;
			}
		}
		g_list_free(wl1);

		slider_box = wintrans_slider(win);
		/* Figure out how tall the slider wants to be */
		gtk_widget_size_request(slider_box, &slidereq);
		gtk_window_get_size(GTK_WINDOW(win), &width, &height);
		gtk_box_pack_start(GTK_BOX(vbox),
			slider_box, FALSE, FALSE, 0);
		/* Make window taller so we don't slowly collapse its message area */
		gtk_window_resize(GTK_WINDOW(win), width,
			(height + slidereq.height));
		/* Add window to list, to track that it has a slider */
		slidwin = g_new0(slider_win, 1);
		slidwin->win = win;
		slidwin->slider = slider_box;
		window_list = g_slist_append(window_list, slidwin);
	}
}

static void remove_sliders() {
	if (window_list) {
		GSList *tmp = window_list;
		while (tmp) {
			slider_win *slidwin = (slider_win*) tmp->data;
			if (slidwin != NULL &&
					GTK_IS_WINDOW(slidwin->win)) {
				GtkRequisition slidereq;
				gint width, height;
				/* Figure out how tall the slider was */
				gtk_widget_size_request(
					slidwin->slider, &slidereq);
				gtk_window_get_size(
					GTK_WINDOW(slidwin->win),
					&width, &height);

				gtk_widget_destroy(slidwin->slider);

				gtk_window_resize(
					GTK_WINDOW(slidwin->win),
					width, (height - slidereq.height));
			}
			g_free(slidwin);
			tmp = tmp->next;
		}
		g_slist_free(window_list);
		window_list = NULL;
	}
}

/* Remove all transparency related aspects from conversation windows */
static void remove_convs_wintrans(gboolean remove_signal) {
	GList *wins;

	for (wins = gaim_gtk_conv_windows_get_list(); wins; wins = wins->next) {
		GaimGtkWindow *win = wins->data;
		GtkWidget *window = win->window;

		if (gaim_prefs_get_bool(OPT_WINTRANS_IM_ENABLED))
			set_wintrans(window, 0, FALSE, FALSE);

		/* Remove the focus cbs */
		if (remove_signal)
			g_signal_handlers_disconnect_by_func(G_OBJECT(window),
				G_CALLBACK(focus_conv_win_cb), window);
	}

	remove_sliders();
}

static void set_conv_window_trans(GaimGtkWindow *oldwin, GaimGtkWindow *newwin) {
	GtkWidget *win = newwin->window;

	/* check prefs to see if we want trans */
	if (gaim_prefs_get_bool(OPT_WINTRANS_IM_ENABLED)) {
		set_wintrans(win, gaim_prefs_get_int(OPT_WINTRANS_IM_ALPHA),
			TRUE, gaim_prefs_get_bool(OPT_WINTRANS_IM_ONTOP));

		if (gaim_prefs_get_bool(OPT_WINTRANS_IM_SLIDER)) {
			add_slider(win);
		}
	}

	/* If we're moving from one window to another,
	 * add the focus listeners to the new window if not already there */
	if (oldwin != NULL && oldwin != newwin) {
		if (gaim_gtk_conv_window_get_gtkconv_count(newwin) == 0) {
			g_signal_connect(G_OBJECT(win), "focus_in_event",
				G_CALLBACK(focus_conv_win_cb), win);
			g_signal_connect(G_OBJECT(win), "focus_out_event",
				G_CALLBACK(focus_conv_win_cb), win);
		}

		/* If we've moved the last conversation, cleanup the window */
		if (gaim_gtk_conv_window_get_gtkconv_count(oldwin) == 1)
			cleanup_conv_window(oldwin);
	}
}

static void update_convs_wintrans(GtkWidget *toggle_btn, const char *pref) {
	gaim_prefs_set_bool(pref, gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(toggle_btn)));

	if (gaim_prefs_get_bool(OPT_WINTRANS_IM_ENABLED)) {
		GList *wins;

		for (wins = gaim_gtk_conv_windows_get_list(); wins; wins = wins->next) {
			GaimGtkWindow *win = wins->data;
			set_conv_window_trans(NULL, win);
		}

		if (!gaim_prefs_get_bool(OPT_WINTRANS_IM_SLIDER))
			remove_sliders();
	}
	else
		remove_convs_wintrans(FALSE);
}

static void gaim_new_conversation(GaimConversation *conv) {
	GaimGtkWindow *win = gaim_gtkconv_get_window(GAIM_GTK_CONVERSATION(conv));

	/* If it is the first conversation in the window,
	 * add the sliders, and set transparency */
	if (gaim_gtk_conv_window_get_gtkconv_count(win) == 1) {
		GtkWidget *window = win->window;

		set_conv_window_trans(NULL, win);

		g_signal_connect(G_OBJECT(window), "focus_in_event",
			G_CALLBACK(focus_conv_win_cb), window);
		g_signal_connect(G_OBJECT(window), "focus_out_event",
			G_CALLBACK(focus_conv_win_cb), window);
	}
}

static void blist_created_cb(GaimBuddyList *gaim_blist, gpointer data) {
	if (blist) {
		if (gaim_prefs_get_bool(OPT_WINTRANS_BL_ENABLED)) {
			set_wintrans(blist,
				gaim_prefs_get_int(OPT_WINTRANS_BL_ALPHA),
				TRUE,
				gaim_prefs_get_bool(OPT_WINTRANS_BL_ONTOP));
		}

		g_signal_connect(G_OBJECT(blist), "focus_in_event",
			G_CALLBACK(focus_blist_win_cb), blist);
		g_signal_connect(G_OBJECT(blist), "focus_out_event",
			G_CALLBACK(focus_blist_win_cb), blist);
	}
}

static void alpha_change(GtkWidget *w, gpointer data) {
	GList *wins;
	int imalpha = gtk_range_get_value(GTK_RANGE(w));

	for (wins = gaim_gtk_conv_windows_get_list(); wins; wins = wins->next) {
		GaimGtkWindow *win = wins->data;
		set_wintrans(win->window, imalpha, TRUE,
			gaim_prefs_get_bool(OPT_WINTRANS_IM_ONTOP));
	}
}

static void alpha_pref_set_int (GtkWidget *w, GdkEventFocus *e, const char *pref)
{
	int alpha = gtk_range_get_value(GTK_RANGE(w));
	gaim_prefs_set_int(pref, alpha);
}

static void bl_alpha_change(GtkWidget *w, gpointer data) {
	if (blist)
		change_alpha(w, blist);
}

static void update_existing_convs() {
	GList *wins;

	for (wins = gaim_gtk_conv_windows_get_list(); wins; wins = wins->next) {
		GaimGtkWindow *win = wins->data;
		GtkWidget *window = win->window;

		set_conv_window_trans(NULL, win);

		g_signal_connect(G_OBJECT(window), "focus_in_event",
			G_CALLBACK(focus_conv_win_cb), window);
		g_signal_connect(G_OBJECT(window), "focus_out_event",
			G_CALLBACK(focus_conv_win_cb), window);
	}
}

/*
 *  EXPORTED FUNCTIONS
 */
static gboolean plugin_load(GaimPlugin *plugin) {
	MySetLayeredWindowAttributes = (void*) wgaim_find_and_loadproc(
		"user32.dll", "SetLayeredWindowAttributes");

	if (!MySetLayeredWindowAttributes) {
		gaim_debug_error(WINTRANS_PLUGIN_ID,
			"SetLayeredWindowAttributes API not found (Required W2K+)\n");
		return FALSE;
	}

	gaim_signal_connect(gaim_conversations_get_handle(),
		"conversation-created", plugin,
		GAIM_CALLBACK(gaim_new_conversation), NULL);

	/* Set callback to remove window from the list, if the window is destroyed */
	gaim_signal_connect(gaim_conversations_get_handle(),
		"deleting-conversation", plugin,
		GAIM_CALLBACK(gaim_conversation_delete), NULL);

	gaim_signal_connect(gaim_gtk_conversations_get_handle(),
		"conversation-dragging", plugin,
		GAIM_CALLBACK(set_conv_window_trans), NULL);

	update_existing_convs();

	if (blist)
		blist_created_cb(NULL, NULL);
	else
		gaim_signal_connect(gaim_gtk_blist_get_handle(),
			"gtkblist-created", plugin,
			GAIM_CALLBACK(blist_created_cb), NULL);


	return TRUE;
}

static gboolean plugin_unload(GaimPlugin *plugin) {
	gaim_debug_info(WINTRANS_PLUGIN_ID, "Unloading win2ktrans plugin\n");

	remove_convs_wintrans(TRUE);

	if (blist) {
		if (gaim_prefs_get_bool(OPT_WINTRANS_BL_ENABLED))
			set_wintrans(blist, 0, FALSE, FALSE);

		/* Remove the focus cbs */
		g_signal_handlers_disconnect_by_func(G_OBJECT(blist),
			G_CALLBACK(focus_blist_win_cb), blist);
	}

	return TRUE;
}

static GtkWidget *get_config_frame(GaimPlugin *plugin) {
	GtkWidget *ret;
	GtkWidget *imtransbox, *bltransbox;
	GtkWidget *hbox;
	GtkWidget *label, *slider;
	GtkWidget *button;
	GtkWidget *trans_box;

	ret = gtk_vbox_new(FALSE, 18);
	gtk_container_set_border_width(GTK_CONTAINER (ret), 12);

	/* IM Convo trans options */
	imtransbox = gaim_gtk_make_frame(ret, _("IM Conversation Windows"));
	button = wgaim_button(_("_IM window transparency"),
		OPT_WINTRANS_IM_ENABLED, imtransbox);
	g_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(update_convs_wintrans),
		(gpointer) OPT_WINTRANS_IM_ENABLED);

	trans_box = gtk_vbox_new(FALSE, 18);
	if (!gaim_prefs_get_bool(OPT_WINTRANS_IM_ENABLED))
		gtk_widget_set_sensitive(GTK_WIDGET(trans_box), FALSE);
	gtk_widget_show(trans_box);

	g_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(gaim_gtk_toggle_sensitive), trans_box);

	button = wgaim_button(_("_Show slider bar in IM window"),
		OPT_WINTRANS_IM_SLIDER, trans_box);
	g_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(update_convs_wintrans),
		(gpointer) OPT_WINTRANS_IM_SLIDER);

	button = gaim_gtk_prefs_checkbox(
		_("Remove IM window transparency on focus"),
		OPT_WINTRANS_IM_ONFOCUS, trans_box);

	button = wgaim_button(_("Always on top"), OPT_WINTRANS_IM_ONTOP,
		trans_box);
	g_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(update_convs_wintrans),
		(gpointer) OPT_WINTRANS_IM_ONTOP);

	gtk_box_pack_start(GTK_BOX(imtransbox), trans_box, FALSE, FALSE, 5);

	/* IM transparency slider */
	hbox = gtk_hbox_new(FALSE, 5);

	label = gtk_label_new(_("Opacity:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	slider = gtk_hscale_new_with_range(50, 255, 1);
	gtk_range_set_value(GTK_RANGE(slider),
		gaim_prefs_get_int(OPT_WINTRANS_IM_ALPHA));
	gtk_widget_set_usize(GTK_WIDGET(slider), 200, -1);

	g_signal_connect(GTK_OBJECT(slider), "value-changed",
		GTK_SIGNAL_FUNC(alpha_change), NULL);
	g_signal_connect(GTK_OBJECT(slider), "focus-out-event",
		GTK_SIGNAL_FUNC(alpha_pref_set_int),
		(gpointer) OPT_WINTRANS_IM_ALPHA);

	gtk_box_pack_start(GTK_BOX(hbox), slider, FALSE, TRUE, 5);

	gtk_widget_show_all(hbox);

	gtk_box_pack_start(GTK_BOX(trans_box), hbox, FALSE, FALSE, 5);

	/* Buddy List trans options */
	bltransbox = gaim_gtk_make_frame (ret, _("Buddy List Window"));
	button = wgaim_button(_("_Buddy List window transparency"),
		OPT_WINTRANS_BL_ENABLED, bltransbox);
	g_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(set_blist_trans),
		(gpointer) OPT_WINTRANS_BL_ENABLED);

	trans_box = gtk_vbox_new(FALSE, 18);
	if (!gaim_prefs_get_bool(OPT_WINTRANS_BL_ENABLED))
		gtk_widget_set_sensitive(GTK_WIDGET(trans_box), FALSE);
	gtk_widget_show(trans_box);
	g_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(gaim_gtk_toggle_sensitive), trans_box);
	button = gaim_gtk_prefs_checkbox(
		_("Remove Buddy List window transparency on focus"),
		OPT_WINTRANS_BL_ONFOCUS, trans_box);
	button = wgaim_button(_("Always on top"), OPT_WINTRANS_BL_ONTOP,
		trans_box);
	g_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(set_blist_trans),
		(gpointer) OPT_WINTRANS_BL_ONTOP);
	gtk_box_pack_start(GTK_BOX(bltransbox), trans_box, FALSE, FALSE, 5);

	/* IM transparency slider */
	hbox = gtk_hbox_new(FALSE, 5);

	label = gtk_label_new(_("Opacity:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	slider = gtk_hscale_new_with_range(50, 255, 1);
	gtk_range_set_value(GTK_RANGE(slider),
		gaim_prefs_get_int(OPT_WINTRANS_BL_ALPHA));

	gtk_widget_set_usize(GTK_WIDGET(slider), 200, -1);

	g_signal_connect(GTK_OBJECT(slider), "value-changed",
		GTK_SIGNAL_FUNC(bl_alpha_change), NULL);
	g_signal_connect(GTK_OBJECT(slider), "focus-out-event",
		GTK_SIGNAL_FUNC(alpha_pref_set_int),
		(gpointer) OPT_WINTRANS_BL_ALPHA);

	gtk_box_pack_start(GTK_BOX(hbox), slider, FALSE, TRUE, 5);

	gtk_widget_show_all(hbox);

	gtk_box_pack_start(GTK_BOX(trans_box), hbox, FALSE, FALSE, 5);

	gtk_widget_show_all(ret);
	return ret;
}

static GaimGtkPluginUiInfo ui_info =
{
	get_config_frame,
	0 /* page_num (Reserved) */
};

static GaimPluginInfo info =
{
	GAIM_PLUGIN_MAGIC,
	GAIM_MAJOR_VERSION,
	GAIM_MINOR_VERSION,
	GAIM_PLUGIN_STANDARD,		/**< type           */
	GAIM_GTK_PLUGIN_TYPE,		/**< ui_requirement */
	0,				/**< flags          */
	NULL,				/**< dependencies   */
	GAIM_PRIORITY_DEFAULT,		/**< priority       */
	WINTRANS_PLUGIN_ID,		/**< id             */
	N_("Transparency"),		/**< name           */
	VERSION,			/**< version        */
					/**  summary        */
	N_("Variable Transparency for the buddy list and conversations."),
					/**  description    */
	N_("This plugin enables variable alpha transparency on conversation windows and the buddy list.\n\n"
	"* Note: This plugin requires Win2000 or greater."),
	"Herman Bloggs <hermanator12002@yahoo.com>",	/**< author         */
	GAIM_WEBSITE,			/**< homepage       */
	plugin_load,			/**< load           */
	plugin_unload,			/**< unload         */
	NULL,				/**< destroy        */
	&ui_info,			/**< ui_info        */
	NULL,				/**< extra_info     */
	NULL,				/**< prefs_info     */
	NULL				/**< actions        */
};

static void
init_plugin(GaimPlugin *plugin)
{
	gaim_prefs_add_none("/plugins/gtk/win32");
	gaim_prefs_add_none("/plugins/gtk/win32/wintrans");
	gaim_prefs_add_bool(OPT_WINTRANS_IM_ENABLED, FALSE);
	gaim_prefs_add_int(OPT_WINTRANS_IM_ALPHA, 255);
	gaim_prefs_add_bool(OPT_WINTRANS_IM_SLIDER, FALSE);
	gaim_prefs_add_bool(OPT_WINTRANS_IM_ONFOCUS, FALSE);
	gaim_prefs_add_bool(OPT_WINTRANS_IM_ONTOP, FALSE);
	gaim_prefs_add_bool(OPT_WINTRANS_BL_ENABLED, FALSE);
	gaim_prefs_add_int(OPT_WINTRANS_BL_ALPHA, 255);
	gaim_prefs_add_bool(OPT_WINTRANS_BL_ONFOCUS, FALSE);
	gaim_prefs_add_bool(OPT_WINTRANS_BL_ONTOP, FALSE);
}

GAIM_INIT_PLUGIN(wintrans, init_plugin, info)
