// SPDX-License-Identifier: GPL-2.0-or-later
/* pps-annotation-window.c
 *  this file is part of papers, a gnome document viewer
 *
 * Copyright (C) 2009 Carlos Garcia Campos <carlosgc@gnome.org>
 * Copyright (C) 2007 Iñigo Martinez <inigomartinez@gmail.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <adwaita.h>
#include <string.h>

#include <pps-document-annotations.h>

#include "pps-annotation-window.h"
#include "pps-css-utils.h"
#include "pps-document-misc.h"

#if HAVE_LIBSPELLING
#include <libspelling.h>
#endif

enum {
	PROP_0,
	PROP_ANNOTATION,
	PROP_PARENT,
};

struct _PpsAnnotationWindow {
	GtkWindow base_instance;

	PpsAnnotation *annotation;
	GtkWindow *parent;

	GtkWidget *headerbar;
	GtkWidget *title_label;
	GtkWidget *text_view;

	gboolean is_open;

#if HAVE_LIBSPELLING
	SpellingTextBufferAdapter *adapter;
	gboolean enable_spellchecking;
#endif
};

struct _PpsAnnotationWindowClass {
	GtkWindowClass base_class;
};

G_DEFINE_TYPE (PpsAnnotationWindow, pps_annotation_window, GTK_TYPE_WINDOW)

static void
pps_annotation_window_sync_contents (PpsAnnotationWindow *window)
{
	g_autofree gchar *contents = NULL;
	GtkTextIter start, end;
	GtkTextBuffer *buffer;
	PpsAnnotation *annot = window->annotation;

	if (!window->annotation)
		return;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (window->text_view));
	gtk_text_buffer_get_bounds (buffer, &start, &end);
	contents = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	pps_annotation_set_contents (annot, contents);
}

static void
pps_annotation_window_set_color (PpsAnnotationWindow *window,
                                 GdkRGBA *color)
{
	GtkCssProvider *css_provider = gtk_css_provider_new ();
	g_autofree char *rgba_str = gdk_rgba_to_string (color);
	g_autofree char *css_data = NULL;
	g_autofree char *annotation_id_class = NULL;
	g_autofree char *filtered_annot_name = NULL;
	GdkDisplay *display = gdk_display_get_default ();

	if (display == NULL)
		return;

	filtered_annot_name = pps_css_utils_filter_class_chars (pps_annotation_get_name (window->annotation));
	annotation_id_class = g_strdup_printf ("annotation-%i-%s",
	                                       pps_annotation_get_page_index (window->annotation),
	                                       filtered_annot_name);

	css_data = g_strdup_printf (".%s { --annotation-color: %s; }",
	                            annotation_id_class,
	                            rgba_str);

	gtk_widget_add_css_class (GTK_WIDGET (window), annotation_id_class);

	gtk_css_provider_load_from_string (css_provider, css_data);
	gtk_style_context_add_provider_for_display (display,
	                                            GTK_STYLE_PROVIDER (css_provider),
	                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
pps_annotation_window_label_changed (PpsAnnotationMarkup *annot,
                                     GParamSpec *pspec,
                                     PpsAnnotationWindow *window)
{
	const gchar *label = pps_annotation_markup_get_label (annot);

	gtk_label_set_text (GTK_LABEL (window->title_label), label);
}

static void
pps_annotation_window_contents_changed (PpsAnnotationMarkup *annot,
                                        GParamSpec *pspec,
                                        PpsAnnotationWindow *window)
{
	const gchar *contents = pps_annotation_get_contents (window->annotation);

	if (contents) {
		GtkTextBuffer *buffer;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (window->text_view));
		gtk_text_buffer_set_text (buffer, contents, -1);
	}
}

static void
pps_annotation_window_color_changed (PpsAnnotation *annot,
                                     GParamSpec *pspec,
                                     PpsAnnotationWindow *window)
{
	GdkRGBA rgba;

	pps_annotation_get_rgba (annot, &rgba);
	pps_annotation_window_set_color (window, &rgba);
}

static void
pps_annotation_window_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
	PpsAnnotationWindow *window = PPS_ANNOTATION_WINDOW (object);

	switch (prop_id) {
	case PROP_ANNOTATION:
		window->annotation = g_value_dup_object (value);
		break;
	case PROP_PARENT:
		window->parent = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pps_annotation_window_init (PpsAnnotationWindow *window)
{
	GtkWidget *vbox;
	GtkWidget *swindow;

	gtk_widget_add_css_class (GTK_WIDGET (window), "pps-annotation-window");

	gtk_widget_set_can_focus (GTK_WIDGET (window), TRUE);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	/* Title bar */
	window->headerbar = adw_header_bar_new ();
	gtk_window_set_titlebar (GTK_WINDOW (window), window->headerbar);

	window->title_label = gtk_label_new (NULL);
	gtk_widget_set_halign (window->title_label, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (window->title_label, TRUE);
	adw_header_bar_set_title_widget (ADW_HEADER_BAR (window->headerbar),
	                                 window->title_label);

	/* Contents */
	swindow = gtk_scrolled_window_new ();

#if HAVE_LIBSPELLING
	window->text_view = gtk_source_view_new ();
	window->adapter = spelling_text_buffer_adapter_new (
	    GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (window->text_view))),
	    spelling_checker_get_default ());
	gtk_text_view_set_extra_menu (GTK_TEXT_VIEW (window->text_view),
	                              spelling_text_buffer_adapter_get_menu_model (window->adapter));
	gtk_widget_insert_action_group (window->text_view, "spelling", G_ACTION_GROUP (window->adapter));
	spelling_text_buffer_adapter_set_enabled (window->adapter, TRUE);
