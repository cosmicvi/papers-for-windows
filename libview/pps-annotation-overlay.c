// SPDX-License-Identifier: GPL-2.0-or-later
/* pps-annotation-overlay.c
 * this file is part of papers, a gnome document viewer
 *
 * Copyright (C) 2025 Lucas Baudin <lbaudin@gnome.org>
 */

#include "pps-annotation-overlay.h"
#include "papers-document.h"
#include "pps-css-utils.h"
#include "pps-overlay.h"
#include "pps-view-private.h"
#include "pps-view.h"

#include <config.h>

#include <adwaita.h>
#include <glib/gi18n-lib.h>

enum { PROP_0,
       PROP_ANNOTATION,
       PROP_ANNOTATIONS_CONTEXT,
       PROP_DOCUMENT_MODEL };

typedef struct {
	PpsAnnotation *annotation;
	PpsAnnotationsContext *annots_context;
	PpsDocumentModel *model;
	GtkCssProvider *css;
	GtkBox *box;

	gdouble initial_proportion;

	GtkWidget *internal_overlay;
	int id;

	gboolean annot_removed;
} PpsOverlayAnnotationPrivate;

static void
pps_overlay_overlay_annotation_iface_init (PpsOverlayInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (PpsOverlayAnnotation, pps_overlay_annotation, GTK_TYPE_BOX, G_ADD_PRIVATE (PpsOverlayAnnotation) G_IMPLEMENT_INTERFACE (PPS_TYPE_OVERLAY, pps_overlay_overlay_annotation_iface_init))

#define OVERLAY_GET_PRIVATE(o) pps_overlay_annotation_get_instance_private (PPS_OVERLAY_ANNOTATION (o))

typedef struct {
	GtkPicture *image;
} PpsOverlayAnnotationImagePrivate;

struct _PpsOverlayAnnotationImage {
	PpsOverlayAnnotation base_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE (PpsOverlayAnnotationImage, pps_overlay_annotation_image, PPS_TYPE_OVERLAY_ANNOTATION)

#define IMAGE_GET_PRIVATE(o) pps_overlay_annotation_image_get_instance_private (o)

typedef struct {
	GtkTextView *text_view;

	gboolean ignore_content_changed;
	PpsAnnotationEditingState previous_editing_state;
} PpsOverlayAnnotationEntryPrivate;

struct _PpsOverlayAnnotationEntry {
	PpsOverlayAnnotation base_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE (PpsOverlayAnnotationEntry, pps_overlay_annotation_entry, PPS_TYPE_OVERLAY_ANNOTATION)
#define ENTRY_GET_PRIVATE(o) pps_overlay_annotation_entry_get_instance_private (o)

// This is called when the moving gesture ends and when the resize gesture ends
static void
pps_overlay_annotation_drag_end (GtkGestureDrag *annotation_drag_gesture,
                                 gdouble offset_x,
                                 gdouble offset_y,
                                 PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	if (PPS_IS_ANNOTATION_STAMP (priv->annotation)) {
		PpsAnnotationEditingState state = pps_document_model_get_annotation_editing_state (priv->model);
		pps_document_model_set_annotation_editing_state (priv->model, state & ~PPS_ANNOTATION_EDITING_STATE_STAMP);
	}
	gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
pps_overlay_annotation_drag_update (GtkGestureDrag *annotation_drag_gesture,
                                    gdouble offset_x,
                                    gdouble offset_y,
                                    PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	gdouble page_width, page_height, scale, real_offset_x, real_offset_y;
	PpsRectangle rect;

	gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture), GTK_EVENT_SEQUENCE_CLAIMED);

	pps_annotation_get_area (priv->annotation, &rect);

	scale = pps_document_model_get_scale (priv->model);
	pps_document_get_page_size (pps_document_model_get_document (priv->model), pps_annotation_get_page_index (priv->annotation), &page_width, &page_height);

	/* The following just adds offset_x to x and offset_y to y but some clamping must be made */

	real_offset_x = CLAMP (rect.x1 + offset_x / scale, 0, page_width - (rect.x2 - rect.x1)) - rect.x1;
	real_offset_y = CLAMP (rect.y1 + offset_y / scale, 0, page_height - (rect.y2 - rect.y1)) - rect.y1;

	/* Don't move if the offset is small since gtk rounds coordinates and this may result in a weird quivering */

	if (real_offset_x * scale <= 0.5 && real_offset_x * scale >= -0.5) {
		real_offset_x = 0;
	}

	if (real_offset_y * scale <= 0.5 && real_offset_y * scale >= -0.5) {
		real_offset_y = 0;
	}

	rect.x1 += real_offset_x;
	rect.x2 += real_offset_x;
	rect.y1 += real_offset_y;
	rect.y2 += real_offset_y;

	pps_annotation_set_area (priv->annotation, &rect);

	gtk_widget_queue_resize (GTK_WIDGET (overlay));
}

static void
pps_overlay_annotation_resize_begin (GtkGestureDrag *annotation_drag_gesture,
                                     gdouble offset_x,
                                     gdouble offset_y,
                                     PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	PpsRectangle rect;

	if (PPS_IS_ANNOTATION_STAMP (priv->annotation)) {
		PpsAnnotationEditingState state = pps_document_model_get_annotation_editing_state (priv->model);
		pps_document_model_set_annotation_editing_state (priv->model, state | PPS_ANNOTATION_EDITING_STATE_STAMP);
	}

	pps_annotation_get_area (priv->annotation, &rect);
	priv->initial_proportion = (rect.x2 - rect.x1) / (rect.y2 - rect.y1);

	gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture),
	                       GTK_EVENT_SEQUENCE_CLAIMED);

	pps_overlay_annotation_grab_focus (overlay, -1, -1);
}

