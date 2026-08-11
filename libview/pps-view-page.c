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

#if GTK_CHECK_VERSION(4, 23, 1) /* This version check is for a future GTK version, forcing the else branch for GTK 4.22.4 */
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

static void
inverted_changed_cb (PpsDocumentModel *model,
                     GParamSpec *pspec,
                     PpsViewPage *page)
{
	if (pps_document_model_get_inverted_colors (model)) {
		gtk_widget_add_css_class (GTK_WIDGET (page), PPS_STYLE_CLASS_INVERTED);
	} else {
		gtk_widget_remove_css_class (GTK_WIDGET (page), PPS_STYLE_CLASS_INVERTED);
	}
}

static void
display_annotation_layers (PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	PpsAnnotationEditingState state = pps_document_model_get_annotation_editing_state (priv->model);
	PpsAnnotationLayer *layer = NULL;

	if (!(state & PPS_ANNOTATION_EDITING_STATE_TEXT) && priv->layers[LAYER_OBJECTS]) {
		gtk_widget_set_visible (GTK_WIDGET (priv->layers[LAYER_OBJECTS]), FALSE);
	}

	if (state != PPS_ANNOTATION_EDITING_STATE_INK && priv->layers[LAYER_INK]) {
		gtk_widget_set_visible (GTK_WIDGET (priv->layers[LAYER_INK]), FALSE);
	}

	if (state & PPS_ANNOTATION_EDITING_STATE_INSERT_TEXT) {
		if (!priv->layers[LAYER_OBJECTS]) {
			priv->layers[LAYER_OBJECTS] = PPS_ANNOTATION_LAYER (pps_annotation_layer_objects_new (pps_document_model_get_document (priv->model), priv->model, priv->annots_context));
			gtk_widget_insert_after (GTK_WIDGET (priv->layers[LAYER_OBJECTS]), GTK_WIDGET (page), NULL);
		}
		layer = priv->layers[LAYER_OBJECTS];
	} else if (state == PPS_ANNOTATION_EDITING_STATE_INK) {
		if (!priv->layers[LAYER_INK]) {
			priv->layers[LAYER_INK] = PPS_ANNOTATION_LAYER (pps_annotation_layer_ink_new (pps_document_model_get_document (priv->model), priv->model, priv->annots_context));
			gtk_widget_insert_before (GTK_WIDGET (priv->layers[LAYER_INK]), GTK_WIDGET (page), NULL);
		}
		layer = priv->layers[LAYER_INK];
	}

	if (layer) {
		pps_annotation_layer_set_page (layer, priv->index);
		gtk_widget_set_visible (GTK_WIDGET (layer), TRUE);
	}
}

static void
job_finished_cb (PpsPixbufCache *pixbuf_cache,
                 int finished_page,
                 PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);

	if (finished_page == priv->index) {
		display_annotation_layers (page);
		gtk_widget_queue_draw (GTK_WIDGET (page));
	}
}

static void
search_results_changed_cb (PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	gboolean has_search_results = pps_search_context_has_results_on_page (priv->search_context, priv->index);

	/*
	 * If there are currently no search results shown (had_search_results) nor
	 * there are any to be shown in the next round (has_search_results), there
	 * is no reason to redraw. If either is true, we might have changes.
	 */
	if (has_search_results || priv->had_search_results)
		gtk_widget_queue_draw (GTK_WIDGET (page));

	priv->had_search_results = has_search_results;
}

static void
move_annot_to_point (PpsViewPage *page,
                     gdouble view_point_x,
                     gdouble view_point_y)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	PpsRectangle new_area, old_area;
	PpsPoint doc_point;
	double page_width, page_height;

	pps_annotation_get_area (priv->moving_annot_info.annot, &old_area);
	pps_document_get_page_size (document, priv->index, &page_width, &page_height);
	doc_point = view_point_to_doc_point (page, view_point_x, view_point_y);

	new_area.x1 = MAX (0, doc_point.x - priv->moving_annot_info.cursor_offset.x);
	new_area.y1 = MAX (0, doc_point.y - priv->moving_annot_info.cursor_offset.y);
	new_area.x2 = new_area.x1 + old_area.x2 - old_area.x1;
	new_area.y2 = new_area.y1 + old_area.y2 - old_area.y1;

	/* Prevent the annotation from being moved off the page */
	if (new_area.x2 > page_width) {
		new_area.x2 = page_width;
		new_area.x1 = page_width - old_area.x2 + old_area.x1;
	}
	if (new_area.y2 > page_height) {
		new_area.y2 = page_height;
		new_area.y1 = page_height - old_area.y2 + old_area.y1;
	}

	pps_annotation_set_area (priv->moving_annot_info.annot, &new_area);
}

