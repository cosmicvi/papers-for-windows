// SPDX-License-Identifier: GPL-2.0-or-later
/* pps-annotation-layer-ink.c
 * this file is part of papers, a gnome document viewer
 *
 * Copyright (C) 2025 Lucas Baudin <lbaudin@gnome.org>
 */

#include "pps-annotation-layer-ink.h"
#include "pps-annotation-model.h"
#include "pps-view.h"

#include <cairo.h>
#include <math.h>

typedef struct {
	GskPath *path;
	GskStroke *stroke;
	GdkRGBA color;
	graphene_rect_t bounds;
	PpsAnnotation *annot;
} InkPathDrawData;

// #define PATH_DEBUG

typedef struct {
	GList *pending_draw;
	GSList *pending_times;

	GList *ink_paths;

	gdouble previous_drag_x;
	gdouble previous_drag_y;

	cairo_surface_t *pencil_pixbuf;
	cairo_surface_t *highlight_pixbuf;
	cairo_surface_t *eraser_pixbuf;
} PpsAnnotationLayerInkPrivate;

struct _PpsAnnotationLayerInk {
	PpsAnnotationLayer base_instance;
};

enum { PROP_0 };

G_DEFINE_TYPE_WITH_PRIVATE (PpsAnnotationLayerInk, pps_annotation_layer_ink, PPS_TYPE_ANNOTATION_LAYER)
#define INK_GET_PRIVATE(o) pps_annotation_layer_ink_get_instance_private (o)

#define GET_ANNOT_MODEL(d) pps_document_model_get_annotation_model (pps_annotation_layer_get_doc_model (PPS_ANNOTATION_LAYER (d)))

#define GET_DOC_MODEL(d) pps_annotation_layer_get_doc_model (PPS_ANNOTATION_LAYER (d))
#define GET_ANNOT_CONTEXT(d) pps_annotation_layer_get_annotations_context (PPS_ANNOTATION_LAYER (d))

#define GET_DOC(d) pps_document_model_get_document (pps_annotation_layer_get_doc_model (PPS_ANNOTATION_LAYER (d)))
#define GET_PAGE_INDEX(d) pps_annotation_layer_get_page (PPS_ANNOTATION_LAYER (d))

static inline gboolean
is_point_in_circle (PpsPoint p, PpsPoint center, gdouble radius)
{
	gdouble dx = p.x - center.x;
	gdouble dy = p.y - center.y;
	return (dx * dx + dy * dy) <= (radius * radius);
}

static inline void
line_circle_intersection (PpsPoint A, PpsPoint B, PpsPoint center, double radius, PpsPoint *intersect1, PpsPoint *intersect2, gboolean *i1_found, gboolean *i2_found)
{
	gdouble t1, t2;
	gdouble dx = B.x - A.x;
	gdouble dy = B.y - A.y;
	gdouble fx = A.x - center.x;
	gdouble fy = A.y - center.y;

	gdouble a = dx * dx + dy * dy;
	gdouble b = 2 * (fx * dx + fy * dy);
	gdouble cc = fx * fx + fy * fy - radius * radius;

	gdouble discriminant = b * b - 4 * a * cc;
	*i1_found = FALSE;
	*i2_found = FALSE;

	// no root, meaning that there is no intersection between the line AB and the circle
	if (discriminant < 0) {
		return;
	}

	discriminant = sqrt (discriminant);

	t1 = (-b - discriminant) / (2 * a);
	t2 = (-b + discriminant) / (2 * a);

	// the first root is between A and B
	if (t1 >= 0 && t1 <= 1) {
		t1 = CLAMP (t1, 0., 1.);
		intersect1->x = A.x + t1 * dx;
		intersect1->y = A.y + t1 * dy;
		*i1_found = TRUE;
	}

	// the second root is between A and B
	if (t2 >= 0 && t2 <= 1) {
		t2 = CLAMP (t2, 0., 1.);
		intersect2->x = A.x + t2 * dx;
		intersect2->y = A.y + t2 * dy;
		*i2_found = TRUE;
	}
}

typedef enum {
	NO_INTER,
	INTER_BEGIN,
	INTER_END,
	INTER_FULL,
	INTER_MID
} Intersection;