static void
pps_overlay_annotation_resize_update (GtkGestureDrag *annotation_drag_gesture,
                                      gdouble offset_x,
                                      gdouble offset_y,
                                      PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	gdouble page_width, page_height, scale, real_offset_x, real_offset_y, x1, x2, y1, y2;
	PpsRectangle rect;
	if (ABS (offset_x) <= 2.) {
		return;
	}

	scale = pps_document_model_get_scale (priv->model);
	pps_document_get_page_size (pps_document_model_get_document (priv->model), pps_annotation_get_page_index (priv->annotation), &page_width, &page_height);
	pps_annotation_get_area (priv->annotation, &rect);

	/* The following just adds offset_x to x and offset_y to y but some clamping must be made */
	x1 = rect.x1;
	y1 = rect.y1;
	x2 = rect.x2;
	y2 = rect.y2;

	real_offset_x = CLAMP (x2 + offset_x / scale, x1 + 5, page_width) - x2;
	real_offset_y = CLAMP (y2 + real_offset_x / priv->initial_proportion, y1 + 5, page_height) - y2;
	real_offset_x = real_offset_y * priv->initial_proportion;

	rect.x2 += real_offset_x;
	rect.y2 += real_offset_y;
	pps_annotation_set_area (priv->annotation, &rect);
	if (PPS_IS_ANNOTATION_FREE_TEXT (priv->annotation)) {
		g_autoptr (PangoFontDescription) desc = pps_annotation_free_text_get_font_description (PPS_ANNOTATION_FREE_TEXT (priv->annotation));
		pango_font_description_set_size (desc, pango_font_description_get_size (desc) * (y2 - y1 + real_offset_y) / (y2 - y1));
		pps_annotation_free_text_set_font_description (PPS_ANNOTATION_FREE_TEXT (priv->annotation), desc);
		pps_annotation_model_set_font_size (pps_document_model_get_annotation_model (priv->model), ((double) pango_font_description_get_size (desc)) / PANGO_SCALE);
	}

	gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	gtk_widget_queue_resize (GTK_WIDGET (overlay));
}

static void
pps_overlay_annotation_drag_begin (GtkGestureDrag *annotation_drag_gesture,
                                   gdouble offset_x,
                                   gdouble offset_y,
                                   PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	GtkWidget *child;

	if (PPS_IS_ANNOTATION_STAMP (priv->annotation)) {
		PpsAnnotationEditingState state = pps_document_model_get_annotation_editing_state (priv->model);
		pps_document_model_set_annotation_editing_state (priv->model, state | PPS_ANNOTATION_EDITING_STATE_STAMP);
	}

	/* We claim this if we are not above a child widget or if drag_only_on_border is not set */

	if (PPS_OVERLAY_ANNOTATION_GET_CLASS (overlay)->drag_only_on_border) {
		child = gtk_widget_get_first_child (GTK_WIDGET (priv->box));
		for (; child != NULL;
		     child = gtk_widget_get_next_sibling (child)) {
			graphene_point_t point = { offset_x, offset_y };
			graphene_point_t widget_point;
			g_assert (gtk_widget_compute_point (GTK_WIDGET (overlay), child, &point, &widget_point));
			if (gtk_widget_contains (child, widget_point.x, widget_point.y)) {
				gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture), GTK_EVENT_SEQUENCE_DENIED);
				return;
			}
		}
	}
	child = gtk_widget_get_next_sibling (GTK_WIDGET (priv->box));
	for (; child != NULL;
	     child = gtk_widget_get_next_sibling (child)) {
		graphene_point_t point = { offset_x, offset_y };
		graphene_point_t widget_point;
		g_assert (gtk_widget_compute_point (GTK_WIDGET (overlay), child, &point, &widget_point));
		if (gtk_widget_contains (child, widget_point.x, widget_point.y)) {
			gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture), GTK_EVENT_SEQUENCE_DENIED);
			return;
		}
	}

	pps_overlay_annotation_grab_focus (overlay, -1, -1);

	gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture),
	                       GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
pps_overlay_annotation_update_css (PpsOverlayAnnotation *overlay)
{
	GdkRGBA bg_rgba, accent_color;
	g_autofree gchar *additional_style, *accent_color_str, *bg_rgba_str, *accent_color_dimmed_str;
	AdwStyleManager *style_manager = adw_style_manager_get_default ();
	AdwAccentColor accent = adw_style_manager_get_accent_color (style_manager);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);

	pps_annotation_get_rgba (priv->annotation, &bg_rgba);
	adw_accent_color_to_rgba (accent, &accent_color);
	accent_color.alpha = 1.0;
	accent_color_str = gdk_rgba_to_string (&accent_color);
	accent_color.alpha = .3;
	accent_color_dimmed_str = gdk_rgba_to_string (&accent_color);
	bg_rgba_str = gdk_rgba_to_string (&bg_rgba);

	additional_style = PPS_OVERLAY_ANNOTATION_GET_CLASS (overlay)->style (overlay);

	const gchar *style = g_strdup_printf (
	    ".annot%d .overlay-annot-content {"
	    "background-color: %s;"
	    " }"
	    "%s ",
	    priv->id,
	    bg_rgba_str,
	    additional_style);
	gtk_css_provider_load_from_string (priv->css, style);

	gtk_widget_set_cursor_from_name (GTK_WIDGET (overlay), "grab");
}

