/*
 * GtkIMHtml
 *
 * Copyright (C) 2000, Eric Warmenhoven <warmenhoven@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * under the terms of the GNU General Public License as published by
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "gtkimhtml.h"
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#include <locale.h>
#endif

#if GTK_CHECK_VERSION(1,3,0)
#  define GTK_IMHTML_GET_STYLE_FONT(style) gtk_style_get_font (style)
#else
#  define GTK_IMHTML_GET_STYLE_FONT(style) (style)->font
#  define GTK_CLASS_TYPE(class) (class)->type
#endif

#include "pixmaps/angel.xpm"
#include "pixmaps/bigsmile.xpm"
#include "pixmaps/burp.xpm"
#include "pixmaps/crossedlips.xpm"
#include "pixmaps/cry.xpm"
#include "pixmaps/embarrassed.xpm"
#include "pixmaps/kiss.xpm"
#include "pixmaps/moneymouth.xpm"
#include "pixmaps/sad.xpm"
#include "pixmaps/scream.xpm"
#include "pixmaps/smile.xpm"
#include "pixmaps/smile8.xpm"
#include "pixmaps/think.xpm"
#include "pixmaps/tongue.xpm"
#include "pixmaps/wink.xpm"
#include "pixmaps/yell.xpm"

#define MAX_FONT_SIZE 7
#define DEFAULT_FONT_SIZE 3

#define POINT_SIZE(x) (_point_sizes [MIN ((x), MAX_FONT_SIZE) - 1])
static gint _point_sizes [] = { 80, 100, 120, 140, 200, 300, 400 };

#define DEFAULT_PRE_FACE "courier"

#define BORDER_SIZE 2
#define TOP_BORDER 10
#define MIN_HEIGHT 20
#define HR_HEIGHT 2
#define TOOLTIP_TIMEOUT 500

#define DIFF(a, b) (((a) > (b)) ? ((a) - (b)) : ((b) - (a)))
#define COLOR_MOD  0x8000
#define COLOR_DIFF 0x800

#define TYPE_TEXT     0
#define TYPE_SMILEY   1
#define TYPE_IMG      2
#define TYPE_SEP      3
#define TYPE_BR       4
#define TYPE_COMMENT  5

#define DRAW_IMG(x) (((x)->type == TYPE_IMG) || (imhtml->smileys && ((x)->type == TYPE_SMILEY)))

typedef struct _GtkIMHtmlBit GtkIMHtmlBit;
typedef struct _FontDetail   FontDetail;

struct _GtkSmileyTree {
	GString *values;
	GtkSmileyTree **children;
	gchar **image;
};

static GtkSmileyTree*
gtk_smiley_tree_new ()
{
	return g_new0 (GtkSmileyTree, 1);
}

static void
gtk_smiley_tree_insert (GtkSmileyTree *tree,
			const gchar   *text,
			gchar        **image)
{
	GtkSmileyTree *t = tree;
	const gchar *x = text;

	if (!strlen (x))
		return;

	while (*x) {
		gchar *pos;
		gint index;

		if (!t->values)
			t->values = g_string_new ("");

		pos = strchr (t->values->str, *x);
		if (!pos) {
			t->values = g_string_append_c (t->values, *x);
			index = t->values->len - 1;
			t->children = g_realloc (t->children, t->values->len * sizeof (GtkSmileyTree *));
			t->children [index] = g_new0 (GtkSmileyTree, 1);
		} else
			index = (int) pos - (int) t->values->str;

		t = t->children [index];

		x++;
	}

	t->image = image;
}

static void
gtk_smiley_tree_remove (GtkSmileyTree *tree,
			const gchar   *text)
{
	GtkSmileyTree *t = tree;
	const gchar *x = text;
	gint len = 0;

	while (*x) {
		gchar *pos;

		if (!t->values)
			return;

		pos = strchr (t->values->str, *x);
		if (pos)
			t = t->children [(int) pos - (int) t->values->str];
		else
			return;

		x++; len++;
	}

	if (t->image)
		t->image = NULL;
}

static gint
gtk_smiley_tree_lookup (GtkSmileyTree *tree,
			const gchar   *text)
{
	GtkSmileyTree *t = tree;
	const gchar *x = text;
	gint len = 0;

	while (*x) {
		gchar *pos;

		if (t->image)
			return len;

		if (!t->values)
			return 0;

		pos = strchr (t->values->str, *x);
		if (pos)
			t = t->children [(int) pos - (int) t->values->str];
		else
			return 0;

		x++; len++;
	}

	if (t->image)
		return len;

	return 0;
}

static gchar**
gtk_smiley_tree_image (GtkSmileyTree *tree,
		       const gchar   *text)
{
	GtkSmileyTree *t = tree;
	const gchar *x = text;

	while (*x) {
		gchar *pos;

		if (!t->values)
			return NULL;

		pos = strchr (t->values->str, *x);
		if (pos) {
			t = t->children [(int) pos - (int) t->values->str];
		} else
			return NULL;

		x++;
	}

	return t->image;
}

static void
gtk_smiley_tree_destroy (GtkSmileyTree *tree)
{
	GSList *list = g_slist_append (NULL, tree);

	while (list) {
		GtkSmileyTree *t = list->data;
		gint i;
		list = g_slist_remove(list, t);
		if (t->values) {
			for (i = 0; i < t->values->len; i++)
				list = g_slist_append (list, t->children [i]);
			g_string_free (t->values, TRUE);
			g_free (t->children);
		}
		g_free (t);
	}
}

void
gtk_imhtml_remove_smileys (GtkIMHtml *imhtml)
{
	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	gtk_smiley_tree_destroy (imhtml->smiley_data);
	imhtml->smiley_data = gtk_smiley_tree_new ();
}

struct _GtkIMHtmlBit {
	gint type;

	gchar *text;
	GdkPixmap *pm;
	GdkBitmap *bm;

	GdkFont *font;
	GdkColor *fore;
	GdkColor *back;
	GdkColor *bg;
	gboolean underline;
	gboolean strike;
	gchar *url;

	GList *chunks;
};

struct _FontDetail {
	gushort size;
	gchar *face;
	GdkColor *fore;
	GdkColor *back;
};

struct line_info {
	gint x;
	gint y;
	gint width;
	gint height;
	gint ascent;

	gboolean selected;
	gchar *sel_start;
	gchar *sel_end;

	gchar *text;
	GtkIMHtmlBit *bit;
};

struct clickable {
	gint x;
	gint y;
	gint width;
	gint height;
	GtkIMHtml *imhtml;
	GtkIMHtmlBit *bit;
};

static GtkLayoutClass *parent_class = NULL;

enum {
	TARGET_STRING,
	TARGET_TEXT,
	TARGET_COMPOUND_TEXT
};

enum {
	URL_CLICKED,
	LAST_SIGNAL
};
static guint signals [LAST_SIGNAL] = { 0 };

static void      gtk_imhtml_draw_bit            (GtkIMHtml *, GtkIMHtmlBit *);
static GdkColor *gtk_imhtml_get_color           (const gchar *);
static gint      gtk_imhtml_motion_notify_event (GtkWidget *, GdkEventMotion *);

static void
#if GTK_CHECK_VERSION(1,3,0)
gtk_imhtml_finalize (GObject *object)
#else
gtk_imhtml_destroy (GtkObject *object)
#endif
{
	GtkIMHtml *imhtml;

	imhtml = GTK_IMHTML (object);

	gtk_imhtml_clear (imhtml);

	if (imhtml->selected_text)
		g_string_free (imhtml->selected_text, TRUE);

	if (imhtml->default_font)
		gdk_font_unref (imhtml->default_font);
	if (imhtml->default_fg_color)
		gdk_color_free (imhtml->default_fg_color);
	if (imhtml->default_bg_color)
		gdk_color_free (imhtml->default_bg_color);

	gdk_cursor_destroy (imhtml->hand_cursor);
	gdk_cursor_destroy (imhtml->arrow_cursor);

	gtk_smiley_tree_destroy (imhtml->smiley_data);

#if GTK_CHECK_VERSION(1,3,0)
	G_OBJECT_CLASS (parent_class)->finalize (object);
#else
	if (GTK_OBJECT_CLASS (parent_class)->destroy != NULL)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
#endif
}

static void
gtk_imhtml_realize (GtkWidget *widget)
{
	GtkIMHtml *imhtml;
	GdkWindowAttr attributes;
	gint attributes_mask;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_IMHTML (widget));

	imhtml = GTK_IMHTML (widget);
	GTK_WIDGET_SET_FLAGS (imhtml, GTK_REALIZED);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width;
	attributes.height = widget->allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);
	attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK;

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
					 &attributes, attributes_mask);
	gdk_window_set_user_data (widget->window, widget);

#if GTK_CHECK_VERSION(1,3,0)
	attributes.x = widget->style->xthickness + BORDER_SIZE;
	attributes.y = widget->style->xthickness + BORDER_SIZE;
#else
	attributes.x = widget->style->klass->xthickness + BORDER_SIZE;
	attributes.y = widget->style->klass->xthickness + BORDER_SIZE;
#endif
	attributes.width = MAX (1, (gint) widget->allocation.width - (gint) attributes.x * 2);
	attributes.height = MAX (1, (gint) widget->allocation.height - (gint) attributes.y * 2);
	attributes.event_mask = gtk_widget_get_events (widget)
				| GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
				| GDK_POINTER_MOTION_MASK | GDK_EXPOSURE_MASK | GDK_LEAVE_NOTIFY_MASK;

	GTK_LAYOUT (imhtml)->bin_window = gdk_window_new (widget->window,
							  &attributes, attributes_mask);
	gdk_window_set_user_data (GTK_LAYOUT (imhtml)->bin_window, widget);

	widget->style = gtk_style_attach (widget->style, widget->window);

	gdk_window_set_cursor (widget->window, imhtml->arrow_cursor);

	imhtml->default_font = gdk_font_ref (GTK_IMHTML_GET_STYLE_FONT (widget->style));

	gdk_window_set_background (widget->window, &widget->style->base [GTK_STATE_NORMAL]);
	gdk_window_set_background (GTK_LAYOUT (imhtml)->bin_window,
				   &widget->style->base [GTK_STATE_NORMAL]);

	imhtml->default_fg_color = gdk_color_copy (&GTK_WIDGET (imhtml)->style->fg [GTK_STATE_NORMAL]);
	imhtml->default_bg_color = gdk_color_copy (&GTK_WIDGET (imhtml)->style->base [GTK_STATE_NORMAL]);

	gdk_window_show (GTK_LAYOUT (imhtml)->bin_window);
}

static gboolean
similar_colors (GdkColor *bg,
		GdkColor *fg)
{
	if ((DIFF (bg->red, fg->red) < COLOR_DIFF) &&
	    (DIFF (bg->green, fg->green) < COLOR_DIFF) &&
	    (DIFF (bg->blue, fg->blue) < COLOR_DIFF)) {
		fg->red = (0xff00 - COLOR_MOD > bg->red) ?
			bg->red + COLOR_MOD : bg->red - COLOR_MOD;
		fg->green = (0xff00 - COLOR_MOD > bg->green) ?
			bg->green + COLOR_MOD : bg->green - COLOR_MOD;
		fg->blue = (0xff00 - COLOR_MOD > bg->blue) ?
			bg->blue + COLOR_MOD : bg->blue - COLOR_MOD;
		return TRUE;
	}
	return FALSE;
}

static void
draw_text (GtkIMHtml        *imhtml,
	   struct line_info *line)
{
	GtkIMHtmlBit *bit;
	GdkGC *gc;
	GdkColormap *cmap;
	GdkWindow *window = GTK_LAYOUT (imhtml)->bin_window;
	gfloat xoff, yoff;
	GdkColor *bg, *fg;

	if (GTK_LAYOUT (imhtml)->freeze_count)
		return;

	bit = line->bit;
	gc = gdk_gc_new (window);
	cmap = gtk_widget_get_colormap (GTK_WIDGET (imhtml));
	xoff = GTK_LAYOUT (imhtml)->hadjustment->value;
	yoff = GTK_LAYOUT (imhtml)->vadjustment->value;

	if (bit->bg != NULL) {
		gdk_color_alloc (cmap, bit->bg);
		gdk_gc_set_foreground (gc, bit->bg);
		bg = bit->bg;
	} else {
		gdk_color_alloc (cmap, imhtml->default_bg_color);
		gdk_gc_set_foreground (gc, imhtml->default_bg_color);
		bg = imhtml->default_bg_color;
	}

	gdk_draw_rectangle (window, gc, TRUE, line->x - xoff, line->y - yoff,
			    line->width ? line->width : imhtml->xsize, line->height);

	if (!line->text) {
		gdk_gc_unref (gc);
		return;
	}

	if (bit->back != NULL) {
		gdk_color_alloc (cmap, bit->back);
		gdk_gc_set_foreground (gc, bit->back);
		gdk_draw_rectangle (window, gc, TRUE, line->x - xoff, line->y - yoff,
				    gdk_string_width (bit->font, line->text), line->height);
		bg = bit->back;
	}

	bg = gdk_color_copy (bg);

	if (line->selected) {
		gint width, x;
		gchar *start, *end;
		GdkColor col;

		if ((line->sel_start > line->sel_end) && (line->sel_end != NULL)) {
			start = line->sel_end;
			end = line->sel_start;
		} else {
			start = line->sel_start;
			end = line->sel_end;
		}

		if (start == NULL)
			x = 0;
		else
			x = gdk_text_width (bit->font, line->text, start - line->text);

		if (end == NULL)
			width = gdk_string_width (bit->font, line->text) - x;
		else
			width = gdk_text_width (bit->font, line->text, end - line->text) - x;

		col.red = col.green = col.blue = 0xc000;
		gdk_color_alloc (cmap, &col);
		gdk_gc_set_foreground (gc, &col);

		gdk_draw_rectangle (window, gc, TRUE, x + line->x - xoff, line->y - yoff,
				    width, line->height);
	}

	if (bit->url) {
		GdkColor *tc = gtk_imhtml_get_color ("#0000a0");
		gdk_color_alloc (cmap, tc);
		gdk_gc_set_foreground (gc, tc);
		fg = gdk_color_copy (tc);
		gdk_color_free (tc);
	} else if (bit->fore) {
		gdk_color_alloc (cmap, bit->fore);
		gdk_gc_set_foreground (gc, bit->fore);
		fg = gdk_color_copy (bit->fore);
	} else {
		gdk_color_alloc (cmap, imhtml->default_fg_color);
		gdk_gc_set_foreground (gc, imhtml->default_fg_color);
		fg = gdk_color_copy (imhtml->default_fg_color);
	}

	if (similar_colors (bg, fg)) {
		gdk_color_alloc (cmap, fg);
		gdk_gc_set_foreground (gc, fg);
	}
	gdk_color_free (bg);
	gdk_color_free (fg);

	gdk_draw_string (window, bit->font, gc, line->x - xoff,
			 line->y - yoff + line->ascent, line->text);

	if (bit->underline || bit->url)
		gdk_draw_rectangle (window, gc, TRUE, line->x - xoff, line->y - yoff + line->ascent + 1,
				    gdk_string_width (bit->font, line->text), 1);
	if (bit->strike)
		gdk_draw_rectangle (window, gc, TRUE, line->x - xoff,
				    line->y - yoff + line->ascent - (bit->font->ascent / 2),
				    gdk_string_width (bit->font, line->text), 1);

	gdk_gc_unref (gc);
}

static void
draw_img (GtkIMHtml        *imhtml,
	  struct line_info *line)
{
	GtkIMHtmlBit *bit;
	GdkGC *gc;
	GdkColormap *cmap;
	gint width, height, hoff;
	GdkWindow *window = GTK_LAYOUT (imhtml)->bin_window;
	gfloat xoff, yoff;

	if (GTK_LAYOUT (imhtml)->freeze_count)
		return;

	bit = line->bit;
	gdk_window_get_size (bit->pm, &width, &height);
	hoff = (line->height - height) / 2;
	xoff = GTK_LAYOUT (imhtml)->hadjustment->value;
	yoff = GTK_LAYOUT (imhtml)->vadjustment->value;
	gc = gdk_gc_new (window);
	cmap = gtk_widget_get_colormap (GTK_WIDGET (imhtml));

	if (bit->bg != NULL) {
		gdk_color_alloc (cmap, bit->bg);
		gdk_gc_set_foreground (gc, bit->bg);
	} else {
		gdk_color_alloc (cmap, imhtml->default_bg_color);
		gdk_gc_set_foreground (gc, imhtml->default_bg_color);
	}

	gdk_draw_rectangle (window, gc, TRUE, line->x - xoff, line->y - yoff, line->width, line->height);

	if (bit->back != NULL) {
		gdk_color_alloc (cmap, bit->back);
		gdk_gc_set_foreground (gc, bit->back);
		gdk_draw_rectangle (window, gc, TRUE, line->x - xoff, line->y - yoff,
				    width, line->height);
	}

	gdk_draw_pixmap (window, gc, bit->pm, 0, 0, line->x - xoff, line->y - yoff + hoff, -1, -1);

	gdk_gc_unref (gc);
}

static void
draw_line (GtkIMHtml        *imhtml,
	   struct line_info *line)
{
	GtkIMHtmlBit *bit;
	GdkDrawable *drawable;
	GdkColormap *cmap;
	GdkGC *gc;
	guint line_height;
	gfloat xoff, yoff;

	if (GTK_LAYOUT (imhtml)->freeze_count)
		return;

	xoff = GTK_LAYOUT (imhtml)->hadjustment->value;
	yoff = GTK_LAYOUT (imhtml)->vadjustment->value;
	bit = line->bit;
	drawable = GTK_LAYOUT (imhtml)->bin_window;
	cmap = gtk_widget_get_colormap (GTK_WIDGET (imhtml));
	gc = gdk_gc_new (drawable);

	if (bit->bg != NULL) {
		gdk_color_alloc (cmap, bit->bg);
		gdk_gc_set_foreground (gc, bit->bg);

		gdk_draw_rectangle (drawable, gc, TRUE, line->x - xoff, line->y - yoff,
				    line->width, line->height);
	}

	gdk_color_alloc (cmap, imhtml->default_fg_color);
	gdk_gc_set_foreground (gc, imhtml->default_fg_color);

	line_height = line->height / 2;

	gdk_draw_rectangle (drawable, gc, TRUE, line->x - xoff, line->y - yoff + line_height / 2,
			    line->width, line_height);

	gdk_gc_unref (gc);
}

static void
gtk_imhtml_draw_focus (GtkWidget *widget)
{
	GtkIMHtml *imhtml;
	gint x = 0,
	     y = 0,
	     w = 0,
	     h = 0;
	
	imhtml = GTK_IMHTML (widget);

	if (!GTK_WIDGET_DRAWABLE (widget))
		return;

	if (GTK_WIDGET_HAS_FOCUS (widget)) {
		gtk_paint_focus (widget->style, widget->window, NULL, widget, "text", 0, 0,
				 widget->allocation.width - 1, widget->allocation.height - 1);
		x = 1; y = 1; w = 2; h = 2;
	}

	gtk_paint_shadow (widget->style, widget->window, GTK_STATE_NORMAL,
			  GTK_SHADOW_IN, NULL, widget, "text", x, y,
			  widget->allocation.width - w, widget->allocation.height - h);
}

static void
gtk_imhtml_draw_exposed (GtkIMHtml *imhtml)
{
	GList *bits;
	GtkIMHtmlBit *bit;
	GList *chunks;
	struct line_info *line;
	gfloat x, y;
#if GTK_CHECK_VERSION(1,3,0)
	guint32 width, height;
#else
	gint width, height;
#endif

	x = GTK_LAYOUT (imhtml)->hadjustment->value;
	y = GTK_LAYOUT (imhtml)->vadjustment->value;
	gdk_window_get_size (GTK_LAYOUT (imhtml)->bin_window, &width, &height);

	bits = imhtml->bits;

	while (bits) {
		bit = bits->data;
		chunks = bit->chunks;
		if (DRAW_IMG (bit)) {
			if (chunks) {
				line = chunks->data;
				if ((line->x <= x + width) &&
				    (line->y <= y + height) &&
				    (x <= line->x + line->width) &&
				    (y <= line->y + line->height))
					draw_img (imhtml, line);
			}
		} else if (bit->type == TYPE_SEP) {
			if (chunks) {
				line = chunks->data;
				if ((line->x <= x + width) &&
				    (line->y <= y + height) &&
				    (x <= line->x + line->width) &&
				    (y <= line->y + line->height))
					draw_line (imhtml, line);

				line = chunks->next->data;
				if ((line->x <= x + width) &&
				    (line->y <= y + height) &&
				    (x <= line->x + line->width) &&
				    (y <= line->y + line->height))
					draw_text (imhtml, line);
			}
		} else {
			while (chunks) {
				line = chunks->data;
				if ((line->x <= x + width) &&
				    (line->y <= y + height) &&
				    (x <= line->x + line->width) &&
				    (y <= line->y + line->height))
					draw_text (imhtml, line);
				chunks = g_list_next (chunks);
			}
		}
		bits = g_list_next (bits);
	}

	gtk_imhtml_draw_focus (GTK_WIDGET (imhtml));
}

#if !GTK_CHECK_VERSION(1,3,0)
static void
gtk_imhtml_draw (GtkWidget    *widget,
		 GdkRectangle *area)
{
	GtkIMHtml *imhtml;

	imhtml = GTK_IMHTML (widget);
	gtk_imhtml_draw_exposed (imhtml);
}
#endif

static void
gtk_imhtml_style_set (GtkWidget *widget,
		      GtkStyle  *style)
{
	GtkIMHtml *imhtml;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_IMHTML (widget));

	if (GTK_WIDGET_CLASS (parent_class)->style_set)
		(* GTK_WIDGET_CLASS (parent_class)->style_set) (widget, style);

	if (!GTK_WIDGET_REALIZED (widget))
		return;

	imhtml = GTK_IMHTML (widget);
	gtk_imhtml_draw_exposed (imhtml);
}

static gint
gtk_imhtml_expose_event (GtkWidget      *widget,
			 GdkEventExpose *event)
{
	GtkIMHtml *imhtml;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_IMHTML (widget), FALSE);

	imhtml = GTK_IMHTML (widget);
	gtk_imhtml_draw_exposed (imhtml);

	return FALSE;
}

static void
gtk_imhtml_redraw_all (GtkIMHtml *imhtml)
{
	GList *b;
	GtkIMHtmlBit *bit;
	GtkAdjustment *vadj;
	gfloat oldvalue;
	gint oldy;

	vadj = GTK_LAYOUT (imhtml)->vadjustment;
	oldvalue = vadj->value / vadj->upper;
	oldy = imhtml->y;

	gtk_layout_freeze (GTK_LAYOUT (imhtml));

	g_list_free (imhtml->line);
	imhtml->line = NULL;

	while (imhtml->click) {
		g_free (imhtml->click->data);
		imhtml->click = g_list_remove (imhtml->click, imhtml->click->data);
	}

	imhtml->x = 0;
	imhtml->y = TOP_BORDER;
	imhtml->llheight = 0;
	imhtml->llascent = 0;

	if (GTK_LAYOUT (imhtml)->vadjustment->value < TOP_BORDER)
		gdk_window_clear_area (GTK_LAYOUT (imhtml)->bin_window, 0, 0,
				       imhtml->xsize,
				       TOP_BORDER - GTK_LAYOUT (imhtml)->vadjustment->value);

	b = imhtml->bits;
	while (b) {
		bit = b->data;
		b = g_list_next (b);
		while (bit->chunks) {
			struct line_info *li = bit->chunks->data;
			if (li->text)
				g_free (li->text);
			bit->chunks = g_list_remove (bit->chunks, li);
			g_free (li);
		}
		gtk_imhtml_draw_bit (imhtml, bit);
	}

	GTK_LAYOUT (imhtml)->height = imhtml->y;
	GTK_LAYOUT (imhtml)->vadjustment->upper = imhtml->y;
	gtk_signal_emit_by_name (GTK_OBJECT (GTK_LAYOUT (imhtml)->vadjustment), "changed");

	gtk_widget_set_usize (GTK_WIDGET (imhtml), -1, imhtml->y);
	gtk_adjustment_set_value (vadj, vadj->upper * oldvalue);

	if (GTK_LAYOUT (imhtml)->bin_window && (imhtml->y < oldy)) {
		GdkGC *gc;
		GdkColormap *cmap;

		gc = gdk_gc_new (GTK_LAYOUT (imhtml)->bin_window);
		cmap = gtk_widget_get_colormap (GTK_WIDGET (imhtml));

		gdk_color_alloc (cmap, imhtml->default_bg_color);
		gdk_gc_set_foreground (gc, imhtml->default_bg_color);

		gdk_draw_rectangle (GTK_LAYOUT (imhtml)->bin_window, gc, TRUE,
				    0, imhtml->y - GTK_LAYOUT (imhtml)->vadjustment->value,
				    GTK_WIDGET (imhtml)->allocation.width,
				    oldy - imhtml->y);

		gdk_gc_unref (gc);
	}

	gtk_layout_thaw (GTK_LAYOUT (imhtml));
	gtk_imhtml_draw_focus (GTK_WIDGET (imhtml));
}

static void
gtk_imhtml_size_allocate (GtkWidget     *widget,
			  GtkAllocation *allocation)
{
	GtkIMHtml *imhtml;
	GtkLayout *layout;
	gint new_xsize, new_ysize;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_IMHTML (widget));
	g_return_if_fail (allocation != NULL);

	imhtml = GTK_IMHTML (widget);
	layout = GTK_LAYOUT (widget);

	widget->allocation = *allocation;

#if GTK_CHECK_VERSION(1,3,0)
	new_xsize = MAX (1, (gint) allocation->width -
			    (gint) (widget->style->xthickness + BORDER_SIZE) * 2);
	new_ysize = MAX (1, (gint) allocation->height -
			    (gint) (widget->style->ythickness + BORDER_SIZE) * 2);

	if (GTK_WIDGET_REALIZED (widget)) {
		gint x = widget->style->xthickness + BORDER_SIZE;
		gint y = widget->style->ythickness + BORDER_SIZE;
		gdk_window_move_resize (widget->window,
					allocation->x, allocation->y,
					allocation->width, allocation->height);
		gdk_window_move_resize (layout->bin_window,
					x, y, new_xsize, new_ysize);
	}
#else
	new_xsize = MAX (1, (gint) allocation->width -
			    (gint) (widget->style->klass->xthickness + BORDER_SIZE) * 2);
	new_ysize = MAX (1, (gint) allocation->height -
			    (gint) (widget->style->klass->ythickness + BORDER_SIZE) * 2);

	if (GTK_WIDGET_REALIZED (widget)) {
		gint x = widget->style->klass->xthickness + BORDER_SIZE;
		gint y = widget->style->klass->ythickness + BORDER_SIZE;
		gdk_window_move_resize (widget->window,
					allocation->x, allocation->y,
					allocation->width, allocation->height);
		gdk_window_move_resize (layout->bin_window,
					x, y, new_xsize, new_ysize);
	}
#endif

	layout->hadjustment->page_size = new_xsize;
	layout->hadjustment->page_increment = new_xsize / 2;
	layout->hadjustment->lower = 0;
	layout->hadjustment->upper = imhtml->x;

	layout->vadjustment->page_size = new_ysize;
	layout->vadjustment->page_increment = new_ysize / 2;
	layout->vadjustment->lower = 0;
	layout->vadjustment->upper = imhtml->y;

	gtk_signal_emit_by_name (GTK_OBJECT (layout->hadjustment), "changed");
	gtk_signal_emit_by_name (GTK_OBJECT (layout->vadjustment), "changed");

	if (new_xsize == imhtml->xsize) {
		if ((GTK_LAYOUT (imhtml)->vadjustment->value > imhtml->y - new_ysize)) {
			if (imhtml->y > new_ysize)
				gtk_adjustment_set_value (GTK_LAYOUT (imhtml)->vadjustment,
							  imhtml->y - new_ysize);
			else
				gtk_adjustment_set_value (GTK_LAYOUT (imhtml)->vadjustment, 0);
		}
		return;
	}

	imhtml->xsize = new_xsize;

	if (GTK_WIDGET_REALIZED (widget))
		gtk_imhtml_redraw_all (imhtml);
}

static void
gtk_imhtml_select_none (GtkIMHtml *imhtml)
{
	GList *bits;
	GList *chunks;
	GtkIMHtmlBit *bit;
	struct line_info *chunk;

	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	bits = imhtml->bits;
	while (bits) {
		bit = bits->data;
		chunks = bit->chunks;

		while (chunks) {
			chunk = chunks->data;

			if (chunk->selected) {
				chunk->selected = FALSE;
				chunk->sel_start = chunk->text;
				chunk->sel_end = NULL;
				if (DRAW_IMG (bit))
					draw_img (imhtml, chunk);
				else if ((bit->type == TYPE_SEP) && (bit->chunks->data == chunk))
					draw_line (imhtml, chunk);
				else if (chunk->width)
					draw_text (imhtml, chunk);
			}

			chunks = g_list_next (chunks);
		}

		bits = g_list_next (bits);
	}
	imhtml->sel_endchunk = NULL;
}

static gchar*
get_position (struct line_info *chunk,
	      gint              x,
	      gboolean          smileys)
{
	gint width = x - chunk->x;
	gchar *text;
	gchar *pos;
	guint total = 0;

	switch (chunk->bit->type) {
	case TYPE_TEXT:
	case TYPE_COMMENT:
		text = chunk->text;
		break;
	case TYPE_SMILEY:
		if (smileys)
			return NULL;
		else
			text = chunk->text;
		break;
	default:
		return NULL;
		break;
	}

	if (width <= 0)
		return text;

	for (pos = text; *pos != '\0'; pos++) {
		gint char_width = gdk_text_width (chunk->bit->font, pos, 1);
		if ((width > total) && (width <= total + char_width)) {
			if (width < total + (char_width / 2))
				return pos;
			else
				return ++pos;
		}
		total += char_width;
	}

	return pos;
}

static GString*
append_to_sel (GString          *string,
	       struct line_info *chunk,
	       gboolean          smileys)
{
	GString *new_string;
	gchar *buf;
	gchar *start;
	gint length;

	switch (chunk->bit->type) {
	case TYPE_TEXT:
	case TYPE_COMMENT:
		start = (chunk->sel_start == NULL) ? chunk->text : chunk->sel_start;
		length = (chunk->sel_end == NULL) ? strlen (start) : chunk->sel_end - start;
		if (length <= 0)
			return string;
		buf = g_strndup (start, length);
		break;
	case TYPE_SMILEY:
		if (smileys) {
			start = (chunk->sel_start == NULL) ? chunk->bit->text : chunk->sel_start;
			length = (chunk->sel_end == NULL) ? strlen (start) : chunk->sel_end - start;
			if (length <= 0)
				return string;
			buf = g_strndup (start, length);
		} else {
			start = (chunk->sel_start == NULL) ? chunk->text : chunk->sel_start;
			length = (chunk->sel_end == NULL) ? strlen (start) : chunk->sel_end - start;
			if (length <= 0)
				return string;
			buf = g_strndup (start, length);
		}
		break;
	case TYPE_BR:
		buf = g_strdup ("\n");
		break;
	default:
		return string;
		break;
	}

	new_string = g_string_append (string, buf);
	g_free (buf);

	return new_string;
}

#define COORDS_IN_CHUNK(xx, yy) (((xx) < chunk->x + chunk->width) && \
				 ((yy) < chunk->y + chunk->height))

static void
gtk_imhtml_select_bits (GtkIMHtml *imhtml)
{
	GList *bits;
	GList *chunks;
	GtkIMHtmlBit *bit;
	struct line_info *chunk;

	guint startx = imhtml->sel_startx,
	      starty = imhtml->sel_starty,
	      endx   = imhtml->sel_endx,
	      endy   = imhtml->sel_endy;
	gchar *new_pos;
	gint selection = 0;
	gboolean smileys = imhtml->smileys;
	gboolean redraw = FALSE;
	gboolean got_start = FALSE;
	gboolean got_end = FALSE;

	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	if (!imhtml->selection)
		return;

	if (imhtml->selected_text) {
		g_string_free (imhtml->selected_text, TRUE);
		imhtml->selected_text = g_string_new ("");
	}

	bits = imhtml->bits;
	while (bits) {
		bit = bits->data;
		chunks = bit->chunks;

		while (chunks) {
			chunk = chunks->data;

			switch (selection) {
			case 0:
				if (COORDS_IN_CHUNK (startx, starty)) {
					new_pos = get_position (chunk, startx, smileys);
					if ( !chunk->selected ||
					    (chunk->sel_start != new_pos) ||
					    (chunk->sel_end != NULL))
						redraw = TRUE;
					chunk->selected = TRUE;
					chunk->sel_start = new_pos;
					chunk->sel_end = NULL;
					selection++;
					got_start = TRUE;
				}

				if (COORDS_IN_CHUNK (endx, endy)) {
					if (got_start) {
						new_pos = get_position (chunk, endx, smileys);
						if (chunk->sel_end != new_pos)
							redraw = TRUE;
						if (chunk->sel_start > new_pos) {
							chunk->sel_end = chunk->sel_start;
							chunk->sel_start = new_pos;
						} else
							chunk->sel_end = new_pos;
						selection = 2;
						imhtml->sel_endchunk = chunk;
						got_end = TRUE;
					} else {
						new_pos = get_position (chunk, endx, smileys);
						if ( !chunk->selected ||
						    (chunk->sel_start != new_pos) ||
						    (chunk->sel_end != NULL))
							redraw = TRUE;
						chunk->selected = TRUE;
						chunk->sel_start = new_pos;
						chunk->sel_end = NULL;
						selection++;
						imhtml->sel_endchunk = chunk;
						got_end = TRUE;
					}
				} else if (!COORDS_IN_CHUNK (startx, starty) && !got_start) {
					if (chunk->selected)
						redraw = TRUE;
					chunk->selected = FALSE;
					chunk->sel_start = chunk->text;
					chunk->sel_end = NULL;
				}

				break;
			case 1:
				if (!got_start && COORDS_IN_CHUNK (startx, starty)) {
					new_pos = get_position (chunk, startx, smileys);
					if ( !chunk->selected ||
					    (chunk->sel_end != new_pos) ||
					    (chunk->sel_start != chunk->text))
						redraw = TRUE;
					chunk->selected = TRUE;
					chunk->sel_start = chunk->text;
					chunk->sel_end = new_pos;
					selection++;
					got_start = TRUE;
				} else if (!got_end && COORDS_IN_CHUNK (endx, endy)) {
					new_pos = get_position (chunk, endx, smileys);
					if ( !chunk->selected ||
					    (chunk->sel_end != new_pos) ||
					    (chunk->sel_start != chunk->text))
						redraw = TRUE;
					chunk->selected = TRUE;
					chunk->sel_start = chunk->text;
					chunk->sel_end = new_pos;
					selection++;
					imhtml->sel_endchunk = chunk;
					got_end = TRUE;
				} else {
					if ( !chunk->selected ||
					    (chunk->sel_end != NULL) ||
					    (chunk->sel_start != chunk->text))
						redraw = TRUE;
					chunk->selected = TRUE;
					chunk->sel_start = chunk->text;
					chunk->sel_end = NULL;
				}

				break;
			case 2:
				if (chunk->selected)
					redraw = TRUE;
				chunk->selected = FALSE;
				chunk->sel_start = chunk->text;
				chunk->sel_end = NULL;
				break;
			}

			if (chunk->selected == TRUE)
				imhtml->selected_text = append_to_sel (imhtml->selected_text,
								       chunk, smileys);

			if (redraw) {
				if (DRAW_IMG (bit))
					draw_img (imhtml, chunk);
				else if ((bit->type == TYPE_SEP) && (bit->chunks->data == chunk))
					draw_line (imhtml, chunk);
				else if (chunk->width)
					draw_text (imhtml, chunk);
				redraw = FALSE;
			}

			chunks = g_list_next (chunks);
		}

		bits = g_list_next (bits);
	}
}

static void
gtk_imhtml_select_in_chunk (GtkIMHtml *imhtml,
			    struct line_info *chunk)
{
	GtkIMHtmlBit *bit = chunk->bit;
	gchar *new_pos;
	guint endx = imhtml->sel_endx;
	guint startx = imhtml->sel_startx;
	guint starty = imhtml->sel_starty;
	gboolean smileys = imhtml->smileys;
	gboolean redraw = FALSE;

	new_pos = get_position (chunk, endx, smileys);
	if ((starty < chunk->y) ||
	    ((starty < chunk->y + chunk->height) && (startx < endx))) {
		if (chunk->sel_end != new_pos)
			redraw = TRUE;
		chunk->sel_end = new_pos;
	} else {
		if (chunk->sel_start != new_pos)
			redraw = TRUE;
		chunk->sel_start = new_pos;
	}

	if (redraw) {
		if (DRAW_IMG (bit))
			draw_img (imhtml, chunk);
		else if ((bit->type == TYPE_SEP) && 
			 (bit->chunks->data == chunk))
			draw_line (imhtml, chunk);
		else if (chunk->width)
			draw_text (imhtml, chunk);
	}
}

static gint
scroll_timeout (GtkIMHtml *imhtml)
{
	GdkEventMotion event;
	gint x, y;
	GdkModifierType mask;

	imhtml->scroll_timer = 0;

	gdk_window_get_pointer (GTK_LAYOUT (imhtml)->bin_window, &x, &y, &mask);

	if (mask & GDK_BUTTON1_MASK) {
		event.is_hint = 0;
		event.x = x;
		event.y = y;
		event.state = mask;

		gtk_imhtml_motion_notify_event (GTK_WIDGET (imhtml), &event);
	}

	return FALSE;
}

static gint
gtk_imhtml_tip_paint (GtkIMHtml *imhtml)
{
	GtkStyle *style;
	GdkFont *font;
	gint y, baseline_skip, gap;

	style = imhtml->tip_window->style;
	font = GTK_IMHTML_GET_STYLE_FONT (style);

	gap = (font->ascent + font->descent) / 4;
	if (gap < 2)
		gap = 2;
	baseline_skip = font->ascent + font->descent + gap;

	if (!imhtml->tip_bit)
		return FALSE;

	gtk_paint_flat_box (style, imhtml->tip_window->window, GTK_STATE_NORMAL, GTK_SHADOW_OUT,
			   NULL, imhtml->tip_window, "tooltip", 0, 0, -1, -1);

	y = font->ascent + 4;
	gtk_paint_string (style, imhtml->tip_window->window, GTK_STATE_NORMAL, NULL,
			  imhtml->tip_window, "tooltip", 4, y, imhtml->tip_bit->url);

	return FALSE;
}

static gint
gtk_imhtml_tip (gpointer data)
{
	GtkIMHtml *imhtml = data;
	GtkWidget *widget = GTK_WIDGET (imhtml);
	GtkStyle *style;
	GdkFont *font;
	gint gap, x, y, w, h, scr_w, scr_h, baseline_skip;

	if (!imhtml->tip_bit || !GTK_WIDGET_DRAWABLE (widget)) {
		imhtml->tip_timer = 0;
		return FALSE;
	}

	if (imhtml->tip_window)
		gtk_widget_destroy (imhtml->tip_window);

	imhtml->tip_window = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_widget_set_app_paintable (imhtml->tip_window, TRUE);
	gtk_window_set_policy (GTK_WINDOW (imhtml->tip_window), FALSE, FALSE, TRUE);
	gtk_widget_set_name (imhtml->tip_window, "gtk-tooltips");
	gtk_signal_connect_object (GTK_OBJECT (imhtml->tip_window), "expose_event",
				   GTK_SIGNAL_FUNC (gtk_imhtml_tip_paint), GTK_OBJECT (imhtml));
	gtk_signal_connect_object (GTK_OBJECT (imhtml->tip_window), "draw",
				   GTK_SIGNAL_FUNC (gtk_imhtml_tip_paint), GTK_OBJECT (imhtml));

	gtk_widget_ensure_style (imhtml->tip_window);
	style = imhtml->tip_window->style;
	font = GTK_IMHTML_GET_STYLE_FONT (style);

	scr_w = gdk_screen_width ();
	scr_h = gdk_screen_height ();

	gap = (font->ascent + font->descent) / 4;
	if (gap < 2)
		gap = 2;
	baseline_skip = font->ascent + font->descent + gap;

	w = 8 + gdk_string_width (font, imhtml->tip_bit->url);
	h = 8 - gap + baseline_skip;

	gdk_window_get_pointer (NULL, &x, &y, NULL);
	if (GTK_WIDGET_NO_WINDOW (widget))
		y += widget->allocation.y;

	x -= ((w >> 1) + 4);

	if ((x + w) > scr_w)
		x -= (x + w) - scr_w;
	else if (x < 0)
		x = 0;

	if ((y + h + + 4) > scr_h)
		y = y - imhtml->tip_bit->font->ascent + imhtml->tip_bit->font->descent;
	else 
		if (imhtml->tip_bit->font)
			y = y + imhtml->tip_bit->font->ascent + imhtml->tip_bit->font->descent;
		else
			y = y + font->ascent + font->descent;

	gtk_widget_set_usize (imhtml->tip_window, w, h);
	gtk_widget_set_uposition (imhtml->tip_window, x, y);
	gtk_widget_show (imhtml->tip_window);

	imhtml->tip_timer = 0;
	return FALSE;
}

static gint
gtk_imhtml_motion_notify_event (GtkWidget      *widget,
				GdkEventMotion *event)
{
	gint x, y;
	GdkModifierType state;
	GtkIMHtml *imhtml = GTK_IMHTML (widget);
	GtkAdjustment *vadj = GTK_LAYOUT (widget)->vadjustment;
	GtkAdjustment *hadj = GTK_LAYOUT (widget)->hadjustment;

	if (event->is_hint)
		gdk_window_get_pointer (event->window, &x, &y, &state);
	else {
		x = event->x + hadj->value;
		y = event->y + vadj->value;
		state = event->state;
	}

	if (state & GDK_BUTTON1_MASK) {
		gint diff;
		gint height = vadj->page_size;
		gint yy = y - vadj->value;

		if (((yy < 0) || (yy > height)) &&
		    (imhtml->scroll_timer == 0) &&
		    (vadj->upper > vadj->page_size)) {
			imhtml->scroll_timer = gtk_timeout_add (100,
								(GtkFunction) scroll_timeout,
								imhtml);
			diff = (yy < 0) ? (yy / 2) : ((yy - height) / 2);
			gtk_adjustment_set_value (vadj,
						  MIN (vadj->value + diff, vadj->upper - height));
		}

		if (imhtml->selection) {
			struct line_info *chunk = imhtml->sel_endchunk;
			imhtml->sel_endx = MAX (x, 0);
			imhtml->sel_endy = MAX (y, 0);
			if ((chunk == NULL) ||
			    (x < chunk->x) ||
			    (x > chunk->x + chunk->width) ||
			    (y < chunk->y) ||
			    (y > chunk->y + chunk->height))
				gtk_imhtml_select_bits (imhtml);
			else
				gtk_imhtml_select_in_chunk (imhtml, chunk);
		}
	} else {
		GList *click = imhtml->click;
		struct clickable *uw;

		while (click) {
			uw = (struct clickable *) click->data;
			if ((x > uw->x) && (x < uw->x + uw->width) &&
			    (y > uw->y) && (y < uw->y + uw->height)) {
				if (imhtml->tip_bit != uw->bit) {
					imhtml->tip_bit = uw->bit;
					if (imhtml->tip_timer != 0)
						gtk_timeout_remove (imhtml->tip_timer);
					if (imhtml->tip_window) {
						gtk_widget_destroy (imhtml->tip_window);
						imhtml->tip_window = NULL;
					}
					imhtml->tip_timer = gtk_timeout_add (TOOLTIP_TIMEOUT,
									     gtk_imhtml_tip,
									     imhtml);
				}
				gdk_window_set_cursor (GTK_LAYOUT (imhtml)->bin_window,
						       imhtml->hand_cursor);
				return TRUE;
			}
			click = g_list_next (click);
		}
	}

	if (imhtml->tip_timer) {
		gtk_timeout_remove (imhtml->tip_timer);
		imhtml->tip_timer = 0;
	}
	if (imhtml->tip_window) {
		gtk_widget_destroy (imhtml->tip_window);
		imhtml->tip_window = NULL;
	}
	imhtml->tip_bit = NULL;

	gdk_window_set_cursor (GTK_LAYOUT (imhtml)->bin_window, imhtml->arrow_cursor);

	return TRUE;
}

static gint
gtk_imhtml_leave_notify_event (GtkWidget        *widget,
			       GdkEventCrossing *event)
{
	GtkIMHtml *imhtml = GTK_IMHTML (widget);

	if (imhtml->tip_timer) {
		gtk_timeout_remove (imhtml->tip_timer);
		imhtml->tip_timer = 0;
	}
	if (imhtml->tip_window) {
		gtk_widget_destroy (imhtml->tip_window);
		imhtml->tip_window = NULL;
	}
	imhtml->tip_bit = NULL;

	return TRUE;
}

static void
menu_open_url (GtkObject *object,
	       gpointer   data)
{
	struct clickable *uw = data;

	gtk_signal_emit (GTK_OBJECT (uw->imhtml), signals [URL_CLICKED], uw->bit->url);
}

static void
menu_copy_link (GtkObject *object,
		gpointer   data)
{
	struct clickable *uw = data;
	GtkIMHtml *imhtml = uw->imhtml;

	if (imhtml->selected_text)
		g_string_free (imhtml->selected_text, TRUE);

	gtk_imhtml_select_none (uw->imhtml);

	imhtml->selection = TRUE;
	imhtml->selected_text = g_string_new (uw->bit->url);

	gtk_selection_owner_set (GTK_WIDGET (imhtml), GDK_SELECTION_PRIMARY, GDK_CURRENT_TIME);
}

static gint
gtk_imhtml_button_press_event (GtkWidget      *widget,
			       GdkEventButton *event)
{
	GtkIMHtml *imhtml = GTK_IMHTML (widget);
	GtkAdjustment *vadj = GTK_LAYOUT (widget)->vadjustment;
	GtkAdjustment *hadj = GTK_LAYOUT (widget)->hadjustment;
	gint x, y;

	x = event->x + hadj->value;
	y = event->y + vadj->value;

	if (event->button == 1) {
		imhtml->sel_startx = x;
		imhtml->sel_starty = y;
		imhtml->selection = TRUE;
		gtk_imhtml_select_none (imhtml);
	}

	if (event->button == 3) {
		GList *click = imhtml->click;
		struct clickable *uw;

		while (click) {
			uw = click->data;
			if ((x > uw->x) && (x < uw->x + uw->width) &&
			    (y > uw->y) && (y < uw->y + uw->height)) {
				GtkWidget *menu = gtk_menu_new ();
				GtkWidget *button;

				if (uw->bit->url) {
					button = gtk_menu_item_new_with_label ("Open URL");
					gtk_signal_connect (GTK_OBJECT (button), "activate",
							    GTK_SIGNAL_FUNC (menu_open_url), uw);
					gtk_menu_append (GTK_MENU (menu), button);
					gtk_widget_show (button);

					button = gtk_menu_item_new_with_label ("Copy Link Location");
					gtk_signal_connect (GTK_OBJECT (button), "activate",
							    GTK_SIGNAL_FUNC (menu_copy_link), uw);
					gtk_menu_append (GTK_MENU (menu), button);
					gtk_widget_show (button);
				}

				gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->time);

				if (imhtml->tip_timer) {
					gtk_timeout_remove (imhtml->tip_timer);
					imhtml->tip_timer = 0;
				}
				if (imhtml->tip_window) {
					gtk_widget_destroy (imhtml->tip_window);
					imhtml->tip_window = NULL;
				}
				imhtml->tip_bit = NULL;

				return TRUE;
			}
			click = g_list_next (click);
		}
	}

	return TRUE;
}

static gint
gtk_imhtml_button_release_event (GtkWidget      *widget,
				 GdkEventButton *event)
{
	GtkIMHtml *imhtml = GTK_IMHTML (widget);
	GtkAdjustment *vadj = GTK_LAYOUT (widget)->vadjustment;
	GtkAdjustment *hadj = GTK_LAYOUT (widget)->hadjustment;
	gint x, y;

	x = event->x + hadj->value;
	y = event->y + vadj->value;

	if ((event->button == 1) && imhtml->selection) {
		if ((x == imhtml->sel_startx) && (y == imhtml->sel_starty)) {
			imhtml->sel_startx = imhtml->sel_starty = 0;
			imhtml->selection = FALSE;
			gtk_imhtml_select_none (imhtml);
		} else {
			imhtml->sel_endx = MAX (x, 0);
			imhtml->sel_endy = MAX (y, 0);
			gtk_imhtml_select_bits (imhtml);
		}

		gtk_selection_owner_set (widget, GDK_SELECTION_PRIMARY, event->time);
	}

	if ((event->button == 1) && (imhtml->sel_startx == 0)) {
		GList *click = imhtml->click;
		struct clickable *uw;

		while (click) {
			uw = (struct clickable *) click->data;
			if ((x > uw->x) && (x < uw->x + uw->width) &&
			    (y > uw->y) && (y < uw->y + uw->height)) {
				gtk_signal_emit (GTK_OBJECT (imhtml), signals [URL_CLICKED],
						 uw->bit->url);
				return TRUE;
			}
			click = g_list_next (click);
		}
	}

	return TRUE;
}

static void
gtk_imhtml_selection_get (GtkWidget        *widget,
			  GtkSelectionData *sel_data,
			  guint             sel_info,
			  guint32           time)
{
	GtkIMHtml *imhtml;
	gchar *string;
	gint length;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_IMHTML (widget));
	g_return_if_fail (sel_data->selection == GDK_SELECTION_PRIMARY);

	imhtml = GTK_IMHTML (widget);

	g_return_if_fail (imhtml->selected_text != NULL);
	g_return_if_fail (imhtml->selected_text->str != NULL);

	if (imhtml->selected_text->len <= 0)
		return;

	string = g_strdup (imhtml->selected_text->str);
	length = strlen (string);

	if (sel_info == TARGET_STRING) {
		gtk_selection_data_set (sel_data,
					GDK_SELECTION_TYPE_STRING,
					8 * sizeof (gchar),
					(guchar *) string,
					length);
	} else if ((sel_info == TARGET_TEXT) || (sel_info == TARGET_COMPOUND_TEXT)) {
		guchar *text;
		GdkAtom encoding;
		gint format;
		gint new_length;

		gdk_string_to_compound_text (string, &encoding, &format, &text, &new_length);
		gtk_selection_data_set (sel_data, encoding, format, text, new_length);
		gdk_free_compound_text (text);
	}

	g_free (string);
}

static gint
gtk_imhtml_selection_clear_event (GtkWidget         *widget,
				  GdkEventSelection *event)
{
	GtkIMHtml *imhtml;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_IMHTML (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	g_return_val_if_fail (event->selection == GDK_SELECTION_PRIMARY, TRUE);

	if (!gtk_selection_clear (widget, event))
		return FALSE;

	imhtml = GTK_IMHTML (widget);

	gtk_imhtml_select_none (imhtml);

	return TRUE;
}

static void
gtk_imhtml_adjustment_changed (GtkAdjustment *adjustment,
			       GtkIMHtml     *imhtml)
{
	GtkLayout *layout = GTK_LAYOUT (imhtml);

	if (!GTK_WIDGET_MAPPED (imhtml) || !GTK_WIDGET_REALIZED (imhtml))
		return;

	if (layout->freeze_count)
		return;

	if (layout->vadjustment->value < TOP_BORDER)
		gdk_window_clear_area (layout->bin_window, 0, 0,
				       imhtml->xsize, TOP_BORDER - layout->vadjustment->value);

	gtk_imhtml_draw_exposed (imhtml);
}

static void
gtk_imhtml_set_scroll_adjustments (GtkLayout     *layout,
				   GtkAdjustment *hadj,
				   GtkAdjustment *vadj)
{
	gboolean need_adjust = FALSE;

	g_return_if_fail (layout != NULL);
	g_return_if_fail (GTK_IS_IMHTML (layout));

	if (hadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (hadj));
	else
		hadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
	if (vadj)
		g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
	else
		vadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));

	if (layout->hadjustment && (layout->hadjustment != hadj)) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (layout->hadjustment), layout);
		gtk_object_unref (GTK_OBJECT (layout->hadjustment));
	}

	if (layout->vadjustment && (layout->vadjustment != vadj)) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (layout->vadjustment), layout);
		gtk_object_unref (GTK_OBJECT (layout->vadjustment));
	}

	if (layout->hadjustment != hadj) {
		layout->hadjustment = hadj;
		gtk_object_ref (GTK_OBJECT (layout->hadjustment));
		gtk_object_sink (GTK_OBJECT (layout->hadjustment));

		gtk_signal_connect (GTK_OBJECT (layout->hadjustment), "value_changed",
				    (GtkSignalFunc) gtk_imhtml_adjustment_changed, layout);
		need_adjust = TRUE;
	}
	
	if (layout->vadjustment != vadj) {
		layout->vadjustment = vadj;
		gtk_object_ref (GTK_OBJECT (layout->vadjustment));
		gtk_object_sink (GTK_OBJECT (layout->vadjustment));

		gtk_signal_connect (GTK_OBJECT (layout->vadjustment), "value_changed",
				    (GtkSignalFunc) gtk_imhtml_adjustment_changed, layout);
		need_adjust = TRUE;
	}

	if (need_adjust)
		gtk_imhtml_adjustment_changed (NULL, GTK_IMHTML (layout));
}

static void
gtk_imhtml_class_init (GtkIMHtmlClass *class)
{
#if GTK_CHECK_VERSION(1,3,0)
	GObjectClass  *gobject_class;
#endif
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkLayoutClass *layout_class;

#if GTK_CHECK_VERSION(1,3,0)
	gobject_class = (GObjectClass*) class;
#endif
	object_class = (GtkObjectClass*) class;
	widget_class = (GtkWidgetClass*) class;
	layout_class = (GtkLayoutClass*) class;

	parent_class = gtk_type_class (GTK_TYPE_LAYOUT);

	signals [URL_CLICKED] =
		gtk_signal_new ("url_clicked",
				GTK_RUN_FIRST,
				GTK_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (GtkIMHtmlClass, url_clicked),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_POINTER);

#if GTK_CHECK_VERSION(1,3,0)
	gobject_class->finalize = gtk_imhtml_finalize;
#else
	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);

	object_class->destroy = gtk_imhtml_destroy;
#endif

	widget_class->realize = gtk_imhtml_realize;
#if !GTK_CHECK_VERSION(1,3,0)
	widget_class->draw = gtk_imhtml_draw;
	widget_class->draw_focus = gtk_imhtml_draw_focus;
#endif
	widget_class->style_set = gtk_imhtml_style_set;
	widget_class->expose_event  = gtk_imhtml_expose_event;
	widget_class->size_allocate = gtk_imhtml_size_allocate;
	widget_class->motion_notify_event = gtk_imhtml_motion_notify_event;
	widget_class->leave_notify_event = gtk_imhtml_leave_notify_event;
	widget_class->button_press_event = gtk_imhtml_button_press_event;
	widget_class->button_release_event = gtk_imhtml_button_release_event;
	widget_class->selection_get = gtk_imhtml_selection_get;
	widget_class->selection_clear_event = gtk_imhtml_selection_clear_event;

	layout_class->set_scroll_adjustments = gtk_imhtml_set_scroll_adjustments;
}

static gchar*
gtk_imhtml_get_font_name (GdkFont *font)
{
#if GTK_CHECK_VERSION(1,3,0)
	return "--*-helvetica-medium-r-normal--10-*-*-*-*-*-*-*";
#else
	GdkFontPrivate *fontpriv = (GdkFontPrivate *) font;
	return fontpriv->names->data;
#endif
}

#define TRY_FONT	tmp = g_strjoinv ("-", newvals); \
			if (default_font->type == GDK_FONT_FONT) \
				ret_font = gdk_font_load (tmp); \
			else \
				ret_font = gdk_fontset_load (tmp); \
			g_free (tmp); \
			if (ret_font) { \
				g_free (newvals); \
				g_strfreev (xnames); \
				g_strfreev (xflds); \
				g_strfreev (names); \
				return ret_font; \
			}


static GdkFont*
gtk_imhtml_font_load (GtkIMHtml *imhtml,
		      gchar     *name,
		      gboolean   bold,
		      gboolean   italics,
		      gint       fontsize)
{
	GdkFont *default_font = imhtml->default_font;
	gchar *default_name;
	gchar **xnames;
	gchar **pos;

	if (!name && !bold && !italics && !fontsize)
		return gdk_font_ref (default_font);

	default_name = gtk_imhtml_get_font_name (default_font);
	xnames = g_strsplit (default_name, ",", -1);

	for (pos = xnames; pos && *pos; pos++) {
		gchar *xname;
		gchar **xflds;

		gchar **newvals;
		gint i, j;
		gchar **names;
		gchar fs[10];

		gchar *tmp;
		GdkFont *ret_font;

		xname = *pos;
		xname = g_strchomp (xname);
		xname = g_strchug (xname);

		xflds = g_strsplit (xname, "-", -1);

#define FNDRY 1
#define FMLY 2
#define WGHT 3
#define SLANT 4
#define SWDTH 5
#define ADSTYL 6
#define PXLSZ 7
#define PTSZ 8
#define RESX 9
#define RESY 10
#define SPC 11
#define AVGWDTH 12
#define RGSTRY 13
#define ENCDNG 14

		for (i = 0; xflds [i]; i++);
		if (i != 15) {
			int tmp;
			newvals = g_malloc0 (16 * sizeof (gchar *));
			newvals [0] = "";
			for (tmp = 1; tmp < 15; tmp++)
				newvals [tmp] = "*";
		} else
			newvals = g_memdup (xflds, 16 * sizeof (xflds));

		newvals [FNDRY] = "*";

		if ((i > ADSTYL) && !xflds [ADSTYL][0])
			newvals [ADSTYL] = "*";

		if (bold)
			newvals [WGHT] = "bold";
		if (italics)
			newvals [SLANT] = "i";
		if (fontsize) {
			g_snprintf (fs, sizeof (fs), "%d", POINT_SIZE (fontsize));
			newvals [PXLSZ] = "*";
			newvals [PTSZ] = fs;
		}

		if (name)
			names = g_strsplit (name, ",", -1);
		else if (i > FMLY) {
			names = g_new0 (gchar *, 2);
			names [0] = g_strdup (xflds [FMLY]);
		} else {
			names = g_new0 (gchar *, 2);
			names [0] = g_strdup ("*");
		}

		for (j = 0; names [j]; j++) {
			newvals [FMLY] = names [j];
			TRY_FONT;
		}

		for (j = 0; italics && names [j]; j++) {
			newvals [FMLY] = names [j];

			newvals [SLANT] = "o";
			TRY_FONT;

			if (i > SLANT)
				newvals [SLANT] = xflds [SLANT];
			else
				newvals [SLANT] = "*";
			TRY_FONT;
		}

		for (j = 0; fontsize && names [j]; j++) {
			newvals [FMLY] = names [j];

			if (i > PTSZ) {
				newvals [PXLSZ] = xflds [PXLSZ];
				newvals [PTSZ] = xflds [PTSZ];
			} else {
				newvals [PXLSZ] = "*";
				newvals [PTSZ] = "*";
			}
			TRY_FONT;
		}

		for (j = 0; bold && names [j]; j++) {
			newvals [FMLY] = names [j];

			if (i > WGHT)
				newvals [WGHT] = xflds [WGHT];
			else
				newvals [WGHT] = "*";
			TRY_FONT;
		}

		g_free (newvals);
		g_strfreev (xflds);
		g_strfreev (names);

	}

	g_strfreev (xnames);

	return gdk_font_ref (default_font);
}

static void
gtk_imhtml_init (GtkIMHtml *imhtml)
{
	static const GtkTargetEntry targets [] = {
		{ "STRING", 0, TARGET_STRING },
		{ "TEXT", 0, TARGET_TEXT },
		{ "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT }
	};

	imhtml->hand_cursor = gdk_cursor_new (GDK_HAND2);
	imhtml->arrow_cursor = gdk_cursor_new (GDK_LEFT_PTR);

	GTK_WIDGET_SET_FLAGS (GTK_WIDGET (imhtml), GTK_CAN_FOCUS);
	gtk_selection_add_targets (GTK_WIDGET (imhtml), GDK_SELECTION_PRIMARY, targets, 3);
}

GtkType
gtk_imhtml_get_type (void)
{
	static GtkType imhtml_type = 0;

	if (!imhtml_type) {
		static const GtkTypeInfo imhtml_info = {
			"GtkIMHtml",
			sizeof (GtkIMHtml),
			sizeof (GtkIMHtmlClass),
			(GtkClassInitFunc) gtk_imhtml_class_init,
			(GtkObjectInitFunc) gtk_imhtml_init,
			NULL,
			NULL,
			NULL
		};

		imhtml_type = gtk_type_unique (GTK_TYPE_LAYOUT, &imhtml_info);
	}

	return imhtml_type;
}

static void
gtk_imhtml_init_smileys (GtkIMHtml *imhtml)
{
	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	imhtml->smiley_data = gtk_smiley_tree_new ();

	gtk_imhtml_associate_smiley (imhtml, ":)", smile_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-)", smile_xpm);

	gtk_imhtml_associate_smiley (imhtml, ":(", sad_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-(", sad_xpm);

	gtk_imhtml_associate_smiley (imhtml, ";)", wink_xpm);
	gtk_imhtml_associate_smiley (imhtml, ";-)", wink_xpm);

	gtk_imhtml_associate_smiley (imhtml, ":-p", tongue_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-P", tongue_xpm);

	gtk_imhtml_associate_smiley (imhtml, "=-O", scream_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-*", kiss_xpm);
	gtk_imhtml_associate_smiley (imhtml, ">:o", yell_xpm);
	gtk_imhtml_associate_smiley (imhtml, "8-)", smile8_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-$", moneymouth_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-!", burp_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-[", embarrassed_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":'(", cry_xpm);

	gtk_imhtml_associate_smiley (imhtml, ":-/", think_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-\\", think_xpm);

	gtk_imhtml_associate_smiley (imhtml, ":-X", crossedlips_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-D", bigsmile_xpm);
	gtk_imhtml_associate_smiley (imhtml, ":-d", bigsmile_xpm);
	gtk_imhtml_associate_smiley (imhtml, "O:-)", angel_xpm);
}

GtkWidget*
gtk_imhtml_new (GtkAdjustment *hadj,
		GtkAdjustment *vadj)
{
	GtkIMHtml *imhtml = gtk_type_new (GTK_TYPE_IMHTML);

	gtk_imhtml_set_adjustments (imhtml, hadj, vadj);

	imhtml->bits = NULL;
	imhtml->click = NULL;

	imhtml->x = 0;
	imhtml->y = TOP_BORDER;
	imhtml->llheight = 0;
	imhtml->llascent = 0;
	imhtml->line = NULL;

	imhtml->selected_text = g_string_new ("");
	imhtml->scroll_timer = 0;

	imhtml->img = NULL;

	imhtml->smileys = TRUE;
	imhtml->comments = FALSE;

	gtk_imhtml_init_smileys (imhtml);

	return GTK_WIDGET (imhtml);
}

void
gtk_imhtml_set_adjustments (GtkIMHtml     *imhtml,
			    GtkAdjustment *hadj,
			    GtkAdjustment *vadj)
{
	gtk_layout_set_hadjustment (GTK_LAYOUT (imhtml), hadj);
	gtk_layout_set_vadjustment (GTK_LAYOUT (imhtml), vadj);
}

void
gtk_imhtml_set_defaults (GtkIMHtml *imhtml,
			 GdkFont   *font,
			 GdkColor  *fg_color,
			 GdkColor  *bg_color)
{
	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	if (font) {
		if (imhtml->default_font)
			gdk_font_unref (imhtml->default_font);
		imhtml->default_font = gdk_font_ref (font);
	}

	if (fg_color) {
		if (imhtml->default_fg_color)
			gdk_color_free (imhtml->default_fg_color);
		imhtml->default_fg_color = gdk_color_copy (fg_color);
	}

	if (bg_color) {
		if (imhtml->default_bg_color)
			gdk_color_free (imhtml->default_bg_color);
		imhtml->default_bg_color = gdk_color_copy (bg_color);
		gdk_window_set_background (GTK_LAYOUT (imhtml)->bin_window, imhtml->default_bg_color);
	}
}

void
gtk_imhtml_set_img_handler (GtkIMHtml      *imhtml,
			    GtkIMHtmlImage  handler)
{
	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	imhtml->img = handler;
}

void
gtk_imhtml_associate_smiley (GtkIMHtml  *imhtml,
			     gchar      *text,
			     gchar     **xpm)
{
	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));
	g_return_if_fail (text != NULL);

	if (xpm == NULL)
		gtk_smiley_tree_remove (imhtml->smiley_data, text);
	else
		gtk_smiley_tree_insert (imhtml->smiley_data, text, xpm);
}

static void
new_line (GtkIMHtml *imhtml)
{
	GList *last = g_list_last (imhtml->line);
	struct line_info *li;

	if (last) {
		li = last->data;
		if (li->x + li->width != imhtml->xsize)
			li->width = imhtml->xsize - li->x;
	}

	last = imhtml->line;
	if (last) {
		li = last->data;
		if (li->height < MIN_HEIGHT) {
			while (last) {
				gint diff;
				li = last->data;
				diff = MIN_HEIGHT - li->height;
				li->height = MIN_HEIGHT;
				li->ascent += diff / 2;
				last = g_list_next (last);
			}
			imhtml->llheight = MIN_HEIGHT;
		}
	}

	g_list_free (imhtml->line);
	imhtml->line = NULL;

	imhtml->x = 0;
	imhtml->y += imhtml->llheight;
	imhtml->llheight = 0;
	imhtml->llascent = 0;
}

static void
backwards_update (GtkIMHtml    *imhtml,
		  GtkIMHtmlBit *bit,
		  gint          height,
		  gint          ascent)
{
	gint diff;
	GList *ls = NULL;
	struct line_info *li;
	struct clickable *uw;

	if (height > imhtml->llheight) {
		diff = height - imhtml->llheight;

		ls = imhtml->line;
		while (ls) {
			li = ls->data;
			li->height += diff;
			if (ascent)
				li->ascent = ascent;
			else
				li->ascent += diff / 2;
			ls = g_list_next (ls);
		}

		ls = imhtml->click;
		while (ls) {
			uw = ls->data;
			if (uw->y + diff > imhtml->y)
				uw->y += diff;
			ls = g_list_next (ls);
		}

		imhtml->llheight = height;
		if (ascent)
			imhtml->llascent = ascent;
		else
			imhtml->llascent += diff / 2;
	}
}

static void
add_text_renderer (GtkIMHtml    *imhtml,
		   GtkIMHtmlBit *bit,
		   gchar        *text)
{
	struct line_info *li;
	struct clickable *uw;
	gint width;

	if (text)
		width = gdk_string_width (bit->font, text);
	else
		width = 0;

	li = g_new0 (struct line_info, 1);
	li->x = imhtml->x;
	li->y = imhtml->y;
	li->width = width;
	li->height = imhtml->llheight;
	if (text)
		li->ascent = MAX (imhtml->llascent, bit->font->ascent);
	else
		li->ascent = 0;
	li->text = text;
	li->bit = bit;

	if (bit->url) {
		uw = g_new0 (struct clickable, 1);
		uw->x = imhtml->x;
		uw->y = imhtml->y;
		uw->width = width;
		uw->height = imhtml->llheight;
		uw->imhtml = imhtml;
		uw->bit = bit;
		imhtml->click = g_list_append (imhtml->click, uw);
	}

	bit->chunks = g_list_append (bit->chunks, li);
	imhtml->line = g_list_append (imhtml->line, li);
}

static void
add_img_renderer (GtkIMHtml    *imhtml,
		  GtkIMHtmlBit *bit)
{
	struct line_info *li;
	struct clickable *uw;
	gint width;

	gdk_window_get_size (bit->pm, &width, NULL);

	li = g_new0 (struct line_info, 1);
	li->x = imhtml->x;
	li->y = imhtml->y;
	li->width = width;
	li->height = imhtml->llheight;
	li->ascent = 0;
	li->bit = bit;

	if (bit->url) {
		uw = g_new0 (struct clickable, 1);
		uw->x = imhtml->x;
		uw->y = imhtml->y;
		uw->width = width;
		uw->height = imhtml->llheight;
		uw->imhtml = imhtml;
		uw->bit = bit;
		imhtml->click = g_list_append (imhtml->click, uw);
	}

	bit->chunks = g_list_append (bit->chunks, li);
	imhtml->line = g_list_append (imhtml->line, li);

	imhtml->x += width;
}

static void
gtk_imhtml_draw_bit (GtkIMHtml    *imhtml,
		     GtkIMHtmlBit *bit)
{
	gint width, height;

	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));
	g_return_if_fail (bit != NULL);

	if ( (bit->type == TYPE_TEXT) ||
	    ((bit->type == TYPE_SMILEY) && !imhtml->smileys) ||
	    ((bit->type == TYPE_COMMENT) && imhtml->comments)) {
		gchar *copy = g_strdup (bit->text);
		gint pos = 0;
		gboolean seenspace = FALSE;
		gchar *tmp;

		height = bit->font->ascent + bit->font->descent;
		width = gdk_string_width (bit->font, bit->text);

		if ((imhtml->x != 0) && ((imhtml->x + width) > imhtml->xsize)) {
			gint remain = imhtml->xsize - imhtml->x;
			while (gdk_text_width (bit->font, copy, pos) < remain) {
				if (copy [pos] == ' ')
					seenspace = TRUE;
				pos++;
			}
			if (seenspace) {
				while (copy [pos - 1] != ' ') pos--;

				tmp = g_strndup (copy, pos);

				backwards_update (imhtml, bit, height, bit->font->ascent);
				add_text_renderer (imhtml, bit, tmp);
			} else
				pos = 0;
			seenspace = FALSE;
			new_line (imhtml);
		}

		backwards_update (imhtml, bit, height, bit->font->ascent);

		while (pos < strlen (bit->text)) {
			width = gdk_string_width (bit->font, copy + pos);
			if (imhtml->x + width > imhtml->xsize) {
				gint newpos = 0;
				gint remain = imhtml->xsize - imhtml->x;
				while (gdk_text_width (bit->font, copy + pos, newpos) < remain) {
					if (copy [pos + newpos] == ' ')
						seenspace = TRUE;
					newpos++;
				}

				if (seenspace)
					while (copy [pos + newpos - 1] != ' ') newpos--;

				if (newpos == 0)
					break;

				tmp = g_strndup (copy + pos, newpos);
				pos += newpos;

				backwards_update (imhtml, bit, height, bit->font->ascent);
				add_text_renderer (imhtml, bit, tmp);

				seenspace = FALSE;
				new_line (imhtml);
			} else {
				tmp = g_strdup (copy + pos);

				backwards_update (imhtml, bit, height, bit->font->ascent);
				add_text_renderer (imhtml, bit, tmp);

				pos = strlen (bit->text);

				imhtml->x += width;
			}
		}

		g_free (copy);
	} else if ((bit->type == TYPE_SMILEY) || (bit->type == TYPE_IMG)) {
		gdk_window_get_size (bit->pm, &width, &height);

		if ((imhtml->x != 0) && ((imhtml->x + width) > imhtml->xsize))
			new_line (imhtml);
		else
			backwards_update (imhtml, bit, height, 0);

		add_img_renderer (imhtml, bit);
	} else if (bit->type == TYPE_BR) {
		new_line (imhtml);
		add_text_renderer (imhtml, bit, NULL);
	} else if (bit->type == TYPE_SEP) {
		struct line_info *li;
		if (imhtml->llheight)
			new_line (imhtml);

		li = g_new0 (struct line_info, 1);
		li->x = imhtml->x;
		li->y = imhtml->y;
		li->width = imhtml->xsize;
		li->height = HR_HEIGHT * 2;
		li->ascent = 0;
		li->text = NULL;
		li->bit = bit;

		bit->chunks = g_list_append (bit->chunks, li);

		imhtml->llheight = HR_HEIGHT * 2;
		new_line (imhtml);
		add_text_renderer (imhtml, bit, NULL);
	}
}

void
gtk_imhtml_show_smileys (GtkIMHtml *imhtml,
			 gboolean   show)
{
	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	imhtml->smileys = show;

	if (GTK_WIDGET_VISIBLE (GTK_WIDGET (imhtml)))
		gtk_imhtml_redraw_all (imhtml);
}

void
gtk_imhtml_show_comments (GtkIMHtml *imhtml,
			  gboolean   show)
{
	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	imhtml->comments = show;

	if (GTK_WIDGET_VISIBLE (GTK_WIDGET (imhtml)))
		gtk_imhtml_redraw_all (imhtml);
}

static GdkColor *
gtk_imhtml_get_color (const gchar *color)
{
	GdkColor c;

	if (!gdk_color_parse (color, &c))
		return NULL;

	return gdk_color_copy (&c);
}

static gboolean
gtk_imhtml_is_smiley (GtkIMHtml   *imhtml,
		      const gchar *text,
		      gint        *len)
{
	*len = gtk_smiley_tree_lookup (imhtml->smiley_data, text);
	return (*len > 0);
}

static GtkIMHtmlBit *
gtk_imhtml_new_bit (GtkIMHtml  *imhtml,
		    gint        type,
		    gchar      *text,
		    gint        bold,
		    gint        italics,
		    gint        underline,
		    gint        strike,
		    FontDetail *font,
		    GdkColor   *bg,
		    gchar      *url,
		    gint        pre,
		    gint        sub,
		    gint        sup)
{
	GtkIMHtmlBit *bit = NULL;

	g_return_val_if_fail (imhtml != NULL, NULL);
	g_return_val_if_fail (GTK_IS_IMHTML (imhtml), NULL);

	if ((type == TYPE_TEXT) && ((text == NULL) || (strlen (text) == 0)))
		return NULL;

	bit = g_new0 (GtkIMHtmlBit, 1);
	bit->type = type;

	if ((text != NULL) && (strlen (text) != 0))
		bit->text = g_strdup (text);

	if ((font != NULL) || bold || italics || pre) {
		if (font && (bold || italics || font->size || font->face || pre)) {
			if (pre) {
				bit->font = gtk_imhtml_font_load (imhtml, DEFAULT_PRE_FACE, bold, italics, font->size);
			} else {
				bit->font = gtk_imhtml_font_load (imhtml, font->face, bold, italics, font->size);
			}
		} else if (bold || italics || pre) {
			if (pre) {
				bit->font = gtk_imhtml_font_load (imhtml, DEFAULT_PRE_FACE, bold, italics, 0);
			} else {
				bit->font = gtk_imhtml_font_load (imhtml, NULL, bold, italics, 0);
			}
		}

		if (font && (type != TYPE_BR)) {
			if (font->fore != NULL)
				bit->fore = gdk_color_copy (font->fore);

			if (font->back != NULL)
				bit->back = gdk_color_copy (font->back);
		}
	}

	if (((bit->type == TYPE_TEXT) || (bit->type == TYPE_SMILEY) || (bit->type == TYPE_COMMENT)) &&
	    (bit->font == NULL))
		bit->font = gdk_font_ref (imhtml->default_font);

	if (bg != NULL)
		bit->bg = gdk_color_copy (bg);

	bit->underline = underline;
	bit->strike = strike;

	if (url != NULL)
		bit->url = g_strdup (url);

	if (type == TYPE_SMILEY) {
		GdkColor *clr;

		if ((font != NULL) && (font->back != NULL))
			clr = font->back;
		else
			clr = (bg != NULL) ? bg : imhtml->default_bg_color;

		bit->pm = gdk_pixmap_create_from_xpm_d (GTK_WIDGET (imhtml)->window,
							&bit->bm,
							clr,
							gtk_smiley_tree_image (imhtml->smiley_data, text));
	}

	return bit;
}

#define NEW_TEXT_BIT    gtk_imhtml_new_bit (imhtml, TYPE_TEXT, ws, bold, italics, underline, strike, \
				fonts ? fonts->data : NULL, bg, url, pre, sub, sup)
#define NEW_SMILEY_BIT  gtk_imhtml_new_bit (imhtml, TYPE_SMILEY, ws, bold, italics, underline, strike, \
				fonts ? fonts->data : NULL, bg, url, pre, sub, sup)
#define NEW_SEP_BIT     gtk_imhtml_new_bit (imhtml, TYPE_SEP, NULL, 0, 0, 0, 0, NULL, bg, NULL, 0, 0, 0)
#define NEW_BR_BIT      gtk_imhtml_new_bit (imhtml, TYPE_BR, NULL, 0, 0, 0, 0, \
				fonts ? fonts->data : NULL, bg, NULL, 0, 0, 0)
#define NEW_COMMENT_BIT gtk_imhtml_new_bit (imhtml, TYPE_COMMENT, ws, bold, italics, underline, strike, \
				fonts ? fonts->data : NULL, bg, url, pre, sub, sup)

#define NEW_BIT(bit)	ws [wpos] = '\0';				\
			{ GtkIMHtmlBit *tmp = bit; if (tmp != NULL)	\
			  newbits = g_list_append (newbits, tmp); }	\
			wpos = 0; ws [wpos] = '\0'

#define UPDATE_BG_COLORS \
	{ \
		GdkColormap *cmap; \
		GList *rev; \
		cmap = gtk_widget_get_colormap (GTK_WIDGET (imhtml)); \
		rev = g_list_last (newbits); \
		while (rev) { \
			GtkIMHtmlBit *bit = rev->data; \
			if (bit->bg) \
				gdk_color_free (bit->bg); \
			bit->bg = gdk_color_copy (bg); \
			if (bit->type == TYPE_BR) \
				break; \
			rev = g_list_previous (rev); \
		} \
		if (!rev) { \
			rev = g_list_last (imhtml->bits); \
			while (rev) { \
				GtkIMHtmlBit *bit = rev->data; \
				if (bit->bg) \
					gdk_color_free (bit->bg); \
				bit->bg = gdk_color_copy (bg); \
				gdk_color_alloc (cmap, bit->bg); \
				if (bit->type == TYPE_BR) \
					break; \
				rev = g_list_previous (rev); \
			} \
		} \
	}

static gboolean
gtk_imhtml_is_amp_escape (const gchar *string,
			  gchar       *replace,
			  gint        *length)
{
	g_return_val_if_fail (string != NULL, FALSE);
	g_return_val_if_fail (replace != NULL, FALSE);
	g_return_val_if_fail (length != NULL, FALSE);

	if (!g_strncasecmp (string, "&amp;", 5)) {
		*replace = '&';
		*length = 5;
	} else if (!g_strncasecmp (string, "&lt;", 4)) {
		*replace = '<';
		*length = 4;
	} else if (!g_strncasecmp (string, "&gt;", 4)) {
		*replace = '>';
		*length = 4;
	} else if (!g_strncasecmp (string, "&nbsp;", 6)) {
		*replace = ' ';
		*length = 6;
	} else if (!g_strncasecmp (string, "&copy;", 6)) {
		*replace = '�';
		*length = 6;
	} else if (!g_strncasecmp (string, "&quot;", 6)) {
		*replace = '\"';
		*length = 6;
	} else if (!g_strncasecmp (string, "&reg;", 5)) {
		*replace = '�';
		*length = 5;
	} else if (*(string + 1) == '#') {
		guint pound = 0;
		if (sscanf (string, "&#%u;", &pound) == 1) {
			if (*(string + 3 + (gint)log10 (pound)) != ';')
				return FALSE;
			*replace = (gchar)pound;
			*length = 2;
			while (isdigit ((gint) string [*length])) (*length)++;
			if (string [*length] == ';') (*length)++;
		} else {
			return FALSE;
		}
	} else {
		return FALSE;
	}

	return TRUE;
}

#define VALID_TAG(x)	if (!g_strncasecmp (string, x ">", strlen (x ">"))) {	\
				*tag = g_strndup (string, strlen (x));		\
				*len = strlen (x) + 1;				\
				return TRUE;					\
			}							\
			(*type)++

#define VALID_OPT_TAG(x)	if (!g_strncasecmp (string, x " ", strlen (x " "))) {	\
					const gchar *c = string + strlen (x " ");	\
					gchar e = '"';					\
					gboolean quote = FALSE;				\
					while (*c) {					\
						if (*c == '"' || *c == '\'') {		\
							if (quote && (*c == e))		\
								quote = !quote;		\
							else if (!quote) {		\
								quote = !quote;		\
								e = *c;			\
							}				\
						} else if (!quote && (*c == '>'))	\
							break;				\
						c++;					\
					}						\
					if (*c) {					\
						*tag = g_strndup (string, c - string);	\
						*len = c - string + 1;			\
						return TRUE;				\
					}						\
				}							\
				(*type)++

static gboolean
gtk_imhtml_is_tag (const gchar *string,
		   gchar      **tag,
		   gint        *len,
		   gint        *type)
{
	*type = 1;

	if (!strchr (string, '>'))
		return FALSE;

	VALID_TAG ("B");
	VALID_TAG ("BOLD");
	VALID_TAG ("/B");
	VALID_TAG ("/BOLD");
	VALID_TAG ("I");
	VALID_TAG ("ITALIC");
	VALID_TAG ("/I");
	VALID_TAG ("/ITALIC");
	VALID_TAG ("U");
	VALID_TAG ("UNDERLINE");
	VALID_TAG ("/U");
	VALID_TAG ("/UNDERLINE");
	VALID_TAG ("S");
	VALID_TAG ("STRIKE");
	VALID_TAG ("/S");
	VALID_TAG ("/STRIKE");
	VALID_TAG ("SUB");
	VALID_TAG ("/SUB");
	VALID_TAG ("SUP");
	VALID_TAG ("/SUP");
	VALID_TAG ("PRE");
	VALID_TAG ("/PRE");
	VALID_TAG ("TITLE");
	VALID_TAG ("/TITLE");
	VALID_TAG ("BR");
	VALID_TAG ("HR");
	VALID_TAG ("/FONT");
	VALID_TAG ("/A");
	VALID_TAG ("P");
	VALID_TAG ("/P");
	VALID_TAG ("H3");
	VALID_TAG ("/H3");
	VALID_TAG ("HTML");
	VALID_TAG ("/HTML");
	VALID_TAG ("BODY");
	VALID_TAG ("/BODY");
	VALID_TAG ("FONT");
	VALID_TAG ("HEAD");
	VALID_TAG ("HEAD");

	VALID_OPT_TAG ("HR");
	VALID_OPT_TAG ("FONT");
	VALID_OPT_TAG ("BODY");
	VALID_OPT_TAG ("A");
	VALID_OPT_TAG ("IMG");
	VALID_OPT_TAG ("P");
	VALID_OPT_TAG ("H3");

	if (!g_strncasecmp(string, "!--", strlen ("!--"))) {
		gchar *e = strstr (string, "-->");
		if (e) {
			*len = e - string + strlen ("-->");
			*tag = g_strndup (string + strlen ("!--"), *len - strlen ("!---->"));
			return TRUE;
		}
	}

	return FALSE;
}

static gchar*
gtk_imhtml_get_html_opt (gchar       *tag,
			 const gchar *opt)
{
	gchar *t = tag;
	gchar *e, *a;

	while (g_strncasecmp (t, opt, strlen (opt))) {
		gboolean quote = FALSE;
		if (*t == '\0') break;
		while (*t && !((*t == ' ') && !quote)) {
			if (*t == '\"')
				quote = ! quote;
			t++;
		}
		while (*t && (*t == ' ')) t++;
	}

	if (!g_strncasecmp (t, opt, strlen (opt))) {
		t += strlen (opt);
	} else {
		return NULL;
	}

	if ((*t == '\"') || (*t == '\'')) {
		e = a = ++t;
		while (*e && (*e != *(t - 1))) e++;
		if (*e != '\0') {
			*e = '\0';
			return g_strdup (a);
		} else {
			return NULL;
		}
	} else {
		e = a = t;
		while (*e && !isspace ((gint) *e)) e++;
		*e = '\0';
		return g_strdup (a);
	}
}

GString*
gtk_imhtml_append_text (GtkIMHtml        *imhtml,
			const gchar      *text,
			gint              len,
			GtkIMHtmlOptions  options)
{
	const gchar *c;
	gboolean binary = TRUE;
	gchar *ws;
	gint pos = 0;
	gint wpos = 0;

	gchar *tag;
	gint tlen;
	gint type;

	gchar amp;

	int smilelen;

	GList *newbits = NULL;

	guint	bold = 0,
		italics = 0,
		underline = 0,
		strike = 0,
		sub = 0,
		sup = 0,
		title = 0,
		pre = 0;
	GSList *fonts = NULL;
	GdkColor *bg = NULL;
	gchar *url = NULL;

	GtkAdjustment *vadj;
	gboolean scrolldown = TRUE;

	GString *retval = NULL;

	g_return_val_if_fail (imhtml != NULL, NULL);
	g_return_val_if_fail (GTK_IS_IMHTML (imhtml), NULL);
	g_return_val_if_fail (text != NULL, NULL);

	if (options & GTK_IMHTML_RETURN_LOG)
		retval = g_string_new ("");

	vadj = GTK_LAYOUT (imhtml)->vadjustment;
	if ((vadj->value < imhtml->y - GTK_WIDGET (imhtml)->allocation.height) &&
	    (vadj->upper >= GTK_WIDGET (imhtml)->allocation.height))
		scrolldown = FALSE;

	c = text;
	if (len == -1) {
		binary = FALSE;
		len = strlen (text);
	}

	ws = g_malloc (len + 1);
	ws [0] = '\0';

	while (pos < len) {
		if (*c == '<' && gtk_imhtml_is_tag (c + 1, &tag, &tlen, &type)) {
			c++;
			pos++;
			switch (type) {
			case 1:		/* B */
			case 2:		/* BOLD */
				NEW_BIT (NEW_TEXT_BIT);
				bold++;
				break;
			case 3:		/* /B */
			case 4:		/* /BOLD */
				NEW_BIT (NEW_TEXT_BIT);
				if (bold)
					bold--;
				break;
			case 5:		/* I */
			case 6:		/* ITALIC */
				NEW_BIT (NEW_TEXT_BIT);
				italics++;
				break;
			case 7:		/* /I */
			case 8:		/* /ITALIC */
				NEW_BIT (NEW_TEXT_BIT);
				if (italics)
					italics--;
				break;
			case 9:		/* U */
			case 10:	/* UNDERLINE */
				NEW_BIT (NEW_TEXT_BIT);
				underline++;
				break;
			case 11:	/* /U */
			case 12:	/* /UNDERLINE */
				NEW_BIT (NEW_TEXT_BIT);
				if (underline)
					underline--;
				break;
			case 13:	/* S */
			case 14:	/* STRIKE */
				NEW_BIT (NEW_TEXT_BIT);
				strike++;
				break;
			case 15:	/* /S */
			case 16:	/* /STRIKE */
				NEW_BIT (NEW_TEXT_BIT);
				if (strike)
					strike--;
				break;
			case 17:	/* SUB */
				NEW_BIT (NEW_TEXT_BIT);
				sub++;
				break;
			case 18:	/* /SUB */
				NEW_BIT (NEW_TEXT_BIT);
				if (sub)
					sub--;
				break;
			case 19:	/* SUP */
				NEW_BIT (NEW_TEXT_BIT);
				sup++;
				break;
			case 20:	/* /SUP */
				NEW_BIT (NEW_TEXT_BIT);
				if (sup)
					sup--;
				break;
			case 21:	/* PRE */
				NEW_BIT (NEW_TEXT_BIT);
				pre++;
				break;
			case 22:	/* /PRE */
				NEW_BIT (NEW_TEXT_BIT);
				if (pre)
					pre--;
				break;
			case 23:	/* TITLE */
				NEW_BIT (NEW_TEXT_BIT);
				title++;
				break;
			case 24:	/* /TITLE */
				if (title) {
					if (options & GTK_IMHTML_NO_TITLE) {
						wpos = 0;
						ws [wpos] = '\0';
					}
					title--;
				}
				break;
			case 25:	/* BR */
				NEW_BIT (NEW_TEXT_BIT);
				NEW_BIT (NEW_BR_BIT);
				break;
			case 26:	/* HR */
				NEW_BIT (NEW_TEXT_BIT);
				NEW_BIT (NEW_SEP_BIT);
				break;
			case 27:	/* /FONT */
				if (fonts) {
					FontDetail *font = fonts->data;
					NEW_BIT (NEW_TEXT_BIT);
					fonts = g_slist_remove (fonts, font);
					if (font->face)
						g_free (font->face);
					if (font->fore)
						gdk_color_free (font->fore);
					if (font->back)
						gdk_color_free (font->back);
					g_free (font);
				}
				break;
			case 28:	/* /A */
				if (url) {
					NEW_BIT (NEW_TEXT_BIT);
					g_free (url);
					url = NULL;
				}
				break;
			case 29:	/* P */
			case 30:	/* /P */
			case 31:	/* H3 */
			case 32:	/* /H3 */
			case 33:	/* HTML */
			case 34:	/* /HTML */
			case 35:	/* BODY */
			case 36:	/* /BODY */
			case 37:	/* FONT */
			case 38:	/* HEAD */
			case 39:	/* /HEAD */
				break;

			case 40:	/* HR (opt) */
				NEW_BIT (NEW_TEXT_BIT);
				NEW_BIT (NEW_SEP_BIT);
				break;
			case 41:	/* FONT (opt) */
			{
				gchar *color, *back, *face, *size;
				FontDetail *font;

				color = gtk_imhtml_get_html_opt (tag, "COLOR=");
				back = gtk_imhtml_get_html_opt (tag, "BACK=");
				face = gtk_imhtml_get_html_opt (tag, "FACE=");
				size = gtk_imhtml_get_html_opt (tag, "SIZE=");

				if (!(color || back || face || size))
					break;

				NEW_BIT (NEW_TEXT_BIT);

				font = g_new0 (FontDetail, 1);
				if (color && !(options & GTK_IMHTML_NO_COLOURS))
					font->fore = gtk_imhtml_get_color (color);
				if (back && !(options & GTK_IMHTML_NO_COLOURS))
					font->back = gtk_imhtml_get_color (back);
				if (face && !(options & GTK_IMHTML_NO_FONTS))
					font->face = g_strdup (face);
				if (size && !(options & GTK_IMHTML_NO_SIZES))
					sscanf (size, "%hd", &font->size);

				g_free (color);
				g_free (back);
				g_free (face);
				g_free (size);

				if (fonts) {
					FontDetail *oldfont = fonts->data;
					if (!font->size)
						font->size = oldfont->size;
					if (!font->face && oldfont->face) 
						font->face = g_strdup (oldfont->face);
					if (!font->fore && oldfont->fore)
						font->fore = gdk_color_copy (oldfont->fore);
					if (!font->back && oldfont->back)
						font->back = gdk_color_copy (oldfont->back);
				} else {
					if (!font->size)
						font->size = DEFAULT_FONT_SIZE;
				}

				fonts = g_slist_prepend (fonts, font);
			}
				break;
			case 42:	/* BODY (opt) */
			{
				gchar *bgcolor = gtk_imhtml_get_html_opt (tag, "BGCOLOR=");
				if (bgcolor) {
					GdkColor *tmp = gtk_imhtml_get_color (bgcolor);
					g_free (bgcolor);
					if (tmp) {
						NEW_BIT (NEW_TEXT_BIT);
						bg = tmp;
						UPDATE_BG_COLORS;
					}
				}
			}
				break;
			case 43:	/* A (opt) */
			{
				gchar *href = gtk_imhtml_get_html_opt (tag, "HREF=");
				if (href) {
					NEW_BIT (NEW_TEXT_BIT);
					g_free (url);
					url = href;
				}
			}
				break;
			case 44:	/* IMG (opt) */
			{
				gchar *src = gtk_imhtml_get_html_opt (tag, "SRC=");
				gchar **xpm;
				GdkColor *clr;
				GtkIMHtmlBit *bit;

				if (!src)
					break;

				if (!imhtml->img || ((xpm = imhtml->img (src)) == NULL)) {
					g_free (src);
					break;
				}

				if (!fonts || ((clr = ((FontDetail *) fonts->data)->back) == NULL))
					clr = (bg != NULL) ? bg : imhtml->default_bg_color;

				if (!GTK_WIDGET_REALIZED (imhtml))
					gtk_widget_realize (GTK_WIDGET (imhtml));

				bit = g_new0 (GtkIMHtmlBit, 1);
				bit->type = TYPE_IMG;
				bit->pm = gdk_pixmap_create_from_xpm_d (GTK_WIDGET (imhtml)->window,
									&bit->bm, clr, xpm);
				if (url)
					bit->url = g_strdup (url);

				NEW_BIT (bit);

				g_free (src);
			}
				break;
			case 45:	/* P (opt) */
			case 46:	/* H3 (opt) */
				break;
			case 47:	/* comment */
				NEW_BIT (NEW_TEXT_BIT);
				wpos = g_snprintf (ws, len, "%s", tag);
				NEW_BIT (NEW_COMMENT_BIT);
				break;
			default:
				break;
			}
			g_free (tag);
			c += tlen;
			pos += tlen;
		} else if (*c == '&' && gtk_imhtml_is_amp_escape (c, &amp, &tlen)) {
			ws [wpos++] = amp;
			c += tlen;
			pos += tlen;
		} else if (*c == '\n') {
			if (!(options & GTK_IMHTML_NO_NEWLINE)) {
				NEW_BIT (NEW_TEXT_BIT);
				NEW_BIT (NEW_BR_BIT);
			}
			c++;
			pos++;
		} else if (gtk_imhtml_is_smiley (imhtml, c, &smilelen)) {
			NEW_BIT (NEW_TEXT_BIT);
			g_snprintf (ws, smilelen + 1, "%s", c);
			wpos = smilelen + 1;
			NEW_BIT (NEW_SMILEY_BIT);
			c += smilelen;
			pos += smilelen;
		} else if (*c) {
			ws [wpos++] = *c++;
			pos++;
		} else {
			break;
		}
	}

	NEW_BIT (NEW_TEXT_BIT);

	while (newbits) {
		GtkIMHtmlBit *bit = newbits->data;
		imhtml->bits = g_list_append (imhtml->bits, bit);
		newbits = g_list_remove (newbits, bit);
		gtk_imhtml_draw_bit (imhtml, bit);
	}

	GTK_LAYOUT (imhtml)->height = imhtml->y;
	GTK_LAYOUT (imhtml)->vadjustment->upper = imhtml->y;
	gtk_signal_emit_by_name (GTK_OBJECT (GTK_LAYOUT (imhtml)->vadjustment), "changed");

	gtk_widget_set_usize (GTK_WIDGET (imhtml), -1, imhtml->y);