static void
subtract_circle_from_segment (PpsPoint A, PpsPoint B, PpsPoint center, double radius, Intersection *res, PpsPoint *i1_, PpsPoint *i2_)
{
	PpsPoint intersect1, intersect2;
	gboolean i1_found, i2_found;

	line_circle_intersection (A, B, center, radius, &intersect1, &intersect2, &i1_found, &i2_found);

	if (!i1_found && !i2_found) {
		if (!is_point_in_circle (A, center, radius)) {
			g_assert (!is_point_in_circle (B, center, radius));
			*res = NO_INTER;
		} else {
			g_assert (is_point_in_circle (B, center, radius));
			*res = INTER_FULL;
		}
	} else {
		if (is_point_in_circle (A, center, radius) && is_point_in_circle (B, center, radius)) {
			*res = INTER_FULL;
		} else if (is_point_in_circle (A, center, radius)) {
			g_assert (!i1_found && i2_found);
			*i2_ = intersect2;
			*res = INTER_BEGIN;
		} else if (is_point_in_circle (B, center, radius)) {
			g_assert (i1_found && !i2_found);
			*i1_ = intersect1;
			*res = INTER_END;
		} else {
			g_assert (i1_found && i2_found);
			*i1_ = intersect1;
			*i2_ = intersect2;
			*res = INTER_MID;
		}
	}
}

static double
get_current_tool_radius (PpsAnnotationLayerInk *draw)
{
	PpsAnnotationModel *annotation_model = GET_ANNOT_MODEL (draw);

	switch (pps_annotation_model_get_tool (annotation_model)) {
	case TOOL_PENCIL:
		return pps_annotation_model_get_pen_radius (annotation_model);
	case TOOL_HIGHLIGHT:
		return pps_annotation_model_get_highlight_radius (annotation_model);
	case TOOL_ERASER:
		return pps_annotation_model_get_eraser_radius (annotation_model);
	default:
		return 0.1;
	}
}

static GdkRGBA *
get_current_tool_color (PpsAnnotationLayerInk *draw)
{
	PpsAnnotationModel *annotation_model = GET_ANNOT_MODEL (draw);

	switch (pps_annotation_model_get_tool (annotation_model)) {
	case TOOL_HIGHLIGHT:
		return pps_annotation_model_get_highlight_color (annotation_model);
	default:
		return pps_annotation_model_get_pen_color (annotation_model);
	}
}

static void
create_path_data (PpsAnnotationLayerInk *draw, PpsAnnotationInk *ink, InkPathDrawData **path_data)
{
	GskPathBuilder *builder;
	GskPath *path = NULL;
	GskStroke *stroke = NULL;
	gdouble scale = pps_document_model_get_scale (GET_DOC_MODEL (draw));
	gdouble page_width, page_height;
	GdkRGBA color;
	graphene_rect_t bounds = GRAPHENE_RECT_INIT_ZERO;

	pps_annotation_get_rgba (PPS_ANNOTATION (ink), &color);

	pps_document_get_page_size (GET_DOC (draw), GET_PAGE_INDEX (draw), &page_width, &page_height);

	PpsInkList *ink_list = pps_annotation_ink_get_ink_list (ink);
	if (ink_list && ink_list->paths) {
		stroke = gsk_stroke_new (scale * pps_annotation_get_border_width (PPS_ANNOTATION (ink)));
		builder = gsk_path_builder_new ();

		for (gsize i = 0; i < ink_list->n_paths; i++) {
			PpsPath *path = ink_list->paths[i];
			for (gsize j = 0; j < path->n_points; j++) {

				PpsPoint *p = &(path->points[j]);
				if (j == 0) {
					bounds.origin.x = p->x * scale;
					bounds.origin.y = (page_height - p->y) * scale;
					gsk_path_builder_move_to (builder, p->x * scale, (page_height - p->y) * scale);
				} else {
					graphene_point_t po = { p->x * scale, (page_height - p->y) * scale };
					graphene_rect_expand (&bounds, &po, &bounds);
					gsk_path_builder_line_to (builder, p->x * scale, (page_height - p->y) * scale);
				}
			}
		}

		path = gsk_path_builder_free_to_path (builder);
	}
	if (!*path_data) {
		*path_data = g_malloc (sizeof (InkPathDrawData));
		(*path_data)->annot = PPS_ANNOTATION (ink);
	} else {
		gsk_path_unref ((*path_data)->path);
		gsk_stroke_free ((*path_data)->stroke);
	}

	(*path_data)->color = color;
	(*path_data)->path = path;
	(*path_data)->stroke = stroke;
	bounds.origin.x -= gsk_stroke_get_line_width (stroke);
	bounds.origin.y -= gsk_stroke_get_line_width (stroke);
	bounds.size.height += 2 * gsk_stroke_get_line_width (stroke);
	bounds.size.width += 2 * gsk_stroke_get_line_width (stroke);
	bounds.origin.x = floor (bounds.origin.x);
	bounds.origin.y = floor (bounds.origin.y);
	bounds.size.width = ceil (bounds.size.width);
	bounds.size.height = ceil (bounds.size.height);
	(*path_data)->bounds = bounds;
}