static void
pps_overlay_annotation_annot_prop_changed (PpsAnnotation *annot,
                                           GParamSpec *pspec,
                                           PpsOverlayAnnotation *overlay)
{
	pps_overlay_annotation_update_css (overlay);
}

static void
pps_overlay_annotation_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (object);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);

	switch (prop_id) {
	case PROP_ANNOTATION:
		priv->annotation = g_value_dup_object (value);
		break;
	case PROP_ANNOTATIONS_CONTEXT:
		priv->annots_context = g_value_get_object (value);
		break;
	case PROP_DOCUMENT_MODEL:
		priv->model = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
annot_removed (PpsOverlayAnnotation *overlay, PpsAnnotation *annotation)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	if (annotation == priv->annotation) {
		priv->annot_removed = TRUE;
	}
}

static void
on_remove_button_clicked (PpsOverlayAnnotation *overlay, GtkWidget *button)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	pps_annotations_context_remove_annotation (priv->annots_context, priv->annotation);
}

static PpsRectangle *
pps_overlay_annotation_get_area (PpsOverlay *o, gdouble *padding)
{
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (o);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	PpsRectangle *rect = g_new (PpsRectangle, 1);
	pps_annotation_get_area (priv->annotation, rect);
	*padding = ANNOT_WIDGET_PADDING + ANNOT_WIDGET_BORDER_SIZE;
	return rect;
}

static void
pps_overlay_annotation_update_visibility_from_state (PpsOverlay *self, PpsRenderAnnotsFlags state)
{
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (self);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	if (PPS_IS_ANNOTATION_FREE_TEXT (priv->annotation)) {
		PpsOverlayAnnotationEntryPrivate *entry_priv = ENTRY_GET_PRIVATE (PPS_OVERLAY_ANNOTATION_ENTRY (self));
		if (state & PPS_RENDER_ANNOTS_FREETEXT) {
			entry_priv->previous_editing_state = entry_priv->previous_editing_state & ~PPS_ANNOTATION_EDITING_STATE_TEXT & ~PPS_ANNOTATION_EDITING_STATE_INSERT_TEXT;
		}
		gtk_widget_set_visible (GTK_WIDGET (entry_priv->text_view), !(state & PPS_RENDER_ANNOTS_FREETEXT));

	} else if (PPS_IS_ANNOTATION_STAMP (priv->annotation)) {
		PpsOverlayAnnotationImagePrivate *img_priv = IMAGE_GET_PRIVATE (PPS_OVERLAY_ANNOTATION_IMAGE (self));
		/* Outline mode: page is drawing the stamp; hide the overlay image
		 * to avoid showing the stamp twice. The border remains visible. */
		gtk_widget_set_visible (GTK_WIDGET (img_priv->image), !(state & PPS_RENDER_ANNOTS_STAMP));
	}
}

static void
pps_overlay_overlay_annotation_iface_init (PpsOverlayInterface *iface)
{
	iface->get_area = pps_overlay_annotation_get_area;
	iface->update_visibility_from_state = pps_overlay_annotation_update_visibility_from_state;
}

static void
pps_overlay_annotation_init (PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	static int id = 0;
	id++;
	priv->id = id;
	GtkGesture *gesture;
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

	gtk_widget_set_vexpand (box, TRUE);
	gtk_widget_set_hexpand (box, TRUE);
	priv->box = GTK_BOX (box);

	/* We need an internal overlay to display the resize handle */
	priv->internal_overlay = gtk_overlay_new ();

	gtk_widget_set_vexpand (priv->internal_overlay, TRUE);
	gtk_widget_set_hexpand (priv->internal_overlay, TRUE);

	gtk_overlay_set_child (GTK_OVERLAY (priv->internal_overlay), box);

	gtk_box_append (GTK_BOX (overlay), priv->internal_overlay);

	gesture = gtk_gesture_drag_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 1);
	g_signal_connect (gesture, "drag-update",
	                  G_CALLBACK (pps_overlay_annotation_drag_update), overlay);
	g_signal_connect (gesture, "drag-begin",
	                  G_CALLBACK (pps_overlay_annotation_drag_begin), overlay);
	g_signal_connect (gesture, "drag-end",
	                  G_CALLBACK (pps_overlay_annotation_drag_end), overlay);

	gtk_widget_add_controller (GTK_WIDGET (overlay), GTK_EVENT_CONTROLLER (gesture));

	gtk_widget_add_css_class (box, "overlay-annot-box");

	gtk_widget_add_css_class (GTK_WIDGET (overlay), "overlay-annot-wrapper");
	g_autofree gchar *annotid = g_strdup_printf ("annot%d", priv->id);

	gtk_widget_add_css_class (GTK_WIDGET (overlay), annotid);
}

