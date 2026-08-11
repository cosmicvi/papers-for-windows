// SPDX-License-Identifier: GPL-2.0-or-later
/* pps-view-page.c
 *  this file is part of papers, a gnome document viewer
 *
 * Copyright (C) 2025 Markus Göllnitz <camelcasenick@bewares.it>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pps-annotation-layer.h"
#include "pps-view-page.h"

#include <string.h>

#include "pps-annotation-layer-ink.h"
#include "pps-annotation-layer-objects.h"
#include "pps-colors.h"
#include "pps-debug.h"
#include "pps-overlay.h"
#include "pps-view-private.h"
#include "pps-view.h"
#include <gdk/gdk.h>
#include <gsk/gskenums.h> // For GSK_RECT_SNAP_ROUND
#include <gsk/gskrender.h> // For gtk_snapshot_set_snap
#include <gsk/gsk.h>

#define PPS_STYLE_CLASS_DOCUMENT_PAGE "document-page"
#define PPS_STYLE_CLASS_INVERTED "inverted"

static void pps_view_page_accessible_text_init (GtkAccessibleTextInterface *iface);

typedef struct
{
	PpsPoint cursor_offset;
	PpsAnnotation *annot;
} MovingAnnotInfo;

enum {
	LAYER_INK,
	LAYER_OBJECTS,
	LAYER_COUNT
};

typedef struct
{
	gint index;

	PpsDocumentModel *model;
	PpsAnnotationsContext *annots_context;
	PpsSearchContext *search_context;

	PpsPageCache *page_cache;
	PpsPixbufCache *pixbuf_cache;

	gboolean had_search_results;
	MovingAnnotInfo moving_annot_info;

	gint cursor_page;
	gint cursor_offset;
	PpsAnnotationLayer *layers[LAYER_COUNT];
} PpsViewPagePrivate;

G_DEFINE_TYPE_WITH_CODE (PpsViewPage, pps_view_page, GTK_TYPE_WIDGET, G_ADD_PRIVATE (PpsViewPage) G_IMPLEMENT_INTERFACE (GTK_TYPE_ACCESSIBLE_TEXT, pps_view_page_accessible_text_init))

#define GET_PRIVATE(o) pps_view_page_get_instance_private (o)

enum {
	PROP_PAGE = 1,
	PROP_LAST
};
static GParamSpec *properties[PROP_LAST];

static void
pps_view_page_set_property (GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	PpsViewPage *page = PPS_VIEW_PAGE (object);
	switch (prop_id) {
	case PROP_PAGE:
		pps_view_page_set_page (page, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pps_view_page_get_property (GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	PpsViewPage *page = PPS_VIEW_PAGE (object);
	switch (prop_id) {
	case PROP_PAGE:
		g_value_set_int (value, pps_view_page_get_page (page));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pps_view_page_measure (GtkWidget *widget,
                       GtkOrientation orientation,
                       int for_size,
                       int *minimum,
                       int *natural,
                       int *minimum_baseline,
                       int *natural_baseline)
{
	PpsViewPage *page = PPS_VIEW_PAGE (widget);
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	gint width = 0, height = 0;
	gdouble width_raw, height_raw;
	PpsDocument *document;
	gdouble scale;
	gint rotation;

	if (priv->model != NULL && pps_document_model_get_document (priv->model) != NULL && priv->index >= 0) {
		document = pps_document_model_get_document (priv->model);
		scale = pps_document_model_get_scale (priv->model);
		rotation = pps_document_model_get_rotation (priv->model);

		pps_document_get_page_size (document, priv->index, &width_raw, &height_raw);

		width = (gint) (((rotation == 0 || rotation == 180) ? width_raw : height_raw) * scale + 0.5);
		height = (gint) (((rotation == 0 || rotation == 180) ? height_raw : width_raw) * scale + 0.5);
	}

	if (orientation == GTK_ORIENTATION_HORIZONTAL) {
		*minimum = 0;
		*natural = width;
	} else {
		*minimum = 0;
		*natural = height;
	}
}

static void
doc_rect_to_view_rect (PpsViewPage *page,
                       const PpsRectangle *doc_rect,
                       GdkRectangle *view_rect)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	gdouble scale = pps_document_model_get_scale (priv->model);
	gdouble page_width = gtk_widget_get_width (GTK_WIDGET (page));
	gdouble page_height = gtk_widget_get_height (GTK_WIDGET (page));
	gdouble x, y, width, height;

	switch (pps_document_model_get_rotation (priv->model)) {
	case 0:
		x = doc_rect->x1 * scale;
		y = doc_rect->y1 * scale;
		width = (doc_rect->x2 - doc_rect->x1) * scale;
		height = (doc_rect->y2 - doc_rect->y1) * scale;

		break;
	case 90: {
		x = page_width - doc_rect->y2 * scale;
		y = doc_rect->x1 * scale;
		width = (doc_rect->y2 - doc_rect->y1) * scale;
		height = (doc_rect->x2 - doc_rect->x1) * scale;
	} break;
	case 180: {
		x = page_width - doc_rect->x2 * scale;
		y = page_height - doc_rect->y2 * scale;
		width = (doc_rect->x2 - doc_rect->x1) * scale;
		height = (doc_rect->y2 - doc_rect->y1) * scale;
	} break;
	case 270: {
		x = doc_rect->y1 * scale;
		y = page_height - doc_rect->x2 * scale;
		width = (doc_rect->y2 - doc_rect->y1) * scale;
		height = (doc_rect->x2 - doc_rect->x1) * scale;
	} break;
	default:
		g_assert_not_reached ();
	}

	view_rect->x = (gint) (x + 0.5);
	view_rect->y = (gint) (y + 0.5);
	view_rect->width = (gint) (width + 0.5);
	view_rect->height = (gint) (height + 0.5);
}

static PpsPoint
view_point_to_doc_point (PpsViewPage *page, double x, double y)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	gdouble scale = pps_document_model_get_scale (priv->model);
	gdouble page_width = gtk_widget_get_width (GTK_WIDGET (page));
	gdouble page_height = gtk_widget_get_height (GTK_WIDGET (page));
	PpsPoint point;

	switch (pps_document_model_get_rotation (priv->model)) {
	case 0:
		point.x = x / scale;
		point.y = y / scale;
		break;
	case 90: {
		point.x = (page_width - x) * scale;
		point.y = y / scale;
	} break;
	case 180: {
		point.x = (page_width - x) / scale;
		point.y = (page_height - y) / scale;
	} break;
	case 270: {
		point.x = x / scale;
		point.y = (page_height - y) / scale;
	} break;
	default:
		g_assert_not_reached ();
	}

	return point;
}

static void
stroke_view_rect (GtkSnapshot *snapshot,
                  GdkRectangle *clip,
                  GdkRGBA *color,
                  GdkRectangle *view_rect)
{
	GdkRectangle intersect;
	GdkRGBA border_color[4] = { *color, *color, *color, *color };
	float border_width[4] = { 1, 1, 1, 1 };

	if (gdk_rectangle_intersect (view_rect, clip, &intersect)) {
		gtk_snapshot_append_border (snapshot,
		                            &GSK_ROUNDED_RECT_INIT (intersect.x, intersect.y,
		                                                    intersect.width, intersect.height),
		                            border_width, border_color);
	}
}

static void
stroke_doc_rect (GtkSnapshot *snapshot,
                 PpsViewPage *page,
                 GdkRGBA *color,
                 GdkRectangle *clip,
                 PpsRectangle *doc_rect)
{
	GdkRectangle view_rect;

	doc_rect_to_view_rect (page, doc_rect, &view_rect);
	stroke_view_rect (snapshot, clip, color, &view_rect);
}

static void
show_chars_border (GtkSnapshot *snapshot,
                   PpsViewPage *page,
                   GdkRectangle *clip)
{
	PpsRectangle *areas = NULL;
	guint n_areas = 0;
	guint i;
	GdkRGBA color = { 1, 0, 0, 1 };
	PpsViewPagePrivate *priv = GET_PRIVATE (page);

	pps_page_cache_get_text_layout (priv->page_cache, priv->index, &areas, &n_areas);
	if (!areas)
		return;

	for (i = 0; i < n_areas; i++) {
		PpsRectangle *doc_rect = areas + i;

		stroke_doc_rect (snapshot, page, &color, clip, doc_rect);
	}
}

static void
show_mapping_list_border (GtkSnapshot *snapshot,
                          PpsViewPage *page,
                          GdkRGBA *color,
                          GdkRectangle *clip,
                          PpsMappingList *mapping_list)
{
	GList *l;

	for (l = pps_mapping_list_get_list (mapping_list); l; l = g_list_next (l)) {
		PpsMapping *mapping = (PpsMapping *) l->data;

		stroke_doc_rect (snapshot, page, color, clip, &mapping->area);
	}
}

static void
show_links_border (GtkSnapshot *snapshot,
                   PpsViewPage *page,
                   GdkRectangle *clip)
{
	GdkRGBA color = { 0, 0, 1, 1 };
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	show_mapping_list_border (snapshot, page, &color, clip,
	                          pps_page_cache_get_link_mapping (priv->page_cache, priv->index));
}

static void
show_forms_border (GtkSnapshot *snapshot,
                   PpsViewPage *page,
                   GdkRectangle *clip)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	GdkRGBA color = { 0, 1, 0, 1 };
	show_mapping_list_border (snapshot, page, &color, clip,
	                          pps_page_cache_get_form_field_mapping (priv->page_cache, priv->index));
}

static void
show_annots_border (GtkSnapshot *snapshot,
                    PpsViewPage *page,
                    GdkRectangle *clip)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	GdkRGBA color = { 0, 1, 1, 1 };
	GListModel *model =
	    pps_annotations_context_get_annots_model (priv->annots_context);

	// To make this generic, in the future we should have an interface
	// to get areas, instead of the Mapping. See #382
	for (gint i = 0; i < g_list_model_get_n_items (model); i++) {
		g_autoptr (PpsAnnotation) annot = g_list_model_get_item (model, i);
		PpsRectangle area;
		if (priv->index != pps_annotation_get_page_index (annot))
			continue;
		pps_annotation_get_area (annot, &area);
		stroke_doc_rect (snapshot, page, &color, clip, &area);
	}
}

static void
show_images_border (GtkSnapshot *snapshot,
                    PpsViewPage *page,
                    GdkRectangle *clip)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	GdkRGBA color = { 1, 0, 1, 1 };
	show_mapping_list_border (snapshot, page, &color, clip,
	                          pps_page_cache_get_image_mapping (priv->page_cache, priv->index));
}

static void
show_media_border (GtkSnapshot *snapshot,
                   PpsViewPage *page,
                   GdkRectangle *clip)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	GdkRGBA color = { 1, 1, 0, 1 };
	show_mapping_list_border (snapshot, page, &color, clip,
	                          pps_page_cache_get_media_mapping (priv->page_cache, priv->index));
}

static void
show_selections_border (GtkSnapshot *snapshot,
                        PpsViewPage *page,
                        GdkRectangle *clip)
{
	cairo_region_t *region;
	guint i, n_rects;
	GdkRGBA color = { 0.75, 0.50, 0.25, 1 };
	PpsViewPagePrivate *priv = GET_PRIVATE (page);

	region = pps_page_cache_get_text_mapping (priv->page_cache, priv->index);
	if (!region)
		return;

	region = cairo_region_copy (region);
	n_rects = cairo_region_num_rectangles (region);
	for (i = 0; i < n_rects; i++) {
		GdkRectangle doc_rect_int;
		PpsRectangle doc_rect_float;

		cairo_region_get_rectangle (region, i, &doc_rect_int);

		/* Convert the doc rect to a PpsRectangle */
		doc_rect_float.x1 = doc_rect_int.x;
		doc_rect_float.y1 = doc_rect_int.y;
		doc_rect_float.x2 = doc_rect_int.x + doc_rect_int.width;
		doc_rect_float.y2 = doc_rect_int.y + doc_rect_int.height;

		stroke_doc_rect (snapshot, page, &color, clip, &doc_rect_float);
	}
	cairo_region_destroy (region);
}

