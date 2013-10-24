/*
 *   This is an example of a purple dbus plugin.  After enabling this
 *   plugin, the following commands should work from the command line:
 *
 *   prompt$ purple-send DbusExampleGetHelloObject
 *
 *     returns, say: int32 74
 *
 *   prompt$ purple-send DbusExampleGetText int32:74
 *
 *     returns: string "Hello."
 *
 *   prompt$ purple-send DbusExampleSetText int32:74 string:Bye!
 *
 *   prompt$ purple-send DbusExampleGetText int32:74
 *
 *     returns: string "Bye!"
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

#include "internal.h"
#include "purple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include "dbus-maybe.h"
#include "dbus-bindings.h"

typedef struct {
	char *text;
} PurpleText;

/* This makes the structure PurpleText visible to the purple-dbus type
   system.  It defines PurpleText as a type with no parent.  From now
   on, we will be able to register pointers to structures of this
   type.  You to dbus-define types you want to be directly accessible
   by external applications. */
PURPLE_DBUS_DEFINE_TYPE(PurpleText)

/* Here we make four functions accessible to other applications by
   DBus.  These functions can access types defined in purple proper
   (PurpleBuddy) as well as the types defined in the plugin (PurpleText).  */
DBUS_EXPORT PurpleText* dbus_example_get_hello_object(void);
DBUS_EXPORT void dbus_example_set_text(PurpleText *obj, const char *text);
DBUS_EXPORT const char *dbus_example_get_text(PurpleText *obj);
DBUS_EXPORT const char *dbus_example_get_buddy_name(PurpleBuddy *buddy);

/* This file has been generated by the #dbus-analize-functions.py
   script.  It contains dbus wrappers for the four functions declared
   above. */
#include "dbus-example-bindings.c"

/* This is the PurpleText object we want to make publicly visible. */
static PurpleText hello;

/* Here come the definitions of the four exported functions. */
PurpleText* dbus_example_get_hello_object(void)
{
	return &hello;
}

void dbus_example_set_text(PurpleText *obj, const char *text)
{
	if (obj != NULL) {
		g_free(obj->text);
		obj->text = g_strdup(text);
	}
}

const char *dbus_example_get_text(PurpleText *obj)
{
	if (obj != NULL)
		return obj->text;
	else
		return NULL;
}

const char *dbus_example_get_buddy_name(PurpleBuddy *buddy)
{
	return purple_buddy_get_name(buddy);
}

/* And now standard plugin stuff */

static gboolean
plugin_load(PurplePlugin *plugin)
{
	PURPLE_DBUS_RETURN_FALSE_IF_DISABLED(plugin);

	/* First, we have to register our four exported functions with the
	   main purple dbus loop.  Without this statement, the purple dbus
	   code wouldn't know about our functions. */
	PURPLE_DBUS_REGISTER_BINDINGS(plugin);

	/* Then, we register the hello object of type PurpleText.  Note that
	   pointer registrations / unregistrations are completely dynamic;
	   they don't have to be made when the plugin is loaded /
	   unloaded.  Without this statement the dbus purple code wouldn't
	   know about the hello object.  */
	PURPLE_DBUS_REGISTER_POINTER(&hello, PurpleText);

	hello.text = g_strdup("Hello.");

	return TRUE;
}


static gboolean
plugin_unload(PurplePlugin *plugin)
{
	g_free(hello.text);

	/* It is necessary to unregister all pointers registered by the module. */
	PURPLE_DBUS_UNREGISTER_POINTER(&hello);

	return TRUE;
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                             /**< type           */
	NULL,                                             /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                            /**< priority       */

	"dbus-example",                                   /**< id             */
	N_("DBus Example"),                               /**< name           */
	DISPLAY_VERSION,                                  /**< version        */
	                                                  /**  summary        */
	N_("DBus Plugin Example"),
	                                                  /**  description    */
	N_("DBus Plugin Example"),
	"Piotr Zielinski (http://cl.cam.ac.uk/~pz215)",   /**< author         */
	PURPLE_WEBSITE,                                     /**< homepage       */

	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */

	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,                                       /**< prefs_info     */
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static void init_plugin(PurplePlugin *plugin)
{
}

PURPLE_INIT_PLUGIN(dbus_example, init_plugin, info)