static void
annotation_drag_update_cb (GtkGestureDrag *annotation_drag_gesture,
                           gdouble offset_x,
                           gdouble offset_y,
                           PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (annotation_drag_gesture));
	gdouble start_x, start_y, view_point_x, view_point_y;

	if (!priv->moving_annot_info.annot)
		g_assert_not_reached ();

	if (gtk_drag_check_threshold (GTK_WIDGET (page), 0, 0,
	                              offset_x, offset_y))
		gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture),
		                       GTK_EVENT_SEQUENCE_CLAIMED);

	if (gtk_gesture_get_sequence_state (GTK_GESTURE (annotation_drag_gesture), sequence) != GTK_EVENT_SEQUENCE_CLAIMED)
		return;

	gtk_gesture_drag_get_start_point (annotation_drag_gesture, &start_x, &start_y);

	view_point_x = start_x + offset_x;
	view_point_y = start_y + offset_y;
	move_annot_to_point (page, view_point_x, view_point_y);
}

static void
annotation_drag_begin_cb (GtkGestureDrag *annotation_drag_gesture,
                          gdouble x,
                          gdouble y,
                          PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	PpsAnnotation *annot;
	PpsDocumentPoint doc_point;
	PpsRectangle annot_area;

	if (!PPS_ANNOTATIONS_CONTEXT (priv->annots_context)) {
		gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture), GTK_EVENT_SEQUENCE_DENIED);
		return;
	}

	doc_point.page_index = priv->index;
	doc_point.point_on_page = view_point_to_doc_point (page, x, y);

	annot = pps_annotations_context_get_annot_at_doc_point (priv->annots_context,
	                                                        &doc_point);

	if (!PPS_IS_ANNOTATION_TEXT (annot)) {
		gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture),
		                       GTK_EVENT_SEQUENCE_DENIED);
		return;
	}

	if (pps_document_model_get_annotation_editing_state (priv->model) != PPS_ANNOTATION_EDITING_STATE_NONE) {
		gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture),
		                       GTK_EVENT_SEQUENCE_DENIED);
		return;
	}

	g_set_object (&(priv->moving_annot_info.annot), annot);

	pps_annotation_get_area (annot, &annot_area);
	/* Remember the offset of the cursor with respect to
	 * the annotation area in order to prevent the annotation from
	 * jumping under the cursor while moving it. */
	priv->moving_annot_info.cursor_offset.x = doc_point.point_on_page.x - annot_area.x1;
	priv->moving_annot_info.cursor_offset.y = doc_point.point_on_page.y - annot_area.y1;
}

static void
annotation_drag_end_cb (GtkGestureDrag *annotation_drag_gesture,
                        gdouble offset_x,
                        gdouble offset_y,
                        PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);

	g_clear_object (&(priv->moving_annot_info.annot));
}

static void
view_cursor_moved_cb (PpsView *view,
                      gint cursor_page,
                      gint cursor_offset,
                      PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	priv->cursor_page = cursor_page;
	priv->cursor_offset = cursor_offset;
	if (cursor_page == priv->index) {
		gtk_accessible_text_update_caret_position (GTK_ACCESSIBLE_TEXT (page));
	}
}

static void
view_selection_changed_cb (PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	gtk_widget_queue_draw (GTK_WIDGET (page));
	if (priv->cursor_page == priv->index) {
		gtk_accessible_text_update_selection_bound (GTK_ACCESSIBLE_TEXT (page));
	}
}

/* ATs expect to be able to identify sentence boundaries based on content. Valid,
 * content-based boundaries may be present at the end of a newline, for instance
 * at the end of a heading within a document. Thus being able to distinguish hard
 * returns from soft returns is necessary. However, the text we get from Poppler
 * for non-tagged PDFs has "\n" inserted at the end of each line resulting in a
 * broken accessibility implementation w.r.t. sentences.
 */