static void
drag_end (GtkGestureDrag *annotation_drag_gesture,
          gdouble offset_x,
          gdouble offset_y,
          PpsAnnotationLayerInk *draw)
{
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (draw);

	g_autoptr (PpsPage) page = NULL;
	GSList *points_list = NULL;
	GList *points = NULL;
	gdouble scale = 0.0;
	gdouble radius = 0.0;

	PpsAnnotationTool tool = pps_annotation_model_get_tool (pps_document_model_get_annotation_model (GET_DOC_MODEL (draw)));

	GdkDeviceTool *device_tool = gdk_event_get_device_tool (gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (annotation_drag_gesture)));

	if (device_tool && gdk_device_tool_get_tool_type (device_tool) == GDK_DEVICE_TOOL_TYPE_ERASER) {
		tool = TOOL_ERASER;
	}

	if (tool == TOOL_PENCIL || tool == TOOL_HIGHLIGHT) {
		gdouble height;
		GdkRGBA *stroke = get_current_tool_color (draw);
		PpsAnnotationInkAddData add_data;

		scale = pps_document_model_get_scale (GET_DOC_MODEL (draw));
		page = pps_document_get_page (GET_DOC (draw), GET_PAGE_INDEX (draw));
		pps_document_get_page_size (GET_DOC (draw), GET_PAGE_INDEX (draw), NULL, &height);

		points = priv->pending_draw;
		while (points) {
			PpsPoint *i = points->data;
			i->x = i->x / scale;
			i->y = height - i->y / scale;
			points_list = g_slist_append (points_list, i);
			points = g_list_next (points);
		}
		radius = get_current_tool_radius (draw);

		add_data.highlight = tool == TOOL_HIGHLIGHT;
		add_data.ink_list = pps_ink_list_new_for_list (g_slist_append (NULL, pps_path_new_for_list (points_list)));
		add_data.times = g_slist_reverse (priv->pending_times);
		priv->pending_times = NULL;
		add_data.line_width = 2 * radius;
		pps_annotations_context_add_annotation_sync (GET_ANNOT_CONTEXT (draw),
		                                             GET_PAGE_INDEX (draw),
		                                             PPS_ANNOTATION_TYPE_INK,
		                                             NULL, NULL, stroke, &add_data);
		g_slist_free_full (add_data.times, (GDestroyNotify) g_free);
		pps_ink_list_free (add_data.ink_list);

		g_list_free (priv->pending_draw);
		priv->pending_draw = NULL;
	}
}

static GSList *
erase_ink_list (PpsInkList *ink_list,
                PpsPoint center,
                gdouble radius,
                gboolean erase_objects,
                gboolean *changed)
{
	GSList *new_list = NULL;

	for (int i = 0; i < ink_list->n_paths; i++) {
		PpsPath *path = ink_list->paths[i];
		GSList *new_path = NULL;

		for (int i = 0; i < path->n_points - 1; i++) {
			PpsPoint *a = &path->points[i];
			PpsPoint *b = &path->points[i + 1];
			PpsPoint i1, i2;
			Intersection res;
			subtract_circle_from_segment (*a, *b, center, radius, &res, &i1, &i2);

			if (erase_objects) {
				if (res != NO_INTER) {
					*changed = TRUE;
					g_slist_free_full (new_path, g_free);
					new_path = NULL;
					break;
				} else {
					new_path = g_slist_append (new_path, pps_point_copy (a));
					if (i == path->n_points - 2)
						new_path = g_slist_append (new_path, pps_point_copy (b));
				}
			} else {
				switch (res) {
				case INTER_BEGIN:
					*changed = TRUE;
					if (new_path != NULL) {
						new_list = g_slist_append (new_list, pps_path_new_for_list (new_path));
						new_path = NULL;
					}
					new_path = g_slist_append (new_path, pps_point_copy (&i2));
					if (i == path->n_points - 2)
						new_path = g_slist_append (new_path, pps_point_copy (b));
					break;
				case INTER_END:
					*changed = TRUE;
					new_path = g_slist_append (new_path, pps_point_copy (a));
					new_path = g_slist_append (new_path, pps_point_copy (&i1));
					new_list = g_slist_append (new_list, pps_path_new_for_list (new_path));
					new_path = NULL;
					break;
				case INTER_FULL:
					*changed = TRUE;
					if (new_path != NULL) {
						new_list = g_slist_append (new_list, pps_path_new_for_list (new_path));
						new_path = NULL;
					}
					break;
				case INTER_MID:
					*changed = TRUE;
					new_path = g_slist_append (new_path, pps_point_copy (a));
					new_path = g_slist_append (new_path, pps_point_copy (&i1));
					new_list = g_slist_append (new_list, pps_path_new_for_list (new_path));
					new_path = NULL;
					new_path = g_slist_append (new_path, pps_point_copy (&i2));
					if (i == path->n_points - 2)
						new_path = g_slist_append (new_path, pps_point_copy (b));
					break;
				case NO_INTER:
					new_path = g_slist_append (new_path, pps_point_copy (a));
					if (i == path->n_points - 2)
						new_path = g_slist_append (new_path, pps_point_copy (b));
					break;
				}
			}
		}

		if (new_path != NULL && g_slist_length (new_path) > 1) {
			new_list = g_slist_append (new_list, pps_path_new_for_list (new_path));
		}
	}
	return new_list;
}

