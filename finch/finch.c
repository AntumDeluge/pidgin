/**
 * finch
 *
 * Finch is the legal property of its developers, whose names are too numerous
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
#include "account.h"
#include "conversation.h"
#include "core.h"
#include "debug.h"
#include "eventloop.h"
#include "ft.h"
#include "log.h"
#include "notify.h"
#include "prefs.h"
#include "prpl.h"
#include "pounce.h"
#include "savedstatuses.h"
#include "sound.h"
#include "status.h"
#include "util.h"
#include "whiteboard.h"

#include "gntdebug.h"
#include "finch.h"
#include "gntprefs.h"
#include "gntui.h"
#include "gntidle.h"

#define _GNU_SOURCE
#include <getopt.h>

#include "config.h"

static void
debug_init()
{
	finch_debug_init();
	purple_debug_set_ui_ops(finch_debug_get_ui_ops());
}

static PurpleCoreUiOps core_ops =
{
	finch_prefs_init,
	debug_init,
	gnt_ui_init,
	gnt_ui_uninit,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static PurpleCoreUiOps *
gnt_core_get_ui_ops()
{
	return &core_ops;
}

/* Anything IO-related is directly copied from gtkpurple's source tree */

#define FINCH_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define FINCH_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

typedef struct _PurpleGntIOClosure {
	PurpleInputFunction function;
	guint result;
	gpointer data;

} PurpleGntIOClosure;

static void purple_gnt_io_destroy(gpointer data)
{
	g_free(data);
}

static gboolean purple_gnt_io_invoke(GIOChannel *source, GIOCondition condition, gpointer data)
{
	PurpleGntIOClosure *closure = data;
	PurpleInputCondition purple_cond = 0;

	if (condition & FINCH_READ_COND)
		purple_cond |= PURPLE_INPUT_READ;
	if (condition & FINCH_WRITE_COND)
		purple_cond |= PURPLE_INPUT_WRITE;

#if 0
	purple_debug(PURPLE_DEBUG_MISC, "gtk_eventloop",
			   "CLOSURE: callback for %d, fd is %d\n",
			   closure->result, g_io_channel_unix_get_fd(source));
#endif

#ifdef _WIN32
	if(! purple_cond) {
#if DEBUG
		purple_debug_misc("gnt_eventloop",
			   "CLOSURE received GIOCondition of 0x%x, which does not"
			   " match 0x%x (READ) or 0x%x (WRITE)\n",
			   condition, FINCH_READ_COND, FINCH_WRITE_COND);
#endif /* DEBUG */

		return TRUE;
	}
#endif /* _WIN32 */

	closure->function(closure->data, g_io_channel_unix_get_fd(source),
			  purple_cond);

	return TRUE;
}

static guint gnt_input_add(gint fd, PurpleInputCondition condition, PurpleInputFunction function,
							   gpointer data)
{
	PurpleGntIOClosure *closure = g_new0(PurpleGntIOClosure, 1);
	GIOChannel *channel;
	GIOCondition cond = 0;

	closure->function = function;
	closure->data = data;

	if (condition & PURPLE_INPUT_READ)
		cond |= FINCH_READ_COND;
	if (condition & PURPLE_INPUT_WRITE)
		cond |= FINCH_WRITE_COND;

	channel = g_io_channel_unix_new(fd);
	closure->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
					      purple_gnt_io_invoke, closure, purple_gnt_io_destroy);

	g_io_channel_unref(channel);
	return closure->result;
}