static gboolean
treat_as_soft_return (PpsViewPage *self,
                      PangoLogAttr *log_attrs,
                      gint offset)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	PpsRectangle *areas = NULL;
	guint n_areas = 0;
	gdouble line_spacing, this_line_height, next_word_width;
	PpsRectangle *this_line_start;
	PpsRectangle *this_line_end;
	PpsRectangle *next_line_start;
	PpsRectangle *next_line_end;
	PpsRectangle *next_word_end;
	gint prpps_offset, next_offset;

	if (!log_attrs[offset].is_white)
		return FALSE;

	pps_page_cache_get_text_layout (priv->page_cache, priv->index, &areas, &n_areas);
	if (n_areas <= offset + 1)
		return FALSE;

	prpps_offset = offset - 1;
	next_offset = offset + 1;

	/* In wrapped text, the character at the start of the next line starts a word.
	 * Examples where this condition might fail include bullets and images. But it
	 * also includes things like "(", so also check the next character.
	 */
	if (!log_attrs[next_offset].is_word_start &&
	    (next_offset + 1 >= n_areas || !log_attrs[next_offset + 1].is_word_start))
		return FALSE;

	/* In wrapped text, the chars on either side of the newline have very similar heights.
	 * Examples where this condition might fail include a newline at the end of a heading,
	 * and a newline at the end of a paragraph that is followed by a heading.
	 */
	this_line_end = areas + prpps_offset;
	next_line_start = areas + next_offset;

	this_line_height = this_line_end->y2 - this_line_end->y1;
	if (ABS (this_line_height - (next_line_start->y2 - next_line_start->y1)) > 0.25)
		return FALSE;

	/* If there is significant white space between this line and the next, odds are this
	 * is not a soft return in wrapped text. Lines within a typical paragraph are at most
	 * double-spaced. If the spacing is more than that, assume a hard return is present.
	 */
	line_spacing = next_line_start->y1 - this_line_end->y2;
	if (line_spacing - this_line_height > 1)
		return FALSE;

	/* Lines within a typical paragraph have *reasonably* similar x1 coordinates. But
	 * we cannot count on them being nearly identical. Examples where indentation can
	 * be present in wrapped text include indenting the first line of the paragraph,
	 * and hanging indents (e.g. in the works cited within an academic paper). So we'll
	 * be somewhat tolerant here.
	 */
	while (prpps_offset > 0 && !log_attrs[prpps_offset].is_mandatory_break)
		prpps_offset--;
	this_line_start = areas + prpps_offset;
	if (ABS (this_line_start->x1 - next_line_start->x1) > 20)
		return FALSE;

	/* Ditto for x2, but this line might be short due to a wide word on the next line. */
	while (next_offset < n_areas && !log_attrs[next_offset].is_word_end)
		next_offset++;
	next_word_end = areas + next_offset;
	next_word_width = next_word_end->x2 - next_line_start->x1;

	while (next_offset < n_areas && !log_attrs[next_offset + 1].is_mandatory_break)
		next_offset++;
	next_line_end = areas + next_offset;
	if (next_line_end->x2 - (this_line_end->x2 + next_word_width) > 20)
		return FALSE;

	return TRUE;
}

static gchar *
get_substring (GtkAccessibleText *text,
               unsigned int start,
               unsigned int end)
{
	PpsViewPage *self = PPS_VIEW_PAGE (text);
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	gchar *substring, *normalized;
	const gchar *page_text;

	page_text = pps_page_cache_get_text (priv->page_cache, priv->index);
	if (!page_text)
		return NULL;
	if (end > g_utf8_strlen (page_text, -1))
		end = strlen (page_text);
	start = CLAMP (start, 0, end);

	substring = g_utf8_substring (page_text, start, end);
	normalized = g_utf8_normalize (substring, -1, G_NORMALIZE_NFKC);
	g_free (substring);

	return normalized;
}

static GBytes *
pps_view_page_accessible_text_get_contents (GtkAccessibleText *text,
                                            unsigned int start,
                                            unsigned int end)
{
	gchar *substring = get_substring (text, start, end);
	if (!substring)
		return g_bytes_new (NULL, 0);

	return g_bytes_new_take (substring, strlen (substring));
}