#if GTK_CHECK_VERSION(1,3,0)
	if (!(options & GTK_IMHTML_NO_SCROLL) &&
	    scrolldown &&
	    (imhtml->y >= MAX (1,
			       (GTK_WIDGET (imhtml)->allocation.height -
				(GTK_WIDGET (imhtml)->style->ythickness + BORDER_SIZE) * 2))))
		gtk_adjustment_set_value (vadj, imhtml->y -
					  MAX (1, (GTK_WIDGET (imhtml)->allocation.height - 
						   (GTK_WIDGET (imhtml)->style->ythickness +
						    BORDER_SIZE) * 2)));
#else
	if (!(options & GTK_IMHTML_NO_SCROLL) &&
	    scrolldown &&
	    (imhtml->y >= MAX (1,
			       (GTK_WIDGET (imhtml)->allocation.height -
				(GTK_WIDGET (imhtml)->style->klass->ythickness + BORDER_SIZE) * 2))))
		gtk_adjustment_set_value (vadj, imhtml->y -
					  MAX (1, (GTK_WIDGET (imhtml)->allocation.height - 
						   (GTK_WIDGET (imhtml)->style->klass->ythickness +
						    BORDER_SIZE) * 2)));
#endif

	if (url) {
		g_free (url);
		if (retval)
			retval = g_string_append (retval, "</A>");
	}
	if (bg)
		gdk_color_free (bg);
	while (fonts) {
		FontDetail *font = fonts->data;
		fonts = g_slist_remove (fonts, font);
		if (font->face)
			g_free (font->face);
		if (font->fore)
			gdk_color_free (font->fore);
		if (font->back)
			gdk_color_free (font->back);
		g_free (font);
		if (retval)
			retval = g_string_append (retval, "</FONT>");
	}
	if (retval) {
		while (bold) {
			retval = g_string_append (retval, "</B>");
			bold--;
		}
		while (italics) {
			retval = g_string_append (retval, "</I>");
			italics--;
		}
		while (underline) {
			retval = g_string_append (retval, "</U>");
			underline--;
		}
		while (strike) {
			retval = g_string_append (retval, "</S>");
			strike--;
		}
		while (sub) {
			retval = g_string_append (retval, "</SUB>");
			sub--;
		}
		while (sup) {
			retval = g_string_append (retval, "</SUP>");
			sup--;
		}
		while (title) {
			retval = g_string_append (retval, "</TITLE>");
			title--;
		}
		while (pre) {
			retval = g_string_append (retval, "</PRE>");
			pre--;
		}
	}
	g_free (ws);

	return retval;
}