static void
draw_debug_borders (GtkSnapshot *snapshot,
                    PpsViewPage *page,
                    GdkRectangle *clip)
{
	PpsDebugBorders borders = pps_debug_get_debug_borders ();

	if (borders & PPS_DEBUG_BORDER_CHARS)
		show_chars_border (snapshot, page, clip);
	if (borders & PPS_DEBUG_BORDER_LINKS)
		show_links_border (snapshot, page, clip);
	if (borders & PPS_DEBUG_BORDER_FORMS)
		show_forms_border (snapshot, page, clip);
	if (borders & PPS_DEBUG_BORDER_ANNOTS)
		show_annots_border (snapshot, page, clip);
	if (borders & PPS_DEBUG_BORDER_IMAGES)
		show_images_border (snapshot, page, clip);
	if (borders & PPS_DEBUG_BORDER_MEDIA)
		show_media_border (snapshot, page, clip);
	if (borders & PPS_DEBUG_BORDER_SELECTIONS)
		show_selections_border (snapshot, page, clip);
}

static void
draw_rect (GtkSnapshot *snapshot,
           const GdkRectangle *rect,
           const GdkRGBA *color)
{
	graphene_rect_t graphene_rect = GRAPHENE_RECT_INIT (rect->x,
	                                                    rect->y,
	                                                    rect->width,
	                                                    rect->height);

	gtk_snapshot_append_color (snapshot, color, &graphene_rect);
}