static void
pps_overlay_annotation_constructed (GObject *obj)
{
	/* For some GObject reasons, object class is not set in the init function, so we
	have to add resizes handles here */
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (obj);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	GtkWidget *remove_button = gtk_button_new_from_icon_name ("cross-large-circle-filled-symbolic");

	G_OBJECT_CLASS (pps_overlay_annotation_parent_class)->constructed (obj);

	gtk_widget_add_css_class (remove_button, "annot-remove-button");
	gtk_widget_set_valign (remove_button, GTK_ALIGN_START);
	gtk_widget_set_halign (remove_button, GTK_ALIGN_END);
	gtk_widget_set_cursor_from_name (remove_button, "default");
	gtk_overlay_add_overlay (GTK_OVERLAY (priv->internal_overlay), remove_button);
	g_signal_connect_swapped (remove_button, "clicked", G_CALLBACK (on_remove_button_clicked), obj);

	if (PPS_OVERLAY_ANNOTATION_GET_CLASS (overlay)->resize_handle) {
		GtkGesture *gesture = gtk_gesture_drag_new ();
		GtkWidget *dot = gtk_button_new_from_icon_name ("bidirectional-arrow-symbolic");
		gtk_widget_add_css_class (dot, "annot-remove-button");

		gtk_widget_set_valign (dot, GTK_ALIGN_END);
		gtk_widget_set_halign (dot, GTK_ALIGN_END);
		gtk_overlay_add_overlay (GTK_OVERLAY (priv->internal_overlay), dot);
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 1);
		g_signal_connect (gesture, "drag-update",
		                  G_CALLBACK (pps_overlay_annotation_resize_update), overlay);
		g_signal_connect (gesture, "drag-begin",
		                  G_CALLBACK (pps_overlay_annotation_resize_begin), overlay);
		g_signal_connect (gesture, "drag-end",
		                  G_CALLBACK (pps_overlay_annotation_drag_end), overlay);
		gtk_widget_add_controller (dot, GTK_EVENT_CONTROLLER (gesture));
		gtk_widget_set_cursor_from_name (dot, "nwse-resize");
	}

	g_signal_connect_object (priv->annots_context, "annot-removed", G_CALLBACK (annot_removed), overlay, G_CONNECT_SWAPPED);
}

static GObject *
pps_overlay_annotation_constructor (GType type,
                                    guint n_construct_properties,
                                    GObjectConstructParam *construct_params)
{
	GObject *object;
	PpsOverlayAnnotation *overlay;
	GtkWidget *widget;
	PpsOverlayAnnotationPrivate *priv;

	object = G_OBJECT_CLASS (pps_overlay_annotation_parent_class)
	             ->constructor (type, n_construct_properties, construct_params);
	overlay = PPS_OVERLAY_ANNOTATION (object);
	priv = OVERLAY_GET_PRIVATE (overlay);
	widget = GTK_WIDGET (overlay);

	g_signal_connect (priv->model,
	                  "notify::scale",
	                  G_CALLBACK (pps_overlay_annotation_annot_prop_changed),
	                  overlay);

	/* Theming */
	priv->css = gtk_css_provider_new ();
	gtk_style_context_add_provider_for_display (gtk_widget_get_display (widget),
	                                            GTK_STYLE_PROVIDER (priv->css),
	                                            GTK_STYLE_PROVIDER_PRIORITY_USER);

	pps_overlay_annotation_update_css (overlay);

	return object;
}

static void
pps_overlay_annotation_dispose (GObject *object)
{
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (object);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);

	g_signal_handlers_disconnect_by_func (priv->annotation,
	                                      G_CALLBACK (pps_overlay_annotation_annot_prop_changed), overlay);
	g_signal_handlers_disconnect_by_func (priv->model,
	                                      G_CALLBACK (pps_overlay_annotation_annot_prop_changed), overlay);

	gtk_style_context_remove_provider_for_display (gtk_widget_get_display (GTK_WIDGET (object)),
	                                               GTK_STYLE_PROVIDER (priv->css));

	g_object_unref (priv->annotation);
	G_OBJECT_CLASS (pps_overlay_annotation_parent_class)->dispose (object);
}

static gchar *
pps_overlay_annotation_style (PpsOverlayAnnotation *ov)
{
	return g_strdup ("");
}