static void
drag_update (GtkGestureDrag *annotation_drag_gesture,
             gdouble offset_x,
             gdouble offset_y,
             PpsAnnotationLayerInk *draw)
{
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (draw);
	PpsAnnotationTool tool = pps_annotation_model_get_tool (pps_document_model_get_annotation_model (GET_DOC_MODEL (draw)));
	GdkDeviceTool *device_tool = gdk_event_get_device_tool (gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (annotation_drag_gesture)));
	gdouble scale = pps_document_model_get_scale (GET_DOC_MODEL (draw));
	gdouble height;
	pps_document_get_page_size (GET_DOC (draw), GET_PAGE_INDEX (draw), NULL, &height);

	if (device_tool && gdk_device_tool_get_tool_type (device_tool) == GDK_DEVICE_TOOL_TYPE_ERASER) {
		tool = TOOL_ERASER;
	}

#define TO_DOCX(xx) (init->x + xx) / scale
#define TO_DOCY(yy) height - (init->y + yy) / scale

	gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture), GTK_EVENT_SEQUENCE_CLAIMED);

	if (tool == TOOL_PENCIL || tool == TOOL_HIGHLIGHT) {
		PpsPoint *p = g_malloc (sizeof (PpsPoint));
		PpsPoint *init = priv->pending_draw->data;
		PpsInkTime *t = g_new (PpsInkTime, 1);
		p->x = init->x + offset_x;
		p->y = init->y + offset_y;

		if (gdk_event_get_modifier_state (gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (annotation_drag_gesture))) & GDK_SHIFT_MASK) {
			p->y = ((PpsPoint *) g_list_last (priv->pending_draw)->data)->y;
		}
		priv->pending_draw = g_list_append (priv->pending_draw, p);
		t->time = gtk_event_controller_get_current_event_time (GTK_EVENT_CONTROLLER (annotation_drag_gesture));
		t->x = TO_DOCX (offset_x);
		t->y = TO_DOCY (offset_y);
		priv->pending_times = g_slist_prepend (priv->pending_times, t);
	} else if (tool == TOOL_ERASER) {
		gdouble radius = pps_annotation_model_get_eraser_radius (pps_document_model_get_annotation_model (GET_DOC_MODEL (draw)));
		gboolean erase_objects = pps_annotation_model_get_eraser_objects (GET_ANNOT_MODEL (draw));

		GSList *to_save = NULL;
		GSList *to_remove = NULL;
		gdouble x, y;
		PpsPoint *init = priv->pending_draw->data;
		x = TO_DOCX (offset_x);
		y = TO_DOCY (offset_y);

		for (GList *paths = priv->ink_paths; paths; paths = g_list_next (paths)) {
			PpsAnnotationInk *ink_annot = PPS_ANNOTATION_INK (((InkPathDrawData *) (paths->data))->annot);
			PpsInkList *ink_list = pps_annotation_ink_get_ink_list (ink_annot);
			gdouble prev_x, prev_y;
			GSList *ink_paths = NULL;

			/* this is somewhat tricky because we have to erase everything between the
			last known point and the current point, not only the current point */
			prev_x = TO_DOCX (priv->previous_drag_x);
			prev_y = TO_DOCY (priv->previous_drag_y);

			int n = (int) (sqrt ((prev_x - x) * (prev_x - x) + (prev_y - y) * (prev_y - y)) / radius) + 1;
			gboolean c = FALSE;
			for (int i = 1; i <= n; i++) {
				gdouble t = ((double) i) / (double) n;
				gboolean changed = FALSE;
				PpsPoint center = { prev_x * (1. - t) + x * t, prev_y * (1. - t) + y * t };
				ink_paths = erase_ink_list (ink_list, center, radius, erase_objects, &changed);
				c |= changed;
			}
			if (ink_paths) {
				if (c) {
					pps_annotation_ink_set_ink_list (ink_annot, pps_ink_list_new_for_list (ink_paths));
					to_save = g_slist_append (to_save, ink_annot);
				}
			} else {
				to_remove = g_slist_append (to_remove, ink_annot);
			}
		}
		for (GSList *l = to_remove; l; l = g_slist_next (l)) {
			pps_annotations_context_remove_annotation (GET_ANNOT_CONTEXT (draw), PPS_ANNOTATION (l->data));
		}
		/* also remove other annotations */
		if (erase_objects) {
			PpsAnnotationsContext *annots_context = GET_ANNOT_CONTEXT (draw);
			GListModel *all_annots = pps_annotations_context_get_annots_model (annots_context);
			gint page_index = GET_PAGE_INDEX (draw);

			for (int i = pps_annotations_context_first_index_for_page (annots_context, page_index);
			     i < g_list_model_get_n_items (all_annots); i++) {
				PpsAnnotation *annot = PPS_ANNOTATION (g_list_model_get_item (all_annots, i));
				PpsRectangle area;

				if (pps_annotation_get_page_index (annot) != page_index) {
					break;
				}
				if (PPS_IS_ANNOTATION_INK (annot)) {
					continue;
				}
				pps_annotation_get_area (annot, &area);
				if (area.x1 <= x && area.x2 >= x && area.y1 <= height - y && area.y2 >= height - y) {
					pps_annotations_context_remove_annotation (annots_context, annot);
				}
			}
		}
	}
	priv->previous_drag_x = offset_x;
	priv->previous_drag_y = offset_y;
	gtk_widget_queue_draw (GTK_WIDGET (draw));
}