static void
draw_selection_region (GtkSnapshot *snapshot,
                       cairo_region_t *region,
                       GdkRGBA *color)
{
	cairo_rectangle_int_t box;
	gint n_boxes, i;

	n_boxes = cairo_region_num_rectangles (region);

	for (i = 0; i < n_boxes; i++) {
		cairo_region_get_rectangle (region, i, &box);
		draw_rect (snapshot, &box, color);
	}
}

static void
highlight_find_results (GtkSnapshot *snapshot,
                        PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	GtkSingleSelection *model = pps_search_context_get_result_model (priv->search_context);
	g_autoptr (GPtrArray) results = pps_search_context_get_results_on_page (priv->search_context, priv->index);
	PpsRectangle pps_rect;
	GdkRGBA color_selected, color_default;

	get_accent_color (&color_selected, NULL);
	color_selected.alpha *= 0.6;
	get_accent_color (&color_default, NULL);
	color_default.alpha *= 0.3;

	for (gint i = 0; i < results->len; i++) {
		PpsSearchResult *result = g_ptr_array_index (results, i);
		PpsFindRectangle *find_rect;
		GList *rectangles;
		GdkRectangle view_rectangle;
		GdkRGBA color = color_default;

		rectangles = pps_search_result_get_rectangle_list (result);
		find_rect = (PpsFindRectangle *) rectangles->data;
		pps_rect.x1 = find_rect->x1;
		pps_rect.x2 = find_rect->x2;
		pps_rect.y1 = find_rect->y1;
		pps_rect.y2 = find_rect->y2;

		if (result == gtk_single_selection_get_selected_item (model))
			color = color_selected;

		doc_rect_to_view_rect (page, &pps_rect, &view_rectangle);
		draw_rect (snapshot, &view_rectangle, &color);

		if (rectangles->next) {
			/* Draw now next result (which is second part of multi-line match) */
			find_rect = (PpsFindRectangle *) rectangles->next->data;
			pps_rect.x1 = find_rect->x1;
			pps_rect.x2 = find_rect->x2;
			pps_rect.y1 = find_rect->y1;
			pps_rect.y2 = find_rect->y2;
			doc_rect_to_view_rect (page, &pps_rect, &view_rectangle);
			draw_rect (snapshot, &view_rectangle, &color);
		}
	}
}