static void
get_range_for_granularity (GtkAccessibleText *text,
                           GtkAccessibleTextGranularity granularity,
                           unsigned int offset,
                           unsigned int *start_offset,
                           unsigned int *end_offset)
{
	PpsViewPage *self = PPS_VIEW_PAGE (text);
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	gint start = 0;
	gint end = 0;
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;

	if (!priv->page_cache)
		return;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->index, &log_attrs, &n_attrs);
	if (!log_attrs)
		return;

	if (offset >= n_attrs)
		return;

	switch (granularity) {
	case GTK_ACCESSIBLE_TEXT_GRANULARITY_CHARACTER:
		start = offset;
		end = offset + 1;
		break;
	case GTK_ACCESSIBLE_TEXT_GRANULARITY_WORD:
		start = offset;
		while (start > 0 && !log_attrs[start].is_word_start)
			start--;
		end = offset + 1;
		while (end < n_attrs && !log_attrs[end].is_word_start)
			end++;
		break;
	case GTK_ACCESSIBLE_TEXT_GRANULARITY_SENTENCE:
		for (start = offset; start > 0; start--) {
			if (log_attrs[start].is_mandatory_break && treat_as_soft_return (self, log_attrs, start - 1))
				continue;
			if (log_attrs[start].is_sentence_start)
				break;
		}
		for (end = offset + 1; end < n_attrs; end++) {
			if (log_attrs[end].is_mandatory_break && treat_as_soft_return (self, log_attrs, end - 1))
				continue;
			if (log_attrs[end].is_sentence_start)
				break;
		}
		break;
	case GTK_ACCESSIBLE_TEXT_GRANULARITY_LINE:
		start = offset;
		while (start > 0 && !log_attrs[start].is_mandatory_break)
			start--;
		end = offset + 1;
		while (end < n_attrs && !log_attrs[end].is_mandatory_break)
			end++;
		break;
	case GTK_ACCESSIBLE_TEXT_GRANULARITY_PARAGRAPH:
		/* FIXME: There is likely more than one paragraph on the page, so try to deduce it properly */
		start = 0;
		end = n_attrs;
	}

	*start_offset = start;
	*end_offset = end;
}

static GBytes *
pps_view_page_accessible_text_get_contents_at (GtkAccessibleText *text,
                                               unsigned int offset,
                                               GtkAccessibleTextGranularity granularity,
                                               unsigned int *start_offset,
                                               unsigned int *end_offset)
{
	gchar *substring;

	get_range_for_granularity (text, granularity, offset, start_offset, end_offset);
	substring = get_substring (text, *start_offset, *end_offset);

	/* If newlines appear inside the text of a sentence (i.e. between the start and
	 * end offsets returned by pps_page_accessible_get_substring), it interferes with
	 * the prosody of text-to-speech based-solutions such as a screen reader because
	 * speech synthesizers tend to pause after the newline char as if it were the end
	 * of the sentence.
	 */
	if (granularity == GTK_ACCESSIBLE_TEXT_GRANULARITY_SENTENCE)
		g_strdelimit (substring, "\n", ' ');

	return g_bytes_new_take (substring, strlen (substring));
}

static unsigned int
pps_view_page_accessible_text_get_caret_position (GtkAccessibleText *text)
{
	PpsViewPage *self = PPS_VIEW_PAGE (text);
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	PpsView *view = PPS_VIEW (gtk_widget_get_parent (GTK_WIDGET (self)));

	if (priv->index == priv->cursor_page && pps_view_is_caret_navigation_enabled (view))
		return priv->cursor_offset;

	return 0;
}

static gboolean
get_selection_bounds (PpsViewPage *self,
                      gint *start_offset,
                      gint *end_offset)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	PpsView *view = PPS_VIEW (gtk_widget_get_parent (GTK_WIDGET (self)));
	cairo_rectangle_int_t rect;
	cairo_region_t *region;
	gint start, end;
	gdouble scale = pps_document_model_get_scale (priv->model);

	region = pps_pixbuf_cache_get_selection_region (priv->pixbuf_cache,
	                                                priv->index,
	                                                scale);

	if (!region || cairo_region_is_empty (region))
		return FALSE;

	cairo_region_get_rectangle (region, 0, &rect);
	start = _pps_view_get_caret_cursor_offset_at_doc_point (view,
	                                                        priv->index,
	                                                        rect.x / scale,
	                                                        (rect.y + (rect.height / 2)) / scale);
	if (start == -1)
		return FALSE;

	cairo_region_get_rectangle (region,
	                            cairo_region_num_rectangles (region) - 1,
	                            &rect);
	end = _pps_view_get_caret_cursor_offset_at_doc_point (view,
	                                                      priv->index,
	                                                      (rect.x + rect.width) / scale,
	                                                      (rect.y + (rect.height / 2)) / scale);
	if (end == -1)
		return FALSE;

	*start_offset = start;
	*end_offset = end;

	return TRUE;
}

static gboolean
pps_view_page_accessible_text_get_selection (GtkAccessibleText *text,
                                             gsize *n_ranges,
                                             GtkAccessibleTextRange **ranges)
{
	PpsViewPage *self = PPS_VIEW_PAGE (text);

	*n_ranges = 0;

	gint start = 0, end = 0;
	if (get_selection_bounds (self, &start, &end) && start != end) {
		*n_ranges = 1;
		*ranges = g_new (GtkAccessibleTextRange, 1);
		(*ranges)[0].start = start;
		(*ranges)[0].length = end - start;
		return TRUE;
	}

	return FALSE;
}