#else
	window->text_view = gtk_text_view_new ();
#endif

	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (window->text_view), TRUE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (window->text_view), GTK_WRAP_WORD);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (swindow), window->text_view);
	gtk_widget_set_valign (swindow, GTK_ALIGN_FILL);
	gtk_widget_set_vexpand (swindow, TRUE);
	gtk_window_set_focus (GTK_WINDOW (window), window->text_view);
	g_signal_connect_swapped (window->text_view, "notify::has-focus",
	                          G_CALLBACK (pps_annotation_window_sync_contents),
	                          window);

	gtk_box_append (GTK_BOX (vbox), swindow);

	gtk_window_set_child (GTK_WINDOW (window), vbox);

	gtk_window_set_decorated (GTK_WINDOW (window), TRUE);
	gtk_window_set_resizable (GTK_WINDOW (window), TRUE);
	gtk_window_set_hide_on_close (GTK_WINDOW (window), TRUE);
	gtk_widget_add_css_class (GTK_WIDGET (window), "pps-annotation-window");
}

static GObject *
pps_annotation_window_constructor (GType type,
                                   guint n_construct_properties,
                                   GObjectConstructParam *construct_params)
{
	GObject *object;
	PpsAnnotationWindow *window;
	PpsAnnotation *annot;
	PpsAnnotationMarkup *markup;
	const gchar *contents;
	const gchar *label;
	GdkRGBA color;
	PpsRectangle rect;
	gdouble scale;

	object = G_OBJECT_CLASS (pps_annotation_window_parent_class)->constructor (type, n_construct_properties, construct_params);
	window = PPS_ANNOTATION_WINDOW (object);
	annot = window->annotation;
	markup = PPS_ANNOTATION_MARKUP (annot);

	gtk_window_set_transient_for (GTK_WINDOW (window), window->parent);
	gtk_window_set_destroy_with_parent (GTK_WINDOW (window), FALSE);

	label = pps_annotation_markup_get_label (markup);
	if (label == NULL || *label == '\0')
		label = _ ("Edit Note");
	window->is_open = pps_annotation_markup_get_popup_is_open (markup);
	pps_annotation_markup_get_rectangle (markup, &rect);

	/* Rectangle is at doc resolution (72.0) */
	scale = pps_document_misc_get_widget_dpi (GTK_WIDGET (window)) / 72.0;
	gtk_window_set_default_size (GTK_WINDOW (window),
	                             (gint) ((rect.x2 - rect.x1) * scale),
	                             (gint) ((rect.y2 - rect.y1) * scale));

	pps_annotation_get_rgba (annot, &color);
	pps_annotation_window_set_color (window, &color);

	gtk_widget_set_name (GTK_WIDGET (window), pps_annotation_get_name (annot));
	gtk_label_set_text (GTK_LABEL (window->title_label), label);

	contents = pps_annotation_get_contents (annot);
	if (contents) {
		GtkTextBuffer *buffer;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (window->text_view));
		gtk_text_buffer_set_text (buffer, contents, -1);
	}

	g_signal_connect (annot, "notify::label",
	                  G_CALLBACK (pps_annotation_window_label_changed),
	                  window);
	g_signal_connect (annot, "notify::rgba",
	                  G_CALLBACK (pps_annotation_window_color_changed),
	                  window);
	g_signal_connect (annot, "notify::contents",
	                  G_CALLBACK (pps_annotation_window_contents_changed),
	                  window);

	return object;
}