static void
draw_surface (GtkSnapshot *snapshot,
              GdkTexture *texture,
              const graphene_rect_t *area,
              gboolean inverted)
{
	gboolean snap_texture = gdk_texture_get_height (texture) == floor (area->size.height);
	gtk_snapshot_save (snapshot);

	if (inverted) {
		gtk_snapshot_push_blend (snapshot, GSK_BLEND_MODE_COLOR);
		gtk_snapshot_push_blend (snapshot, GSK_BLEND_MODE_DIFFERENCE);
		gtk_snapshot_append_color (snapshot, &(GdkRGBA) { 1., 1., 1., 1. }, area);
		gtk_snapshot_pop (snapshot);
		if (snap_texture) {
			gtk_snapshot_append_scaled_texture (snapshot, texture, GSK_SCALING_FILTER_NEAREST, area);
		} else {
			gtk_snapshot_append_texture (snapshot, texture, area);
		}
		gtk_snapshot_pop (snapshot);
		gtk_snapshot_pop (snapshot);
		if (snap_texture) {
			gtk_snapshot_append_scaled_texture (snapshot, texture, GSK_SCALING_FILTER_NEAREST, area);
		} else {
			gtk_snapshot_append_texture (snapshot, texture, area);
		}
		gtk_snapshot_pop (snapshot);
	} else {
		if (snap_texture) {
			gtk_snapshot_append_scaled_texture (snapshot, texture, GSK_SCALING_FILTER_NEAREST, area);
		} else {
			gtk_snapshot_append_texture (snapshot, texture, area);
		}
	}

	gtk_snapshot_restore (snapshot);
}