static void
fill_run_attributes (PangoAttrList *attrs,
                     const gchar *text,
                     unsigned int offset,
                     unsigned int *start_offset,
                     unsigned int *end_offset,
                     GStrvBuilder *names,
                     GStrvBuilder *values)
{
	PangoAttrString *pango_string;
	PangoAttrInt *pango_int;
	PangoAttrColor *pango_color;
	PangoAttrIterator *iter;
	gint i, start, end;
	gboolean has_attrs = FALSE;
	glong text_length;
	gchar *attr_value;

	text_length = g_utf8_strlen (text, -1);
	if (offset < 0 || offset >= text_length)
		return;

	/* Check if there are attributes for the offset,
	 * and set the attributes range if positive */
	iter = pango_attr_list_get_iterator (attrs);
	i = g_utf8_offset_to_pointer (text, offset) - text;

	do {
		pango_attr_iterator_range (iter, &start, &end);
		if (i >= start && i < end) {
			*start_offset = g_utf8_pointer_to_offset (text, text + start);
			if (end == G_MAXINT) /* Last iterator */
				end = text_length;
			*end_offset = g_utf8_pointer_to_offset (text, text + end);
			has_attrs = TRUE;
		}
	} while (!has_attrs && pango_attr_iterator_next (iter));

	if (!has_attrs) {
		pango_attr_iterator_destroy (iter);
		return;
	}

	/* Fill the GtkAccessibleText attributes from the Pango attributes */
	pango_string = (PangoAttrString *) pango_attr_iterator_get (iter, PANGO_ATTR_FAMILY);
	if (pango_string) {
		g_strv_builder_add (names, GTK_ACCESSIBLE_ATTRIBUTE_FAMILY);
		g_strv_builder_add (values, pango_string->value);
	}

	pango_int = (PangoAttrInt *) pango_attr_iterator_get (iter, PANGO_ATTR_SIZE);
	if (pango_int) {
		attr_value = g_strdup_printf ("%i", pango_int->value / PANGO_SCALE);
		g_strv_builder_add (names, GTK_ACCESSIBLE_ATTRIBUTE_SIZE);
		g_strv_builder_add (values, attr_value);
	}

	pango_int = (PangoAttrInt *) pango_attr_iterator_get (iter, PANGO_ATTR_UNDERLINE);
	if (pango_int) {
		g_strv_builder_add (names, GTK_ACCESSIBLE_ATTRIBUTE_UNDERLINE);
		g_strv_builder_add (values, pango_int->value ? GTK_ACCESSIBLE_ATTRIBUTE_UNDERLINE_NONE : GTK_ACCESSIBLE_ATTRIBUTE_UNDERLINE_SINGLE);
	}
	pango_color = (PangoAttrColor *) pango_attr_iterator_get (iter, PANGO_ATTR_FOREGROUND);
	if (pango_color) {
		attr_value = g_strdup_printf ("%u,%u,%u",
		                              pango_color->color.red,
		                              pango_color->color.green,
		                              pango_color->color.blue);
		g_strv_builder_add (names, GTK_ACCESSIBLE_ATTRIBUTE_FOREGROUND);
		g_strv_builder_add (values, attr_value);
	}

	pango_attr_iterator_destroy (iter);
}

static gboolean
pps_view_page_accessible_text_get_attributes (GtkAccessibleText *text,
                                              unsigned int offset,
                                              gsize *n_ranges,
                                              GtkAccessibleTextRange **ranges,
                                              char ***attribute_names,
                                              char ***attribute_values)
{
	PpsViewPage *self = PPS_VIEW_PAGE (text);
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	PangoAttrList *attrs;
	const gchar *page_text;
	unsigned int start_offset = 0;
	unsigned int end_offset = 0;
	PpsPageCache *cache = priv->page_cache;

	if (offset < 0)
		goto no_attrs;

	if (!cache)
		goto no_attrs;

	page_text = pps_page_cache_get_text (cache, priv->index);
	if (!page_text)
		goto no_attrs;

	attrs = pps_page_cache_get_text_attrs (cache, priv->index);
	if (!attrs)
		goto no_attrs;

	GStrvBuilder *names = g_strv_builder_new ();
	GStrvBuilder *values = g_strv_builder_new ();
	fill_run_attributes (attrs, page_text, offset, &start_offset, &end_offset, names, values);
	*n_ranges = 1;
	*ranges = g_new (GtkAccessibleTextRange, 1);
	(*ranges)[0].start = start_offset;
	(*ranges)[0].length = end_offset - start_offset;
	*attribute_names = g_strv_builder_end (names);
	*attribute_values = g_strv_builder_end (values);
	g_strv_builder_unref (names);
	g_strv_builder_unref (values);
	if (attribute_names[0] && attribute_names[0][0])
		return TRUE;
	else {
		*n_ranges = 0;
		return FALSE;
	}
no_attrs:
	*n_ranges = 0;
	*attribute_names = g_new0 (char *, 1);
	*attribute_values = g_new0 (char *, 1);
	return FALSE;
}