static PurpleEventLoopUiOps eventloop_ops =
{
	g_timeout_add,
	g_source_remove,
	gnt_input_add,
	g_source_remove,
	NULL, /* input_get_error */

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static PurpleEventLoopUiOps *
gnt_eventloop_get_ui_ops(void)
{
	return &eventloop_ops;
}

/* This is mostly copied from gtkpurple's source tree */
static void
show_usage(const char *name, gboolean terse)
{
	char *text;

	if (terse) {
		text = g_strdup_printf(_("%s. Try `%s -h' for more information.\n"), VERSION, name);
	} else {
		text = g_strdup_printf(_("%s\n"
		       "Usage: %s [OPTION]...\n\n"
		       "  -c, --config=DIR    use DIR for config files\n"
		       "  -d, --debug         print debugging messages to stdout\n"
		       "  -h, --help          display this help and exit\n"
		       "  -n, --nologin       don't automatically login\n"
		       "  -v, --version       display the current version and exit\n"), VERSION, name);
	}

	purple_print_utf8_to_console(stdout, text);
	g_free(text);
}

static int
init_libpurple(int argc, char **argv)
{
	char *path;
	int opt;
	gboolean opt_help = FALSE;
	gboolean opt_nologin = FALSE;
	gboolean opt_version = FALSE;
	char *opt_config_dir_arg = NULL;
	char *opt_session_arg = NULL;
	gboolean debug_enabled = FALSE;

	struct option long_options[] = {
		{"config",   required_argument, NULL, 'c'},
		{"debug",    no_argument,       NULL, 'd'},
		{"help",     no_argument,       NULL, 'h'},
		{"nologin",  no_argument,       NULL, 'n'},
		{"session",  required_argument, NULL, 's'},
		{"version",  no_argument,       NULL, 'v'},
		{0, 0, 0, 0}
	};

#ifdef ENABLE_NLS
	bindtextdomain(PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(PACKAGE, "UTF-8");
	textdomain(PACKAGE);
#endif

#ifdef HAVE_SETLOCALE
	setlocale(LC_ALL, "");
#endif

	/* scan command-line options */
	opterr = 1;
	while ((opt = getopt_long(argc, argv,
#ifndef _WIN32
				  "c:dhn::s:v",
#else
				  "c:dhn::v",
#endif
				  long_options, NULL)) != -1) {
		switch (opt) {
		case 'c':	/* config dir */
			g_free(opt_config_dir_arg);
			opt_config_dir_arg = g_strdup(optarg);
			break;
		case 'd':	/* debug */
			debug_enabled = TRUE;
			break;
		case 'h':	/* help */
			opt_help = TRUE;
			break;
		case 'n':	/* no autologin */
			opt_nologin = TRUE;
			break;
		case 's':	/* use existing session ID */
			g_free(opt_session_arg);
			opt_session_arg = g_strdup(optarg);
			break;
		case 'v':	/* version */
			opt_version = TRUE;
			break;
		case '?':	/* show terse help */
		default:
			show_usage(argv[0], TRUE);
			return 0;
			break;
		}
	}

	/* show help message */
	if (opt_help) {
		show_usage(argv[0], FALSE);
		return 0;
	}
	/* show version message */
	if (opt_version) {
		/* Translators may want to transliterate the name.
		 It is not to be translated. */
		printf("%s %s\n", _("Finch"), VERSION);
		return 0;
	}

	/* set a user-specified config directory */
	if (opt_config_dir_arg != NULL) {
		purple_util_set_user_dir(opt_config_dir_arg);
		g_free(opt_config_dir_arg);
	}

	/*
	 * We're done piddling around with command line arguments.
	 * Fire up this baby.
	 */

	/* We don't want debug-messages to show up and corrupt the display */
	purple_debug_set_enabled(debug_enabled);

	/* If we're using a custom configuration directory, we
	 * do NOT want to migrate, or weird things will happen. */
	if (opt_config_dir_arg == NULL)
	{
		if (!purple_core_migrate())
		{
			char *old = g_strconcat(purple_home_dir(),
			                        G_DIR_SEPARATOR_S ".gaim", NULL);
			char *text = g_strdup_printf(_(
				"%s encountered errors migrating your settings "
				"from %s to %s. Please investigate and complete the "
				"migration by hand. Please report this error at http://developer.pidgin.im"), _("Finch"),
				old, purple_user_dir());

			g_free(old);

			purple_print_utf8_to_console(stderr, text);
			g_free(text);

			return 0;
		}
	}

	purple_core_set_ui_ops(gnt_core_get_ui_ops());
	purple_eventloop_set_ui_ops(gnt_eventloop_get_ui_ops());
	purple_idle_set_ui_ops(finch_idle_get_ui_ops());

	path = g_build_filename(purple_user_dir(), "plugins", NULL);
	purple_plugins_add_search_path(path);
	g_free(path);

	purple_plugins_add_search_path(LIBDIR);

	if (!purple_core_init(FINCH_UI))
	{
		fprintf(stderr,
				"Initialization of the Purple core failed. Dumping core.\n"
				"Please report this!\n");
		abort();
	}

	/* TODO: Move blist loading into purple_blist_init() */
	purple_set_blist(purple_blist_new());
	purple_blist_load();

	/* TODO: Move prefs loading into purple_prefs_init() */
	purple_prefs_load();
	purple_prefs_update_old();
	finch_prefs_update_old();

	/* load plugins we had when we quit */
	purple_plugins_load_saved("/finch/plugins/loaded");

	/* TODO: Move pounces loading into purple_pounces_init() */
	purple_pounces_load();

	if (opt_nologin)
	{
		/* Set all accounts to "offline" */
		PurpleSavedStatus *saved_status;

		/* If we've used this type+message before, lookup the transient status */
		saved_status = purple_savedstatus_find_transient_by_type_and_message(
							PURPLE_STATUS_OFFLINE, NULL);

		/* If this type+message is unique then create a new transient saved status */
		if (saved_status == NULL)
			saved_status = purple_savedstatus_new(NULL, PURPLE_STATUS_OFFLINE);

		/* Set the status for each account */
		purple_savedstatus_activate(saved_status);
	}
	else
	{
		/* Everything is good to go--sign on already */
		if (!purple_prefs_get_bool("/purple/savedstatus/startup_current_status"))
			purple_savedstatus_activate(purple_savedstatus_get_startup());
		purple_accounts_restore_current_statuses();
	}

	return 1;
}

int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);

	/* Initialize the libpurple stuff */
	if (!init_libpurple(argc, argv))
		return 0;

	purple_blist_show();
	gnt_main();

#ifdef STANDALONE
	purple_core_quit();
#endif

	return 0;
}