static void
pps_view_page_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	PpsViewPage *page = PPS_VIEW_PAGE (widget);
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	guint width = gtk_widget_get_width (widget);
	guint height = gtk_widget_get_height (widget);
	GdkTexture *page_texture = NULL, *selection_texture = NULL;
	GdkRectangle area_rect;
	graphene_rect_t area;
	cairo_region_t *region = NULL;
	gboolean inverted;
	gdouble scale;
	GtkNative *native = gtk_widget_get_native (widget);
	gdouble fractional_scale = gdk_surface_get_scale (gtk_native_get_surface (native));

	if (priv->model == NULL || priv->index < 0)
		return;

	inverted = pps_document_model_get_inverted_colors (priv->model);
	scale = pps_document_model_get_scale (priv->model);
	page_texture = pps_pixbuf_cache_get_texture (priv->pixbuf_cache, priv->index);

	if (!page_texture)
		return;

	area_rect.x = 0;
	area_rect.y = 0;
	area_rect.width = width;
	area_rect.height = height;

	if (priv->layers[LAYER_INK] && gtk_widget_get_visible (GTK_WIDGET (priv->layers[LAYER_INK]))) {
		gtk_snapshot_push_blend (snapshot, GSK_BLEND_MODE_MULTIPLY);
		pps_annotation_layer_ink_snapshot_below (PPS_ANNOTATION_LAYER_INK (priv->layers[LAYER_INK]), snapshot);
		gtk_snapshot_pop (snapshot);
	}

	area = GRAPHENE_RECT_INIT (0, 0,
	                           ceil (width * fractional_scale),
	                           ceil (height * fractional_scale));
	gtk_snapshot_save (snapshot);

#if GTK_CHECK_VERSION(4, 15, 1) /* TODO: bump this to 4.24.0 once it exists */
	/* Snap the texture to a physical pixel so it is not blurred */
	gtk_snapshot_set_snap (snapshot, GSK_RECT_SNAP_ROUND);
#else
	/* No snapping API; need to do it by hand:
	 * Compute residual sub-pixel offset of the widget origin in physical pixel space and shift the texture by that amount so it lands on the nearest physical (integer) pixel. */
	{
		/* HACK: this is a hack to avoid jitter on fractional scaling. It is not perfect, but it is better than nothing. */
		graphene_point_t origin;
		if (gtk_widget_compute_point (widget, GTK_WIDGET (native),
		                              &GRAPHENE_POINT_INIT_ZERO, &origin)) {
			gdouble origin_phys_x = origin.x * fractional_scale;
			gdouble origin_phys_y = origin.y * fractional_scale;
			area.origin.x = round (origin_phys_x) - origin_phys_x;
			area.origin.y = round (origin_phys_y) - origin_phys_y;
		}
	}
#endif
	gtk_snapshot_scale (snapshot, 1 / fractional_scale, 1 / fractional_scale);

	draw_surface (snapshot, page_texture, &area, inverted);

	/* Get the selection pixbuf iff we have something to draw */
	selection_texture = pps_pixbuf_cache_get_selection_texture (priv->pixbuf_cache,
	                                                            priv->index,
	                                                            scale);

	if (selection_texture) {
		draw_surface (snapshot, selection_texture, &area, false);
		// Restore fractional scaling
		gtk_snapshot_restore (snapshot);
	} else {
		// Restore fractional scaling
		gtk_snapshot_restore (snapshot);
		region = pps_pixbuf_cache_get_selection_region (priv->pixbuf_cache,
		                                                priv->index,
		                                                scale);
		if (region) {
			GdkRGBA color;

#ifdef HAVE_TRANSPARENT_SELECTION
			get_selection_color (GTK_WIDGET (page), &color);
#else
			get_accent_color (&color, NULL);
			color.alpha = 0.4;
#endif
			draw_selection_region (snapshot, region, &color);
		}
	}

	if (priv->search_context != NULL && pps_search_context_get_active (priv->search_context))
		highlight_find_results (snapshot, page);

	if (pps_debug_get_debug_borders ())
		draw_debug_borders (snapshot, page, &area_rect);

	GTK_WIDGET_CLASS (pps_view_page_parent_class)->snapshot (widget, snapshot);

	if (priv->layers[LAYER_INK] && gtk_widget_get_visible (GTK_WIDGET (priv->layers[LAYER_INK]))) {
		gtk_snapshot_pop (snapshot);
	}
}