static void
pps_view_page_accessible_text_get_default_attributes (GtkAccessibleText *text,
                                                      char ***attribute_names,
                                                      char ***attribute_values)
{
	/* No default attributes */
	*attribute_names = g_new0 (char *, 1);
	*attribute_values = g_new0 (char *, 1);
	return;
}

static gboolean
pps_view_page_accessible_text_get_extents (GtkAccessibleText *text,
                                           unsigned int start,
                                           unsigned int end,
                                           graphene_rect_t *extents)
{
	PpsViewPage *self = PPS_VIEW_PAGE (text);
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	GtkWidget *toplevel;
	PpsRectangle *areas = NULL;
	PpsRectangle *doc_rect;
	guint n_areas = 0;
	graphene_point_t widget_point;
	GdkRectangle view_rect;
	PpsPageCache *cache = priv->page_cache;

	if (!cache)
		return FALSE;

	pps_page_cache_get_text_layout (cache, priv->index, &areas, &n_areas);
	if (!areas || start < 0 || start >= n_areas || end >= n_areas || start > end)
		return FALSE;

	extents->origin.x = 0;
	extents->origin.y = 0;
	extents->size.width = 0;
	extents->size.height = 0;
	toplevel = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self)));
	for (int offset = start; offset <= end; offset++) {
		doc_rect = areas + offset;
		doc_rect_to_view_rect (self, doc_rect, &view_rect);

		if (!gtk_widget_compute_point (GTK_WIDGET (self), toplevel, graphene_point_zero (), &widget_point))
			return FALSE;
		view_rect.x += widget_point.x;
		view_rect.y += widget_point.y;

		extents->origin.x = MAX (extents->origin.x, view_rect.x);
		extents->origin.y = MAX (extents->origin.y, view_rect.y);
		extents->size.width = MAX (extents->size.width, view_rect.width);
		extents->size.height = MAX (extents->size.height, view_rect.height);
	}

	return TRUE;
}

static gboolean
pps_view_page_accessible_text_get_offset (GtkAccessibleText *text,
                                          const graphene_point_t *point,
                                          unsigned int *offset)
{
	PpsViewPage *self = PPS_VIEW_PAGE (text);
	PpsViewPagePrivate *priv = GET_PRIVATE (self);
	PpsView *view = PPS_VIEW (gtk_widget_get_parent (GTK_WIDGET (self)));
	GtkWidget *toplevel;
	PpsRectangle *areas = NULL;
	PpsRectangle *rect = NULL;
	guint n_areas = 0;
	guint i;
	graphene_point_t widget_point;
	gdouble view_x, view_y;
	PpsPoint doc_point;
	PpsPageCache *cache = priv->page_cache;

	if (!cache)
		return FALSE;

	pps_page_cache_get_text_layout (cache, priv->index, &areas, &n_areas);
	if (!areas)
		return FALSE;

	view_x = point->x;
	view_y = point->y;
	toplevel = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self)));
	if (!gtk_widget_compute_point (GTK_WIDGET (self), toplevel, graphene_point_zero (), &widget_point))
		return FALSE;
	view_x -= widget_point.x;
	view_y -= widget_point.y;

	doc_point = pps_view_get_point_on_page (view, priv->index, view_x, view_y);

	for (i = 0; i < n_areas; i++) {
		rect = areas + i;
		if (doc_point.x >= rect->x1 && doc_point.x <= rect->x2 &&
		    doc_point.y >= rect->y1 && doc_point.y <= rect->y2)
			*offset = i;
	}

	return TRUE;
}

void
pps_view_page_accessible_text_init (GtkAccessibleTextInterface *iface)
{
	iface->get_contents = pps_view_page_accessible_text_get_contents;
	iface->get_contents_at = pps_view_page_accessible_text_get_contents_at;
	iface->get_caret_position = pps_view_page_accessible_text_get_caret_position;
	iface->get_selection = pps_view_page_accessible_text_get_selection;
	iface->get_attributes = pps_view_page_accessible_text_get_attributes;
	iface->get_default_attributes = pps_view_page_accessible_text_get_default_attributes;
	iface->get_extents = pps_view_page_accessible_text_get_extents;
	iface->get_offset = pps_view_page_accessible_text_get_offset;
}