static gboolean
pps_annotation_window_escape_pressed (GtkWidget *widget,
                                      GVariant *args,
                                      gpointer user_data)
{
	gtk_window_close (GTK_WINDOW (widget));
	return TRUE;
}

static void
pps_annotation_window_dispose (GObject *object)
{
	PpsAnnotationWindow *window = PPS_ANNOTATION_WINDOW (object);

	g_signal_handlers_disconnect_by_func (window->annotation,
	                                      G_CALLBACK (pps_annotation_window_label_changed),
	                                      window);
	g_signal_handlers_disconnect_by_func (window->annotation,
	                                      G_CALLBACK (pps_annotation_window_color_changed),
	                                      window);
	g_signal_handlers_disconnect_by_func (window->annotation,
	                                      G_CALLBACK (pps_annotation_window_contents_changed),
	                                      window);

	g_object_set_data (G_OBJECT (window->annotation), "popup",
	                   NULL);

	G_OBJECT_CLASS (pps_annotation_window_parent_class)->dispose (object);
}

static gboolean
pps_annotation_window_close_request (GtkWindow *window)
{
	pps_annotation_window_sync_contents (PPS_ANNOTATION_WINDOW (window));
	((PpsAnnotationWindow *) window)->is_open = FALSE;

	return GTK_WINDOW_CLASS (pps_annotation_window_parent_class)->close_request (window);
}

static void
pps_annotation_window_class_init (PpsAnnotationWindowClass *klass)
{
	GObjectClass *g_object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *gtk_widget_class = GTK_WIDGET_CLASS (klass);
	GtkWindowClass *gtk_window_class = GTK_WINDOW_CLASS (klass);

	g_object_class->constructor = pps_annotation_window_constructor;
	g_object_class->set_property = pps_annotation_window_set_property;
	g_object_class->dispose = pps_annotation_window_dispose;
	gtk_window_class->close_request = pps_annotation_window_close_request;

	gtk_widget_class_add_binding (gtk_widget_class, GDK_KEY_Escape, 0,
	                              pps_annotation_window_escape_pressed, NULL);

	g_object_class_install_property (g_object_class,
	                                 PROP_ANNOTATION,
	                                 g_param_spec_object ("annotation",
	                                                      "Annotation",
	                                                      "The annotation associated to the window",
	                                                      PPS_TYPE_ANNOTATION_MARKUP,
	                                                      G_PARAM_WRITABLE |
	                                                          G_PARAM_CONSTRUCT_ONLY |
	                                                          G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (g_object_class,
	                                 PROP_PARENT,
	                                 g_param_spec_object ("parent",
	                                                      "Parent",
	                                                      "The parent window",
	                                                      GTK_TYPE_WINDOW,
	                                                      G_PARAM_WRITABLE |
	                                                          G_PARAM_CONSTRUCT_ONLY |
	                                                          G_PARAM_STATIC_STRINGS));
}

/* Public methods */
GtkWidget *
pps_annotation_window_new (PpsAnnotationMarkup *annot,
                           GtkWindow *parent)
{
	GtkWidget *window;

	g_return_val_if_fail (PPS_IS_ANNOTATION_MARKUP (annot), NULL);
	g_return_val_if_fail (GTK_IS_WINDOW (parent), NULL);

	window = g_object_new (PPS_TYPE_ANNOTATION_WINDOW,
	                       "annotation", annot,
	                       "parent", parent,
	                       NULL);
	return window;
}

PpsAnnotation *
pps_annotation_window_get_annotation (PpsAnnotationWindow *window)
{
	g_return_val_if_fail (PPS_IS_ANNOTATION_WINDOW (window), NULL);

	return window->annotation;
}

gboolean
pps_annotation_window_is_open (PpsAnnotationWindow *window)
{
	g_return_val_if_fail (PPS_IS_ANNOTATION_WINDOW (window), FALSE);

	return window->is_open;
}

void
pps_annotation_window_show (PpsAnnotationWindow *window)
{
	g_return_if_fail (PPS_IS_ANNOTATION_WINDOW (window));

	window->is_open = TRUE;
	gtk_widget_set_visible (GTK_WIDGET (window), TRUE);
}

void
pps_annotation_window_set_enable_spellchecking (PpsAnnotationWindow *window,
                                                gboolean enable_spellchecking)
{
	g_return_if_fail (PPS_IS_ANNOTATION_WINDOW (window));

#if HAVE_LIBSPELLING
	if (enable_spellchecking == window->enable_spellchecking)
		return;

	window->enable_spellchecking = enable_spellchecking;
	spelling_text_buffer_adapter_set_enabled (window->adapter, enable_spellchecking);
#endif
}