static void
pps_overlay_annotation_class_init (PpsOverlayAnnotationClass *klass)
{
	GObjectClass *g_object_class = G_OBJECT_CLASS (klass);

	g_object_class->constructor = pps_overlay_annotation_constructor;
	g_object_class->constructed = pps_overlay_annotation_constructed;
	g_object_class->set_property = pps_overlay_annotation_set_property;
	g_object_class->dispose = pps_overlay_annotation_dispose;
	klass->style = pps_overlay_annotation_style;

	g_object_class_install_property (
	    g_object_class,
	    PROP_ANNOTATION,
	    g_param_spec_object ("annotation",
	                         "Annotation",
	                         "The annotation associated to the window",
	                         PPS_TYPE_ANNOTATION,
	                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (g_object_class,
	                                 PROP_ANNOTATIONS_CONTEXT,
	                                 g_param_spec_object ("annots-context",
	                                                      NULL,
	                                                      NULL,
	                                                      PPS_TYPE_ANNOTATIONS_CONTEXT,
	                                                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (g_object_class,
	                                 PROP_DOCUMENT_MODEL,
	                                 g_param_spec_object ("model",
	                                                      NULL,
	                                                      NULL,
	                                                      PPS_TYPE_DOCUMENT_MODEL,
	                                                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

void
pps_overlay_annotation_grab_focus (PpsOverlayAnnotation *overlay, int x, int y)
{
	PPS_OVERLAY_ANNOTATION_GET_CLASS (overlay)->grab_focus (overlay, x, y);
}

PpsAnnotation *
pps_overlay_annotation_get_annotation (PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);

	return priv->annotation;
}

/* Overlay image */

/* The two following functions are copied from gdkcairoprivate.h and gdktexture.c */

static GdkMemoryFormat
gdk_cairo_format_to_memory_format (cairo_format_t format)
{
	switch (format) {
	case CAIRO_FORMAT_ARGB32:
		return GDK_MEMORY_DEFAULT;

	case CAIRO_FORMAT_RGB24:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
		return GDK_MEMORY_B8G8R8X8;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
		return GDK_MEMORY_X8R8G8B8;
#else
#error "Unknown byte order for Cairo format"
#endif
	case CAIRO_FORMAT_A8:
		return GDK_MEMORY_A8;
	case CAIRO_FORMAT_RGB96F:
		return GDK_MEMORY_R32G32B32_FLOAT;
	case CAIRO_FORMAT_RGBA128F:
		return GDK_MEMORY_R32G32B32A32_FLOAT_PREMULTIPLIED;

	case CAIRO_FORMAT_RGB16_565:
	case CAIRO_FORMAT_RGB30:
	case CAIRO_FORMAT_INVALID:
	case CAIRO_FORMAT_A1:
	default:
		g_assert_not_reached ();
		return GDK_MEMORY_DEFAULT;
	}
}

static GdkTexture *
gdk_texture_new_for_surface (cairo_surface_t *surface)
{
	GdkTexture *texture;
	GBytes *bytes;

	g_return_val_if_fail (cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_IMAGE, NULL);
	g_return_val_if_fail (cairo_image_surface_get_width (surface) > 0, NULL);
	g_return_val_if_fail (cairo_image_surface_get_height (surface) > 0, NULL);

	bytes = g_bytes_new_with_free_func (cairo_image_surface_get_data (surface),
	                                    cairo_image_surface_get_height (surface) * cairo_image_surface_get_stride (surface),
	                                    (GDestroyNotify) cairo_surface_destroy,
	                                    cairo_surface_reference (surface));

	texture = gdk_memory_texture_new (cairo_image_surface_get_width (surface),
	                                  cairo_image_surface_get_height (surface),
	                                  gdk_cairo_format_to_memory_format (cairo_image_surface_get_format (surface)),
	                                  bytes,
	                                  cairo_image_surface_get_stride (surface));

	g_bytes_unref (bytes);

	return texture;
}

static GObject *
pps_overlay_annotation_image_constructor (GType type,
                                          guint n_construct_properties,
                                          GObjectConstructParam *construct_params)
{
	GObject *object;
	PpsOverlayAnnotationPrivate *priv_overlay;
	PpsOverlayAnnotationImagePrivate *priv_image;
	PpsOverlayAnnotationImage *ov_image;
	cairo_surface_t *surface;
	GdkTexture *texture;

	object = G_OBJECT_CLASS (pps_overlay_annotation_image_parent_class)
	             ->constructor (type, n_construct_properties, construct_params);

	priv_overlay = OVERLAY_GET_PRIVATE (object);

	ov_image = PPS_OVERLAY_ANNOTATION_IMAGE (object);
	priv_image = IMAGE_GET_PRIVATE (ov_image);

	surface = pps_annotation_stamp_get_surface (PPS_ANNOTATION_STAMP (priv_overlay->annotation));

	if (surface) {
		texture = gdk_texture_new_for_surface (surface);
		gtk_picture_set_paintable (GTK_PICTURE (priv_image->image), GDK_PAINTABLE (texture));
	} else {
		g_debug ("Annotation not editable, could not retrieve the texture.");
		gtk_widget_set_sensitive (GTK_WIDGET (object), FALSE);
	}

	return object;
}

static void
pps_overlay_annotation_image_init (PpsOverlayAnnotationImage *ov_image)
{
	PpsOverlayAnnotationImagePrivate *priv = IMAGE_GET_PRIVATE (ov_image);
	PpsOverlayAnnotationPrivate *priv_overlay = OVERLAY_GET_PRIVATE (ov_image);
	priv->image = GTK_PICTURE (gtk_picture_new ());
	gtk_widget_set_focusable (GTK_WIDGET (ov_image), TRUE);
	gtk_picture_set_content_fit (priv->image, GTK_CONTENT_FIT_CONTAIN);

	gtk_widget_set_vexpand (GTK_WIDGET (priv->image), TRUE);
	gtk_widget_set_hexpand (GTK_WIDGET (priv->image), TRUE);

	gtk_widget_add_css_class (GTK_WIDGET (priv->image), "overlay-annot-content");

	gtk_box_append (priv_overlay->box, GTK_WIDGET (priv->image));
}

static void
pps_overlay_annotation_image_grab_focus (PpsOverlayAnnotation *overlay, int x, int y)
{
	gtk_widget_grab_focus (GTK_WIDGET (overlay));
}

static void
pps_overlay_annotation_image_class_init (PpsOverlayAnnotationImageClass *klass)
{
	GObjectClass *g_object_class = G_OBJECT_CLASS (klass);
	PpsOverlayAnnotationClass *ov = PPS_OVERLAY_ANNOTATION_CLASS (klass);

	g_object_class->constructor = pps_overlay_annotation_image_constructor;

	ov->drag_only_on_border = FALSE;
	ov->resize_handle = TRUE;
	ov->grab_focus = pps_overlay_annotation_image_grab_focus;
}

GtkWidget *
pps_overlay_annotation_image_new (PpsAnnotation *annot,
                                  PpsAnnotationsContext *annots_context,
                                  PpsDocumentModel *model)
{
	GtkWidget *overlay;

	g_return_val_if_fail (PPS_IS_ANNOTATION_STAMP (annot), NULL);

	if (!PPS_IS_ANNOTATION_STAMP (annot)) {
		return NULL;
	}

	overlay = g_object_new (PPS_TYPE_OVERLAY_ANNOTATION_IMAGE,
	                        "annotation",
	                        annot,
	                        "annots-context",
	                        annots_context,
	                        "model",
	                        model,
	                        NULL);
	return overlay;
}

/* Overlay Entry */

static void
pps_overlay_annotation_entry_resize (PpsAnnotation *annot, GParamSpec *pspec, PpsOverlayAnnotationEntry *entry)
{
	PangoContext *ctx = gtk_widget_get_pango_context (GTK_WIDGET (entry));

	pps_annotation_free_text_auto_resize (PPS_ANNOTATION_FREE_TEXT (annot), ctx);

	gtk_widget_queue_resize (GTK_WIDGET (entry));
}

static void
pps_overlay_annotation_entry_text_changed (GtkTextBuffer *buffer, PpsOverlayAnnotationEntry *entry)
{
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (entry);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	PpsOverlayAnnotationEntryPrivate *priv_entry = ENTRY_GET_PRIVATE (entry);
	GtkTextIter start, end;
	g_autofree gchar *text;

	gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &start, &end);
	text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buffer), &start, &end, FALSE);

	if (priv_entry->ignore_content_changed) {
		return;
	}

	priv_entry->ignore_content_changed = TRUE;

	if (pps_annotation_set_contents (priv->annotation, text)) {
		pps_overlay_annotation_entry_resize (priv->annotation, NULL, entry);
	}
	priv_entry->ignore_content_changed = FALSE;
}

static gboolean
remove_annot (PpsOverlayAnnotationEntry *entry)
{

	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (entry);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	pps_annotations_context_remove_annotation (priv->annots_context, priv->annotation);
	return FALSE;
}

static void
pps_overlay_annotation_entry_focus_out (GtkTextView *text_view, PpsOverlayAnnotationEntry *entry)
{
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (entry);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	PpsOverlayAnnotationEntryPrivate *priv_entry = ENTRY_GET_PRIVATE (entry);

	pps_document_model_set_annotation_editing_state (priv->model, priv_entry->previous_editing_state);

	if (!priv->annot_removed) {
		if (!g_strcmp0 (pps_annotation_get_contents (PPS_ANNOTATION (priv->annotation)), "")) {
			priv->annot_removed = TRUE;
			// remove at next tick because we are in the middle of processing
			// a focus event and removing the annotation now may result in the widget being
			// destroyed
			g_idle_add_once ((GSourceOnceFunc) remove_annot, entry);
		}
	}
}

static void
pps_overlay_annotation_entry_color_changed (PpsAnnotation *annot, GParamSpec *pspec, PpsOverlayAnnotationEntry *entry)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (PPS_OVERLAY_ANNOTATION (entry));
	PpsOverlayAnnotationEntryPrivate *priv_entry = ENTRY_GET_PRIVATE (entry);

	if (gtk_widget_is_focus (GTK_WIDGET (priv_entry->text_view))) {
		PpsAnnotationModel *annot_model = pps_document_model_get_annotation_model (priv->model);
		GdkRGBA *color = pps_annotation_model_get_text_color (annot_model);
		pps_annotation_free_text_set_font_rgba (PPS_ANNOTATION_FREE_TEXT (priv->annotation), color);
	}
}

static void
pps_overlay_annotation_entry_content_changed (PpsAnnotation *annot, GParamSpec *pspec, PpsOverlayAnnotationEntry *entry)
{
	GtkTextBuffer *buffer;
	const gchar *txt;
	PpsOverlayAnnotationEntryPrivate *priv;

	priv = ENTRY_GET_PRIVATE (entry);

	if (priv->ignore_content_changed) {
		return;
	}
	priv->ignore_content_changed = TRUE;
	buffer = gtk_text_view_get_buffer (priv->text_view);
	txt = pps_annotation_get_contents (annot);

	if (txt)
		gtk_text_buffer_set_text (buffer, txt, -1);
	priv->ignore_content_changed = FALSE;
}

static void
pps_overlay_annotation_entry_font_changed (PpsAnnotation *annot, GParamSpec *pspec, PpsOverlayAnnotationEntry *entry)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (PPS_OVERLAY_ANNOTATION (entry));
	PpsOverlayAnnotationEntryPrivate *priv_entry = ENTRY_GET_PRIVATE (entry);

	if (gtk_widget_is_focus (GTK_WIDGET (priv_entry->text_view))) {
		PpsAnnotationModel *annot_model = pps_document_model_get_annotation_model (priv->model);
		PangoFontDescription *font_desc = pps_annotation_model_get_font_desc (annot_model);
		pps_annotation_free_text_set_font_description (PPS_ANNOTATION_FREE_TEXT (priv->annotation), font_desc);
	}
}

static void
pps_overlay_annotation_entry_focus_in (GtkTextView *text_view, PpsOverlayAnnotationEntry *entry)
{
	PpsOverlayAnnotation *overlay = PPS_OVERLAY_ANNOTATION (entry);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	PpsOverlayAnnotationEntryPrivate *priv_entry = ENTRY_GET_PRIVATE (entry);
	PpsAnnotationModel *annot_model = pps_document_model_get_annotation_model (priv->model);
	PpsAnnotationEditingState state = pps_document_model_get_annotation_editing_state (priv->model);
	g_autoptr (GdkRGBA) color = pps_annotation_free_text_get_font_rgba (PPS_ANNOTATION_FREE_TEXT (priv->annotation));

	priv_entry->previous_editing_state = state;
	pps_document_model_set_annotation_editing_state (priv->model, state | PPS_ANNOTATION_EDITING_STATE_TEXT);
	pps_annotation_model_set_text_color (annot_model, color);
}

static gchar *
pps_overlay_annotation_entry_style (PpsOverlayAnnotation *overlay)
{
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (overlay);
	PpsAnnotation *annot = priv->annotation;
	PpsAnnotationFreeText *annot_ft = PPS_ANNOTATION_FREE_TEXT (annot);
	gdouble scale, border_width;
	PangoFontDescription *font;
	gdouble font_size;
	g_autoptr (GdkRGBA) font_rgba;
	g_autofree gchar *font_size_str, *font_rgba_str, *font_desc_str;

	font = pps_annotation_free_text_get_font_description (annot_ft);
	font_size = (double) pango_font_description_get_size (font) / PANGO_SCALE;
	font_size_str = g_malloc (G_ASCII_DTOSTR_BUF_SIZE);

	font_rgba = pps_annotation_free_text_get_font_rgba (annot_ft);
	font_rgba_str = gdk_rgba_to_string (font_rgba);

	font_desc_str = dzl_pango_font_description_to_css (font);

	scale = pps_document_model_get_scale (priv->model);

	// Convert the float to a string with '.' as the decimal separator
	g_ascii_dtostr (font_size_str, G_ASCII_DTOSTR_BUF_SIZE, font_size * scale);
	border_width = pps_annotation_get_border_width (annot);

	return g_strdup_printf (
	    " .annot%d .overlay-annot-content-overlay {"
	    "padding:%dpx;"
	    "padding-top: %dpx;"
	    "margin: 0;"
	    "outline: 0;"
	    "line-height: 1em;"
	    "color: %s;"
	    "%s"
	    "font-size: %spx;"
	    " }",
	    priv->id,
	    (int) (2 * border_width * scale),
	    // FIXME: this padding-top follows a thumbs rule. GTK and poppler do not
	    // align text the same way.
	    // Empirically, a 12% top displacement gives good results with common fonts.
	    (int) (2 * border_width * scale + font_size * 0.17 * scale),
	    font_rgba_str,
	    font_desc_str,
	    font_size_str);
}

static void
pps_overlay_annotation_entry_init (PpsOverlayAnnotationEntry *entry)
{
	PpsOverlayAnnotationEntryPrivate *priv = ENTRY_GET_PRIVATE (entry);
	PpsOverlayAnnotationPrivate *priv_overlay = OVERLAY_GET_PRIVATE (entry);
	GtkEventController *focus;
	GtkWidget *widget = gtk_text_view_new ();

	priv->text_view = GTK_TEXT_VIEW (widget);

	gtk_widget_add_css_class (widget, "overlay-annot-content");
	gtk_widget_add_css_class (widget, "overlay-annot-content-overlay");
	gtk_widget_set_vexpand (widget, TRUE);
	gtk_widget_set_hexpand (widget, TRUE);

	gtk_box_append (priv_overlay->box, widget);

	focus = gtk_event_controller_focus_new ();
	gtk_widget_add_controller (GTK_WIDGET (priv->text_view), focus);
	g_signal_connect (focus, "enter", G_CALLBACK (pps_overlay_annotation_entry_focus_in), entry);
	g_signal_connect (focus, "leave", G_CALLBACK (pps_overlay_annotation_entry_focus_out), entry);
}

static GObject *
pps_overlay_annotation_entry_constructor (GType type,
                                          guint n_construct_properties,
                                          GObjectConstructParam *construct_params)
{
	GObject *object;
	PpsOverlayAnnotationEntry *entry;
	PpsAnnotation *annot;
	GtkTextBuffer *buffer;
	const gchar *txt;
	PpsOverlayAnnotationPrivate *priv_overlay;
	PpsOverlayAnnotationEntryPrivate *priv;

	object = G_OBJECT_CLASS (pps_overlay_annotation_entry_parent_class)
	             ->constructor (type, n_construct_properties, construct_params);
	entry = PPS_OVERLAY_ANNOTATION_ENTRY (object);
	priv_overlay = OVERLAY_GET_PRIVATE (object);
	priv = ENTRY_GET_PRIVATE (entry);
	annot = priv_overlay->annotation;

	g_signal_connect (annot,
	                  "notify::rgba",
	                  G_CALLBACK (pps_overlay_annotation_annot_prop_changed),
	                  entry);
	g_signal_connect (annot,
	                  "notify::contents",
	                  G_CALLBACK (pps_overlay_annotation_entry_content_changed),
	                  entry);
	g_signal_connect (annot,
	                  "notify::font-rgba",
	                  G_CALLBACK (pps_overlay_annotation_annot_prop_changed),
	                  entry);
	g_signal_connect (annot,
	                  "notify::font-desc",
	                  G_CALLBACK (pps_overlay_annotation_annot_prop_changed),
	                  entry);
	g_signal_connect (annot,
	                  "notify::font-desc",
	                  G_CALLBACK (pps_overlay_annotation_entry_resize),
	                  entry);
	g_signal_connect (pps_document_model_get_annotation_model (priv_overlay->model), "notify::text-color",
	                  G_CALLBACK (pps_overlay_annotation_entry_color_changed),
	                  entry);
	g_signal_connect (pps_document_model_get_annotation_model (priv_overlay->model), "notify::text-font",
	                  G_CALLBACK (pps_overlay_annotation_entry_font_changed),
	                  entry);

	buffer = gtk_text_view_get_buffer (priv->text_view);

	txt = pps_annotation_get_contents (annot);
	if (txt) {
		gtk_text_buffer_set_text (buffer, txt, -1);
	}

	g_signal_connect (buffer, "changed", G_CALLBACK (pps_overlay_annotation_entry_text_changed), entry);

	return object;
}

static void
pps_overlay_annotation_entry_dispose (GObject *object)
{
	PpsOverlayAnnotationEntry *entry = PPS_OVERLAY_ANNOTATION_ENTRY (object);
	PpsOverlayAnnotationPrivate *priv = OVERLAY_GET_PRIVATE (object);

	g_signal_handlers_disconnect_by_func (
	    priv->annotation, G_CALLBACK (pps_overlay_annotation_entry_resize), entry);
	g_signal_handlers_disconnect_by_func (
	    pps_document_model_get_annotation_model (priv->model), G_CALLBACK (pps_overlay_annotation_entry_color_changed), entry);

	G_OBJECT_CLASS (pps_overlay_annotation_entry_parent_class)->dispose (object);
}

typedef struct {
	PpsOverlayAnnotationEntry *entry;
	int x;
	int y;
} AnnotEntryGrabFocus;

static void
pps_overlay_annotation_entry_grab_focus_idle (AnnotEntryGrabFocus *data)
{
	PpsOverlayAnnotationEntryPrivate *priv = ENTRY_GET_PRIVATE (data->entry);
	PpsOverlayAnnotationPrivate *priv_overlay = OVERLAY_GET_PRIVATE (data->entry);
	GtkTextBuffer *buffer;
	int bufx, bufy;
	GtkTextIter at_cursor;
	gdouble scale = pps_document_model_get_scale (priv_overlay->model);

	gdouble view_x = data->x * scale;
	gdouble view_y = data->y * scale;

	if (view_x >= 0. && view_y >= 0.) {
		gtk_text_view_window_to_buffer_coords (priv->text_view, GTK_TEXT_WINDOW_WIDGET, view_x, view_y, &bufx, &bufy);

		buffer = gtk_text_view_get_buffer (priv->text_view);

		// FIXME: This sometimes fail for no apparent reason
		if (!gtk_text_view_get_iter_at_location (priv->text_view, &at_cursor, bufx, bufy)) {
			gtk_text_iter_set_offset (&at_cursor, 0);
		}
		gtk_text_buffer_place_cursor (buffer, &at_cursor);
	}
	gtk_text_view_reset_cursor_blink (priv->text_view);
	g_free (data);
}

static void
pps_overlay_annotation_entry_grab_focus (PpsOverlayAnnotation *overlay, int x, int y)
{

	PpsOverlayAnnotationEntryPrivate *priv = ENTRY_GET_PRIVATE (PPS_OVERLAY_ANNOTATION_ENTRY (overlay));
	AnnotEntryGrabFocus *data = g_malloc0 (sizeof (AnnotEntryGrabFocus));

	gtk_widget_grab_focus (GTK_WIDGET (priv->text_view));

	// The cursor needs to be positioned at the next tick, may be necessary for style to be applied and positions consistent
	data->entry = PPS_OVERLAY_ANNOTATION_ENTRY (overlay);
	data->x = x;
	data->y = y;

	g_idle_add_once ((GSourceOnceFunc) pps_overlay_annotation_entry_grab_focus_idle, (gpointer) data);
}

static void
pps_overlay_annotation_entry_class_init (PpsOverlayAnnotationEntryClass *klass)
{
	GObjectClass *g_object_class = G_OBJECT_CLASS (klass);
	PpsOverlayAnnotationClass *ov_class = PPS_OVERLAY_ANNOTATION_CLASS (klass);

	g_object_class->constructor = pps_overlay_annotation_entry_constructor;
	g_object_class->dispose = pps_overlay_annotation_entry_dispose;

	ov_class->drag_only_on_border = TRUE;
	ov_class->resize_handle = TRUE;
	ov_class->grab_focus = pps_overlay_annotation_entry_grab_focus;
	ov_class->style = pps_overlay_annotation_entry_style;
}

GtkWidget *
pps_overlay_annotation_entry_new (PpsAnnotation *annot,
                                  PpsAnnotationsContext *annots_context,
                                  PpsDocumentModel *model)
{
	GtkWidget *entry;

	g_return_val_if_fail (PPS_IS_ANNOTATION_FREE_TEXT (annot), NULL);

	entry = g_object_new (PPS_TYPE_OVERLAY_ANNOTATION_ENTRY,
	                      "annotation",
	                      annot,
	                      "annots-context",
	                      annots_context,
	                      "model",
	                      model,
	                      NULL);
	return entry;
}