void
pps_view_page_init (PpsViewPage *page)
{
	GtkGesture *annot_drag;

	annot_drag = gtk_gesture_drag_new ();
	gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (annot_drag),
	                                            GTK_PHASE_CAPTURE);
	gtk_gesture_single_set_exclusive (GTK_GESTURE_SINGLE (annot_drag), TRUE);
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (annot_drag), 1);

	g_signal_connect (annot_drag, "drag-begin",
	                  G_CALLBACK (annotation_drag_begin_cb), page);

	gtk_widget_add_controller (GTK_WIDGET (page),
	                           GTK_EVENT_CONTROLLER (annot_drag));
	g_signal_connect (annot_drag, "drag-end",
	                  G_CALLBACK (annotation_drag_end_cb), page);
	g_signal_connect (annot_drag, "drag-update",
	                  G_CALLBACK (annotation_drag_update_cb), page);

	gtk_widget_add_css_class (GTK_WIDGET (page), PPS_STYLE_CLASS_DOCUMENT_PAGE);
	gtk_widget_add_css_class (GTK_WIDGET (page), "card");
}

static void
pps_view_page_dispose (GObject *object)
{
	PpsViewPage *page = PPS_VIEW_PAGE (object);
	PpsViewPagePrivate *priv = GET_PRIVATE (page);

	if (priv->model != NULL)
		g_signal_handlers_disconnect_by_data (priv->model, page);
	if (priv->pixbuf_cache != NULL)
		g_signal_handlers_disconnect_by_data (priv->pixbuf_cache, page);
	if (priv->search_context != NULL)
		g_signal_handlers_disconnect_by_data (priv->search_context, page);
	if (priv->annots_context != NULL)
		g_signal_handlers_disconnect_by_data (priv->annots_context, page);

	g_clear_object (&priv->model);
	g_clear_object (&priv->pixbuf_cache);
	g_clear_object (&priv->page_cache);
	g_clear_object (&priv->annots_context);
	g_clear_object (&priv->search_context);
	g_clear_object (&priv->annots_context);

	for (int i = 0; i < LAYER_COUNT; i++) {
		if (priv->layers[i]) {
			gtk_widget_unparent (GTK_WIDGET (priv->layers[i]));
		}
	}

	G_OBJECT_CLASS (pps_view_page_parent_class)->dispose (object);
}

static void
pps_view_page_size_allocate (GtkWidget *widget,
                             int width,
                             int height,
                             int baseline)
{
	PpsViewPage *view_page = PPS_VIEW_PAGE (widget);
	PpsViewPagePrivate *priv = GET_PRIVATE (view_page);
	gdouble scale = pps_document_model_get_scale (priv->model);

	for (GtkWidget *child = gtk_widget_get_first_child (widget);
	     child != NULL;
	     child = gtk_widget_get_next_sibling (child)) {
		GdkRectangle real_view_area;
		if (PPS_IS_OVERLAY (child)) {
			gdouble padding;
			g_autofree PpsRectangle *doc_rect;
			GdkRectangle minimum_area = { 0, 0, 0, 0 };

			doc_rect = pps_overlay_get_area (PPS_OVERLAY (child), &padding);

			real_view_area.x = doc_rect->x1 * scale - padding;
			real_view_area.y = doc_rect->y1 * scale - padding;
			real_view_area.width = (doc_rect->x2 - doc_rect->x1) * scale + 2 * padding;
			real_view_area.height = (doc_rect->y2 - doc_rect->y1) * scale + 2 * padding;

			gtk_widget_set_size_request (child, real_view_area.width, real_view_area.height);

			/* FIXME: we first allocate a widget its minimum size to force any text view to recompute its height */
			gtk_widget_measure (child, GTK_ORIENTATION_HORIZONTAL, real_view_area.height, &minimum_area.width, NULL, NULL, NULL);
			gtk_widget_measure (child, GTK_ORIENTATION_VERTICAL, real_view_area.width, &minimum_area.height, NULL, NULL, NULL);
			gtk_widget_size_allocate (child, &minimum_area, baseline);

			gtk_widget_size_allocate (child, &real_view_area, baseline);
		} else if (PPS_IS_ANNOTATION_LAYER (child)) {
			real_view_area.x = 0;
			real_view_area.y = 0;
			real_view_area.width = width;
			real_view_area.height = height;
			gtk_widget_set_size_request (child, width, height);
			// TODO: this is a temporary solution to eliminate the warning
			gtk_widget_measure (child, GTK_ORIENTATION_HORIZONTAL, height, NULL, NULL, NULL, NULL);
			gtk_widget_size_allocate (child, &real_view_area, baseline);
		}
	}
}