static void
drag_begin (GtkGestureDrag *annotation_drag_gesture,
            gdouble offset_x,
            gdouble offset_y,
            PpsAnnotationLayerInk *overlay)
{

	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (overlay);
	PpsPoint *first_point = g_malloc (sizeof (PpsPoint));
	PpsInkTime *first_time_point = g_new (PpsInkTime, 1);
	gdouble scale = pps_document_model_get_scale (GET_DOC_MODEL (overlay));
	gdouble height;
	pps_document_get_page_size (GET_DOC (overlay), GET_PAGE_INDEX (overlay), NULL, &height);

	first_point->x = offset_x;
	first_point->y = offset_y;
	first_time_point->time = gtk_event_controller_get_current_event_time (GTK_EVENT_CONTROLLER (annotation_drag_gesture));
	first_time_point->x = offset_x / scale;
	first_time_point->y = height - offset_y / scale;

	priv->pending_draw = g_list_alloc ();
	priv->pending_draw->data = first_point;
	priv->previous_drag_x = 0;
	priv->previous_drag_y = 0;
	priv->pending_times = g_slist_prepend (NULL, first_time_point);

	gtk_gesture_set_state (GTK_GESTURE (annotation_drag_gesture),
	                       GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
update_cursor (PpsAnnotationLayerInk *draw)
{
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (draw);
	gdouble scale = pps_document_model_get_scale (GET_DOC_MODEL (draw));
	gdouble radius = get_current_tool_radius (draw);
	GdkRGBA *color = get_current_tool_color (draw);
	int circle_radius = MAX (radius * scale, 2);
	int icon_width, icon_height;
	cairo_surface_t *icon_surface, *surface = NULL;
	cairo_t *cr;
	g_autoptr (GdkPixbuf) combined_pixbuf = NULL;
	g_autoptr (GdkTexture) texture = NULL;
	g_autoptr (GdkCursor) cursor = NULL;

	switch (pps_annotation_model_get_tool (GET_ANNOT_MODEL (draw))) {
	case TOOL_PENCIL:
		if (!priv->pencil_pixbuf) {
			g_autoptr (GdkPixbuf) pen_pixbuf;
			pen_pixbuf = gdk_pixbuf_new_from_resource ("/org/gnome/papers/icons/scalable/apps/pencil-symbolic.svg", NULL);
			priv->pencil_pixbuf = pps_document_misc_surface_from_pixbuf (pen_pixbuf);
		}
		icon_surface = priv->pencil_pixbuf;
		break;
	case TOOL_ERASER:
		if (!priv->eraser_pixbuf) {
			g_autoptr (GdkPixbuf) eraser_pixbuf;
			eraser_pixbuf = gdk_pixbuf_new_from_resource ("/org/gnome/papers/icons/scalable/apps/eraser-symbolic.svg", NULL);
			priv->eraser_pixbuf = pps_document_misc_surface_from_pixbuf (eraser_pixbuf);
		}
		icon_surface = priv->eraser_pixbuf;
		color = NULL;
		break;
	case TOOL_HIGHLIGHT:
	default:
		if (!priv->highlight_pixbuf) {
			g_autoptr (GdkPixbuf) highlight_pixbuf;
			highlight_pixbuf = gdk_pixbuf_new_from_resource ("/org/gnome/papers/icons/scalable/apps/marker-symbolic.svg", NULL);
			priv->highlight_pixbuf = pps_document_misc_surface_from_pixbuf (highlight_pixbuf);
		}
		icon_surface = priv->highlight_pixbuf;
		break;
	}
	icon_width = cairo_image_surface_get_width (icon_surface);
	icon_height = cairo_image_surface_get_height (icon_surface);
	surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 2 * circle_radius + icon_width, 2 * circle_radius + icon_height);

	int hotspot_x = circle_radius;
	int hotspot_y = icon_height + circle_radius;

	cr = cairo_create (surface);
	cairo_translate (cr, hotspot_x, hotspot_y);
	cairo_arc (cr, 0, 0, circle_radius, 0., 2 * M_PI);
	if (color) {
		cairo_set_source_rgb (cr, color->red, color->green, color->blue);
	} else {
		cairo_set_source_rgba (cr, 0., 0., 0., 0.5);
	}
	cairo_fill (cr);

	cairo_translate (cr, circle_radius, -circle_radius - icon_height);
	cairo_set_source_surface (cr, icon_surface, 0, 0);
	cairo_paint (cr);
	cairo_surface_flush (surface);
	cairo_destroy (cr);

	combined_pixbuf = pps_document_misc_pixbuf_from_surface (surface);

	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	texture = gdk_texture_new_for_pixbuf (combined_pixbuf);
	G_GNUC_END_IGNORE_DEPRECATIONS
	cursor = gdk_cursor_new_from_texture (texture, hotspot_x, hotspot_y, NULL);
	gtk_widget_set_cursor (GTK_WIDGET (draw), cursor);
}

static void
tool_properties_updated (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
	PpsAnnotationLayerInk *draw = PPS_ANNOTATION_LAYER_INK (user_data);
	update_cursor (draw);
}

static void
scale_updated (GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
	PpsAnnotationLayerInk *draw = PPS_ANNOTATION_LAYER_INK (user_data);
	tool_properties_updated (gobject, pspec, user_data);

	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (draw);

	for (GList *c = priv->ink_paths; c; c = g_list_next (c)) {
		InkPathDrawData *p = (InkPathDrawData *) c->data;
		create_path_data (draw, PPS_ANNOTATION_INK (p->annot), &p);
	}
}

static void
pps_annotation_layer_ink_snapshot_layer (PpsAnnotationLayerInk *draw, GtkSnapshot *snapshot, gboolean highlight)
{
	GList *ink_paths;
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (draw);
	GList *pending_points;
	graphene_rect_t bounds;
	int blend_mode = 0;

	g_assert (gtk_widget_compute_bounds (GTK_WIDGET (draw), GTK_WIDGET (draw), &bounds));

	gtk_snapshot_push_clip (snapshot, &bounds);

	if (highlight) {
		GdkRGBA white = { 1.0, 1.0, 1.0, 1.0 };
		/* For some reason there is some artefacts if we don't fill the background with white */
		gtk_snapshot_append_color (snapshot, &white, &bounds);
	}

	if ((pps_annotation_model_get_tool (GET_ANNOT_MODEL (draw)) == TOOL_HIGHLIGHT && highlight) ||
	    (pps_annotation_model_get_tool (GET_ANNOT_MODEL (draw)) != TOOL_HIGHLIGHT && !highlight)) {
		GskPathBuilder *builder;
		g_autoptr (GskPath) path;
		g_autoptr (GskStroke) stroke;
		GdkRGBA *stroke_color = gdk_rgba_copy (get_current_tool_color (draw));
		gdouble scale = pps_document_model_get_scale (GET_DOC_MODEL (draw));
		gdouble radius = get_current_tool_radius (draw);

		pending_points = priv->pending_draw;

		stroke = gsk_stroke_new (2 * scale * radius);

		builder = gsk_path_builder_new ();

		while (pending_points) {
			PpsPoint *i = pending_points->data;
			if (pending_points == priv->pending_draw) {
				gsk_path_builder_move_to (builder, i->x, i->y);
			} else {
				gsk_path_builder_line_to (builder, i->x, i->y);
			}
			pending_points = g_list_next (pending_points);
		}
		path = gsk_path_builder_free_to_path (builder);

		if (highlight) {
			GdkRGBA white = { 1.0, 1.0, 1.0, 1.0 };
			gtk_snapshot_push_blend (snapshot, GSK_BLEND_MODE_MULTIPLY);
			gtk_snapshot_append_color (snapshot, &white, &bounds);
			blend_mode++;
		}
		gtk_snapshot_append_stroke (snapshot, path, stroke, stroke_color);
		if (highlight) {
			gtk_snapshot_pop (snapshot);
		}
	}

	ink_paths = priv->ink_paths;
	while (ink_paths) {
		InkPathDrawData *ip = ink_paths->data;
		if (pps_annotation_ink_get_highlight (PPS_ANNOTATION_INK (ip->annot)) == highlight) {
			if (highlight) {
				GdkRGBA white = { 1.0, 1.0, 1.0, 1.0 };
				gtk_snapshot_push_blend (snapshot, GSK_BLEND_MODE_MULTIPLY);
				gtk_snapshot_append_color (snapshot, &white, &ip->bounds);
				blend_mode++;
			}
			gtk_snapshot_append_stroke (snapshot, ip->path, ip->stroke, &ip->color);
			if (highlight) {
				gtk_snapshot_pop (snapshot);
			}
		}

#ifdef PATH_DEBUG
		PpsAnnotationInk *ink = PPS_ANNOTATION_INK (ip->annot);
		gsize n_paths;
		PpsPath **ink_list = pps_ink_list_get_array (pps_annotation_ink_get_ink_list (ink), &n_paths);

		gdouble page_width, page_height;
		gdouble scale = pps_document_model_get_scale (GET_DOC_MODEL (draw));
		pps_document_get_page_size (GET_DOC (draw), GET_PAGE_INDEX (draw), &page_width, &page_height);
		for (int i = 0; i < n_paths; i++) {
			for (int j = 0; j < ink_list[i]->n_points; j++) {
				GdkRGBA black = { 0 };
				black.alpha = 0.5;
				PpsPoint p =
				    ink_list[i]->points[j];
				graphene_rect_t b = GRAPHENE_RECT_INIT (p.x * scale - 1, (page_height - p.y) * scale - 1, 3, 3);
				gtk_snapshot_append_color (snapshot, &black, &b);
			}
		}
#endif
		ink_paths = g_list_next (ink_paths);
	}

	for (int i = 0; i < blend_mode + 1; i++) {
		gtk_snapshot_pop (snapshot);
	}
}

static void
pps_annotation_layer_ink_snapshot (GtkWidget *w, GtkSnapshot *snapshot)
{
	pps_annotation_layer_ink_snapshot_layer (PPS_ANNOTATION_LAYER_INK (w), snapshot, FALSE);
}

void
pps_annotation_layer_ink_snapshot_below (PpsAnnotationLayerInk *draw, GtkSnapshot *snapshot)
{
	pps_annotation_layer_ink_snapshot_layer (draw, snapshot, TRUE);
}

static void
annotation_updated (PpsAnnotation *a, GParamSpec *pspec, PpsAnnotationLayerInk *ink)
{
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (ink);

	if (PPS_IS_ANNOTATION_INK (a)) {
		for (GList *c = priv->ink_paths; c; c = g_list_next (c)) {
			InkPathDrawData *ip = c->data;
			if (ip->annot == a) {
				create_path_data (ink, PPS_ANNOTATION_INK (a), &ip);
				gtk_widget_queue_draw (GTK_WIDGET (ink));
				break;
			}
		}
	}
}

static void
pps_annotation_layer_ink_annot_removed (PpsAnnotationLayer *layer, PpsAnnotation *a)
{
	PpsAnnotationLayerInk *ink = PPS_ANNOTATION_LAYER_INK (layer);
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (ink);

	if (PPS_IS_ANNOTATION_INK (a)) {
		for (GList *paths = priv->ink_paths; paths; paths = g_list_next (paths)) {
			InkPathDrawData *ip = paths->data;
			if (ip->annot == a) {
				priv->ink_paths = g_list_remove (priv->ink_paths, ip);
				g_signal_handlers_disconnect_by_func (a, G_CALLBACK (annotation_updated), layer);
				break;
			}
		}

		gtk_widget_queue_draw (GTK_WIDGET (layer));
	}
}

static bool
pps_annotation_layer_ink_annot_added (PpsAnnotationLayer *layer, PpsAnnotation *a)
{
	PpsAnnotationLayerInk *ink = PPS_ANNOTATION_LAYER_INK (layer);
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (ink);
	if (PPS_IS_ANNOTATION_INK (a)) {
		InkPathDrawData *path_data = NULL;
		create_path_data (ink, PPS_ANNOTATION_INK (a), &path_data);
		priv->ink_paths = g_list_append (priv->ink_paths, path_data);
		g_signal_connect_object (a, "notify::ink-list",
		                         G_CALLBACK (annotation_updated), layer,
		                         G_CONNECT_DEFAULT);
		gtk_widget_queue_draw (GTK_WIDGET (layer));
		return true;
	}
	return false;
}

static void
pps_annotation_layer_ink_clear_page (PpsAnnotationLayer *object)
{
	PpsAnnotationLayerInk *draw = PPS_ANNOTATION_LAYER_INK (object);
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (draw);
	GList *ink_paths = priv->ink_paths;

	while (ink_paths) {
		InkPathDrawData *ip = ink_paths->data;
		gsk_path_unref (ip->path);
		gsk_stroke_free (ip->stroke);
		g_signal_handlers_disconnect_by_func (ip->annot, G_CALLBACK (annotation_updated), object);

		g_free (ip);
		ink_paths = g_list_next (ink_paths);
	}
	priv->ink_paths = NULL;
}

static GObject *
pps_annotation_layer_ink_constructor (GType type,
                                      guint n_construct_properties,
                                      GObjectConstructParam *construct_params)
{
	GObject *object;
	PpsAnnotationLayerInk *draw;

	object = G_OBJECT_CLASS (pps_annotation_layer_ink_parent_class)
	             ->constructor (type, n_construct_properties, construct_params);
	draw = PPS_ANNOTATION_LAYER_INK (object);

	PpsAnnotationModel *annotation_model = GET_ANNOT_MODEL (draw);

	g_signal_connect (annotation_model, "notify::pen-radius", G_CALLBACK (tool_properties_updated), draw);
	g_signal_connect (annotation_model, "notify::highlight-radius", G_CALLBACK (tool_properties_updated), draw);
	g_signal_connect (annotation_model, "notify::eraser-radius", G_CALLBACK (tool_properties_updated), draw);
	g_signal_connect (annotation_model, "notify::pen-color", G_CALLBACK (tool_properties_updated), draw);
	g_signal_connect (annotation_model, "notify::highlight-color", G_CALLBACK (tool_properties_updated), draw);
	g_signal_connect (annotation_model, "notify::tool", G_CALLBACK (tool_properties_updated), draw);
	g_signal_connect (GET_DOC_MODEL (draw), "notify::scale", G_CALLBACK (scale_updated), draw);

	update_cursor (draw);

	return object;
}

static void
pps_annotation_layer_ink_dispose (GObject *object)
{
	PpsAnnotationLayerInk *draw = PPS_ANNOTATION_LAYER_INK (object);
	PpsAnnotationLayerInkPrivate *priv = INK_GET_PRIVATE (draw);
	GtkWidget *widget = GTK_WIDGET (draw);
	GtkWidget *child = gtk_widget_get_first_child (widget);

	while (child != NULL) {
		GtkWidget *next_child = gtk_widget_get_next_sibling (child);
		gtk_widget_unparent (child);
		child = next_child;
	}

	pps_annotation_layer_ink_clear_page (PPS_ANNOTATION_LAYER (draw));

	g_signal_handlers_disconnect_by_func (GET_DOC_MODEL (draw),
	                                      G_CALLBACK (scale_updated), object);
	g_signal_handlers_disconnect_by_func (GET_ANNOT_MODEL (draw),
	                                      G_CALLBACK (tool_properties_updated), object);

	g_clear_pointer (&priv->eraser_pixbuf, cairo_surface_destroy);
	g_clear_pointer (&priv->pencil_pixbuf, cairo_surface_destroy);
	g_clear_pointer (&priv->highlight_pixbuf, cairo_surface_destroy);

	G_OBJECT_CLASS (pps_annotation_layer_ink_parent_class)->dispose (object);
}

static void
pps_annotation_layer_ink_init (PpsAnnotationLayerInk *draw)
{
	GtkGesture *gesture = gtk_gesture_drag_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 1);
	g_signal_connect (gesture, "drag-update",
	                  G_CALLBACK (drag_update), draw);
	g_signal_connect (gesture, "drag-begin",
	                  G_CALLBACK (drag_begin), draw);
	g_signal_connect (gesture, "drag-end",
	                  G_CALLBACK (drag_end), draw);

	gtk_widget_add_controller (GTK_WIDGET (draw), GTK_EVENT_CONTROLLER (gesture));

	gtk_widget_set_size_request (GTK_WIDGET (draw), 100, 100);
}

static void
pps_annotation_layer_ink_class_init (PpsAnnotationLayerInkClass *klass)
{
	GObjectClass *g_object_class = G_OBJECT_CLASS (klass);

	g_object_class->constructor = pps_annotation_layer_ink_constructor;
	g_object_class->dispose = pps_annotation_layer_ink_dispose;
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS (klass);
	wclass->snapshot = pps_annotation_layer_ink_snapshot;

	PpsAnnotationLayerClass *lclass = PPS_ANNOTATION_LAYER_CLASS (klass);
	lclass->annot_added = pps_annotation_layer_ink_annot_added;
	lclass->annot_removed = pps_annotation_layer_ink_annot_removed;
	lclass->clear_page = pps_annotation_layer_ink_clear_page;
}

GtkWidget *
pps_annotation_layer_ink_new (PpsDocument *document,
                              PpsDocumentModel *model,
                              PpsAnnotationsContext *annotations_context)
{
	g_return_val_if_fail (PPS_IS_DOCUMENT (document), NULL);

	return g_object_new (PPS_TYPE_ANNOTATION_LAYER_INK,
	                     "model",
	                     model,
	                     "annotations-context",
	                     annotations_context,
	                     NULL);
}