void
gtk_imhtml_clear (GtkIMHtml *imhtml)
{
	GtkLayout *layout;

	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	layout = GTK_LAYOUT (imhtml);

	while (imhtml->bits) {
		GtkIMHtmlBit *bit = imhtml->bits->data;
		imhtml->bits = g_list_remove (imhtml->bits, bit);
		if (bit->text)
			g_free (bit->text);
		if (bit->font)
			gdk_font_unref (bit->font);
		if (bit->fore)
			gdk_color_free (bit->fore);
		if (bit->back)
			gdk_color_free (bit->back);
		if (bit->bg)
			gdk_color_free (bit->bg);
		if (bit->url)
			g_free (bit->url);
		if (bit->pm)
			gdk_pixmap_unref (bit->pm);
		if (bit->bm)
			gdk_bitmap_unref (bit->bm);
		while (bit->chunks) {
			struct line_info *li = bit->chunks->data;
			if (li->text)
				g_free (li->text);
			bit->chunks = g_list_remove (bit->chunks, li);
			g_free (li);
		}
		g_free (bit);
	}

	while (imhtml->click) {
		g_free (imhtml->click->data);
		imhtml->click = g_list_remove (imhtml->click, imhtml->click->data);
	}

	if (imhtml->selected_text) {
		g_string_free (imhtml->selected_text, TRUE);
		imhtml->selected_text = g_string_new ("");
	}

	imhtml->sel_startx = 0;
	imhtml->sel_starty = 0;
	imhtml->sel_endx = 0;
	imhtml->sel_endx = 0;
	imhtml->sel_endchunk = NULL;

	if (imhtml->tip_timer) {
		gtk_timeout_remove (imhtml->tip_timer);
		imhtml->tip_timer = 0;
	}
	if (imhtml->tip_window) {
		gtk_widget_destroy (imhtml->tip_window);
		imhtml->tip_window = NULL;
	}
	imhtml->tip_bit = NULL;

	if (imhtml->scroll_timer) {
		gtk_timeout_remove (imhtml->scroll_timer);
		imhtml->scroll_timer = 0;
	}

	imhtml->x = 0;
	imhtml->y = TOP_BORDER;
	imhtml->xsize = 0;
	imhtml->llheight = 0;
	imhtml->llascent = 0;
	if (imhtml->line)
		g_list_free (imhtml->line);
	imhtml->line = NULL;

	layout->hadjustment->page_size = 0;
	layout->hadjustment->page_increment = 0;
	layout->hadjustment->lower = 0;
	layout->hadjustment->upper = imhtml->x;
	gtk_adjustment_set_value (layout->hadjustment, 0);

	layout->vadjustment->page_size = 0;
	layout->vadjustment->page_increment = 0;
	layout->vadjustment->lower = 0;
	layout->vadjustment->upper = imhtml->y;
	gtk_adjustment_set_value (layout->vadjustment, 0);

	if (GTK_WIDGET_REALIZED (GTK_WIDGET (imhtml))) {
		gdk_window_set_cursor (GTK_LAYOUT (imhtml)->bin_window, imhtml->arrow_cursor);
		gdk_window_clear (GTK_LAYOUT (imhtml)->bin_window);
		gtk_signal_emit_by_name (GTK_OBJECT (layout->hadjustment), "changed");
		gtk_signal_emit_by_name (GTK_OBJECT (layout->vadjustment), "changed");
	}
}

void
gtk_imhtml_page_up (GtkIMHtml *imhtml)
{
	GtkAdjustment *vadj;

	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	vadj = GTK_LAYOUT (imhtml)->vadjustment;
	gtk_adjustment_set_value (vadj, MAX (vadj->value - vadj->page_increment,
					     vadj->lower));
	gtk_signal_emit_by_name (GTK_OBJECT (vadj), "changed");
}

void
gtk_imhtml_page_down (GtkIMHtml *imhtml)
{
	GtkAdjustment *vadj;

	g_return_if_fail (imhtml != NULL);
	g_return_if_fail (GTK_IS_IMHTML (imhtml));

	vadj = GTK_LAYOUT (imhtml)->vadjustment;
	gtk_adjustment_set_value (vadj, MIN (vadj->value + vadj->page_increment,
					     vadj->upper - vadj->page_size));
	gtk_signal_emit_by_name (GTK_OBJECT (vadj), "changed");
}