void
pps_view_page_class_init (PpsViewPageClass *page_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (page_class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (page_class);

	object_class->dispose = pps_view_page_dispose;
	object_class->get_property = pps_view_page_get_property;
	object_class->set_property = pps_view_page_set_property;
	widget_class->snapshot = pps_view_page_snapshot;
	widget_class->measure = pps_view_page_measure;
	widget_class->size_allocate = pps_view_page_size_allocate;
	properties[PROP_PAGE] =
	    g_param_spec_int ("page",
	                      "Page",
	                      "Current page index",
	                      -1, G_MAXINT, 0,
	                      G_PARAM_READWRITE |
	                          G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class,
	                                 PROP_PAGE,
	                                 properties[PROP_PAGE]);

	gtk_widget_class_set_accessible_role (GTK_WIDGET_CLASS (page_class), GTK_ACCESSIBLE_ROLE_PARAGRAPH);
}

void
pps_view_page_setup (PpsViewPage *page,
                     PpsDocumentModel *model,
                     PpsAnnotationsContext *annots_context,
                     PpsSearchContext *search_context,
                     PpsPageCache *page_cache,
                     PpsPixbufCache *pixbuf_cache)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);
	GtkWidget *view = gtk_widget_get_parent (GTK_WIDGET (page));

	g_return_if_fail (PPS_IS_VIEW (view));

	if (priv->model != NULL)
		g_signal_handlers_disconnect_by_data (priv->model, page);
	if (priv->pixbuf_cache != NULL)
		g_signal_handlers_disconnect_by_data (priv->pixbuf_cache, page);
	if (priv->search_context != NULL)
		g_signal_handlers_disconnect_by_data (priv->search_context, page);
	if (priv->annots_context != NULL)
		g_signal_handlers_disconnect_by_data (priv->annots_context, page);

	g_set_object (&priv->model, model);
	g_set_object (&priv->annots_context, annots_context);
	g_set_object (&priv->search_context, search_context);
	g_set_object (&priv->page_cache, page_cache);
	g_set_object (&priv->pixbuf_cache, pixbuf_cache);
	g_set_object (&priv->annots_context, annots_context);
	pps_view_page_set_page (page, -1);

	g_signal_connect_swapped (priv->model, "notify::scale",
	                          G_CALLBACK (gtk_widget_queue_resize), page);
	g_signal_connect (priv->model, "notify::inverted-colors",
	                  G_CALLBACK (inverted_changed_cb), page);
	g_signal_connect (priv->pixbuf_cache, "job-finished",
	                  G_CALLBACK (job_finished_cb), page);
	if (priv->search_context != NULL) {
		g_signal_connect_swapped (priv->search_context, "finished",
		                          G_CALLBACK (search_results_changed_cb), page);
		g_signal_connect_swapped (priv->search_context, "result-activated",
		                          G_CALLBACK (search_results_changed_cb), page);
		g_signal_connect_swapped (priv->search_context, "notify::active",
		                          G_CALLBACK (gtk_widget_queue_draw), page);
	}

	if (pps_document_model_get_inverted_colors (priv->model))
		gtk_widget_add_css_class (GTK_WIDGET (page), PPS_STYLE_CLASS_INVERTED);

	g_signal_handlers_disconnect_by_data (view, page);
	g_signal_connect_object (view,
	                         "selection-changed",
	                         G_CALLBACK (view_selection_changed_cb),
	                         page, G_CONNECT_SWAPPED);
	g_signal_connect_object (view,
	                         "cursor-moved",
	                         G_CALLBACK (view_cursor_moved_cb),
	                         page, 0);
}

PpsViewPage *
pps_view_page_new (void)
{
	return g_object_new (PPS_TYPE_VIEW_PAGE,
	                     "overflow", GTK_OVERFLOW_HIDDEN,
	                     "focusable", TRUE,
	                     NULL);
}


void
pps_view_page_set_page (PpsViewPage *page, gint index)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);

	g_return_if_fail (priv->model != NULL && pps_document_model_get_document (priv->model) != NULL);

	if (priv->index == index)
		return;

	if (0 <= index && index < pps_document_get_n_pages (pps_document_model_get_document (priv->model))) {
		priv->index = index;
	} else {
		priv->index = -1;
	}

	priv->had_search_results = FALSE;

	g_object_notify_by_pspec (G_OBJECT (page), properties[PROP_PAGE]);

	display_annotation_layers (page);

	gtk_widget_queue_resize (GTK_WIDGET (page));
}


gint
pps_view_page_get_page (PpsViewPage *page)
{
	PpsViewPagePrivate *priv = GET_PRIVATE (page);

	return priv->index;
}
