// SPDX-License-Identifier: GPL-2.0-or-later
/* this file is part of papers, a gnome document viewer
 *
 *  Copyright (C) 2004 Red Hat, Inc
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <adwaita.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "pps-document-forms.h"
#include "pps-document-images.h"
#include "pps-document-layers.h"
#include "pps-document-links.h"
#include "pps-document-media.h"
#include "pps-document-misc.h"
#include "pps-mapping-list.h"

#include "pps-overlay.h"
#include "pps-view-private.h"
#include "pps-view.h"

#include "pps-annotation-window.h"
#include "pps-colors.h"
#include "pps-document-annotations.h"
#include "pps-form-field-private.h"
#include "pps-page-cache.h"
#include "pps-pixbuf-cache.h"
#include "pps-view-marshal.h"
#include "pps-view-page.h"

#include "factory/pps-annotation-widget-factory.h"
#include "factory/pps-form-widget-factory.h"
#include "pps-annotation-overlay.h"

#if HAVE_LIBSPELLING
#include <libspelling.h>
#endif

enum {
	SIGNAL_SCROLL,
	SIGNAL_HANDLE_LINK,
	SIGNAL_EXTERNAL_LINK,
	SIGNAL_POPUP_MENU,
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_LAYERS_CHANGED,
	SIGNAL_MOVE_CURSOR,
	SIGNAL_CURSOR_MOVED,
	SIGNAL_ACTIVATE,
	SIGNAL_SIGNATURE_RECT,
	N_SIGNALS
};

enum {
	PROP_0,
	PROP_HADJUSTMENT,
	PROP_VADJUSTMENT,
	PROP_HSCROLL_POLICY,
	PROP_VSCROLL_POLICY,
	PROP_CAN_ZOOM_IN,
	PROP_CAN_ZOOM_OUT,
	PROP_ANNOTATIONS_CONTEXT,
	PROP_MODEL,
	PROP_SEARCH_CONTEXT,
};

static guint signals[N_SIGNALS];

typedef enum {
	PPS_VIEW_FIND_NEXT,
	PPS_VIEW_FIND_PREV
} PpsViewFindDirection;

typedef struct
{
	/* Document */
	guint page;
	PpsRectangle doc_rect;
} PpsViewChild;

#define MIN_SCALE 0.05409 /* large documents (comics) need a small value, see #702 */
#define ZOOM_IN_FACTOR 1.2
#define ZOOM_OUT_FACTOR (1.0 / ZOOM_IN_FACTOR)

#define PAGE_WIDGET_POOL_SIZE 25

#define SCROLL_TIME 150
#define SCROLL_PAGE_THRESHOLD 0.7
#define SCROLL_THRESHOLD 5

#define DEFAULT_PIXBUF_CACHE_SIZE 52428800 /* 50MB */

#define ANNOT_POPUP_WINDOW_DEFAULT_WIDTH 200
#define ANNOT_POPUP_WINDOW_DEFAULT_HEIGHT 150

#define LINK_PREVIEW_PAGE_RATIO 1.0 / 3.0    /* Size of popover with respect to page size */
#define LINK_PREVIEW_HORIZONTAL_LINK_POS 0.5 /* as fraction of preview width */
#define LINK_PREVIEW_VERTICAL_LINK_POS 0.3   /* as fraction of preview height */
#define LINK_PREVIEW_DELAY_MS 300            /* Delay before showing preview in milliseconds */

#define BUTTON_MODIFIER_MASK (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK | GDK_BUTTON4_MASK | GDK_BUTTON5_MASK)

#define PPS_STYLE_CLASS_INVERTED "inverted"

/*** Geometry computations ***/
static void get_page_y_offset (PpsView *view,
                               int page,
                               int *y_offset);
static void find_page_at_location (PpsView *view,
                                   gdouble x,
                                   gdouble y,
                                   gint *page,
                                   gint *x_offset,
                                   gint *y_offset);
static void transform_view_rect_to_doc_rect (PpsView *view,
                                             GdkRectangle *view_rect,
                                             GdkRectangle *page_area,
                                             PpsRectangle *doc_rect);
static void transform_page_point_to_view_point (PpsView *view,
                                                int page,
                                                PpsPoint *doc_point,
                                                gdouble *view_point_x,
                                                gdouble *view_point_y);

/*** Hyperrefs ***/
static PpsLink *pps_view_get_link_at_location (PpsView *view,
                                               gdouble x,
                                               gdouble y);
static char *tip_from_link (PpsView *view,
                            PpsLink *link);
static void pps_view_link_preview_popover_cleanup (PpsView *view);
static void get_link_area (PpsView *view,
                           gint page,
                           PpsLink *link,
                           GdkRectangle *area);
static void link_preview_set_thumbnail (GdkTexture *page_surface,
                                        PpsView *view);
static void link_preview_job_finished_cb (PpsJobThumbnailTexture *job,
                                          PpsView *view);
static void link_preview_delayed_show (PpsView *view);

/*** Media ***/
static PpsMedia *pps_view_get_media_at_location (PpsView *view,
                                                 gdouble x,
                                                 gdouble y);
static gboolean pps_view_find_player_for_media (PpsView *view,
                                                PpsMedia *media);

static PpsAnnotation *get_annotation_at_location (PpsView *view,
                                                  gdouble x,
                                                  gdouble y);
static void show_annotation_windows (PpsView *view,
                                     gint page);
static void hide_annotation_windows (PpsView *view,
                                     gint page);

/*** Drawing ***/
static void draw_surface (GtkSnapshot *snapshot,
                          GdkTexture *texture,
                          const graphene_point_t *point,
                          const graphene_rect_t *area,
                          gboolean inverted);
static void pps_view_reload_page (PpsView *view,
                                  gint page,
                                  cairo_region_t *region);
/*** Callbacks ***/
static void pps_view_scroll_to_page (PpsView *view,
                                     gint page);
static void pps_view_page_changed_cb (PpsDocumentModel *model,
                                      gint old_page,
                                      gint new_page,
                                      PpsView *view);
static void pps_interrupt_scroll_animation_cb (GtkAdjustment *adjustment,
                                               PpsView *view);
/*** Zoom and sizing ***/
static double zoom_for_size_fit_width (gdouble doc_width,
                                       gdouble doc_height,
                                       int target_width,
                                       int target_height);
static double zoom_for_size_fit_height (gdouble doc_width,
                                        gdouble doc_height,
                                        int target_width,
                                        int target_height);
static double zoom_for_size_fit_page (gdouble doc_width,
                                      gdouble doc_height,
                                      int target_width,
                                      int target_height);
static double zoom_for_size_automatic (GtkWidget *widget,
                                       gdouble doc_width,
                                       gdouble doc_height,
                                       int target_width,
                                       int target_height);
static gboolean pps_view_can_zoom (PpsView *view,
                                   gdouble factor);
static void pps_view_zoom (PpsView *view,
                           gdouble factor);
static void pps_view_zoom_for_size (PpsView *view,
                                    int width,
                                    int height);
static void pps_view_zoom_for_size_continuous_and_dual_page (PpsView *view,
                                                             int width,
                                                             int height);
static void pps_view_zoom_for_size_continuous (PpsView *view,
                                               int width,
                                               int height);
static void pps_view_zoom_for_size_dual_page (PpsView *view,
                                              int width,
                                              int height);
static void pps_view_zoom_for_size_single_page (PpsView *view,
                                                int width,
                                                int height);
static gboolean pps_view_page_fits (PpsView *view,
                                    GtkOrientation orientation);
/*** Cursors ***/
static void pps_view_set_cursor (PpsView *view,
                                 PpsViewCursor new_cursor);

/*** Selection ***/
static void compute_selections (PpsView *view,
                                PpsSelectionStyle style,
                                gdouble start_x,
                                gdouble start_y,
                                gdouble stop_x,
                                gdouble stop_y);
static void clear_selection (PpsView *view);
static gboolean
get_selection_page_range (PpsView *view,
                          graphene_point_t *start,
                          graphene_point_t *stop,
                          gint *first_page,
                          gint *last_page);

/*** Caret navigation ***/
static void pps_view_check_cursor_blink (PpsView *pps_view);

/*** Signatures ***/
static void pps_view_stop_signature_rect (PpsView *view);

static void pps_view_update_primary_selection (PpsView *view);

G_DEFINE_TYPE_WITH_CODE (PpsView, pps_view, GTK_TYPE_WIDGET, G_ADD_PRIVATE (PpsView) G_IMPLEMENT_INTERFACE (GTK_TYPE_SCROLLABLE, NULL))

#define GET_PRIVATE(o) pps_view_get_instance_private (o)

/* HeightToPage cache */
#define PPS_HEIGHT_TO_PAGE_CACHE_KEY "pps-height-to-page-cache"

static void
pps_view_build_height_to_page_cache (PpsView *view,
                                     PpsHeightToPageCache *cache)
{
	gboolean swap, uniform;
	int i;
	double uniform_height, page_height, next_page_height;
	double saved_height;
	gdouble u_width, u_height;
	gint n_pages;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	gint rotation = pps_document_model_get_rotation (priv->model);

	swap = (rotation == 90 || rotation == 270);

	uniform = pps_document_is_page_size_uniform (document);
	n_pages = pps_document_get_n_pages (document);

	g_free (cache->height_to_page);
	g_free (cache->dual_height_to_page);

	cache->rotation = rotation;
	cache->dual_even_left = !pps_document_model_get_dual_page_odd_pages_left (priv->model);
	cache->height_to_page = g_new0 (gdouble, n_pages + 1);
	cache->dual_height_to_page = g_new0 (gdouble, n_pages + 2);

	if (uniform)
		pps_document_get_page_size (document, 0, &u_width, &u_height);

	saved_height = 0;
	for (i = 0; i <= n_pages; i++) {
		if (uniform) {
			uniform_height = swap ? u_width : u_height;
			cache->height_to_page[i] = i * uniform_height;
		} else {
			if (i < n_pages) {
				gdouble w, h;

				pps_document_get_page_size (document, i, &w, &h);
				page_height = swap ? w : h;
			} else {
				page_height = 0;
			}
			cache->height_to_page[i] = saved_height;
			saved_height += page_height;
		}
	}

	if (cache->dual_even_left && !uniform) {
		gdouble w, h;

		pps_document_get_page_size (document, 0, &w, &h);
		saved_height = swap ? w : h;
	} else {
		saved_height = 0;
	}

	for (i = cache->dual_even_left; i < n_pages + 2; i += 2) {
		if (uniform) {
			uniform_height = swap ? u_width : u_height;
			cache->dual_height_to_page[i] = ((i + cache->dual_even_left) / 2) * uniform_height;
			if (i + 1 < n_pages + 2)
				cache->dual_height_to_page[i + 1] = ((i + cache->dual_even_left) / 2) * uniform_height;
		} else {
			if (i + 1 < n_pages) {
				gdouble w, h;

				pps_document_get_page_size (document, i + 1, &w, &h);
				next_page_height = swap ? w : h;
			} else {
				next_page_height = 0;
			}

			if (i < n_pages) {
				gdouble w, h;

				pps_document_get_page_size (document, i, &w, &h);
				page_height = swap ? w : h;
			} else {
				page_height = 0;
			}

			if (i + 1 < n_pages + 2) {
				cache->dual_height_to_page[i] = saved_height;
				cache->dual_height_to_page[i + 1] = saved_height;
				saved_height += MAX (page_height, next_page_height);
			} else {
				cache->dual_height_to_page[i] = saved_height;
			}
		}
	}
}

static void
pps_height_to_page_cache_free (PpsHeightToPageCache *cache)
{
	g_clear_pointer (&cache->height_to_page, g_free);
	g_clear_pointer (&cache->dual_height_to_page, g_free);
	g_free (cache);
}

static PpsHeightToPageCache *
pps_view_get_height_to_page_cache (PpsView *view)
{
	PpsHeightToPageCache *cache;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!document)
		return NULL;

	cache = g_object_get_data (G_OBJECT (document), PPS_HEIGHT_TO_PAGE_CACHE_KEY);
	if (!cache) {
		cache = g_new0 (PpsHeightToPageCache, 1);
		pps_view_build_height_to_page_cache (view, cache);
		g_object_set_data_full (G_OBJECT (document),
		                        PPS_HEIGHT_TO_PAGE_CACHE_KEY,
		                        cache,
		                        (GDestroyNotify) pps_height_to_page_cache_free);
	}

	return cache;
}

static void
pps_view_get_height_to_page (PpsView *view,
                             gint page,
                             gint *height,
                             gint *dual_height)
{
	PpsHeightToPageCache *cache = NULL;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble h, dh, scale;

	scale = pps_document_model_get_scale (priv->model);

	if (!priv->height_to_page_cache)
		return;

	cache = priv->height_to_page_cache;
	if (cache->rotation != pps_document_model_get_rotation (priv->model) ||
	    cache->dual_even_left != !pps_document_model_get_dual_page_odd_pages_left (priv->model)) {
		pps_view_build_height_to_page_cache (view, cache);
	}

	if (height) {
		h = cache->height_to_page[page];
		*height = (gint) (h * scale + 0.5);
	}

	if (dual_height) {
		dh = cache->dual_height_to_page[page];
		*dual_height = (gint) (dh * scale + 0.5);
	}
}

static gboolean
is_dual_page (PpsView *view,
              gboolean *odd_left_out)
{
	gboolean dual = FALSE;
	gboolean odd_left = FALSE;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	switch (pps_document_model_get_page_layout (priv->model)) {
	case PPS_PAGE_LAYOUT_AUTOMATIC: {
		double scale;
		double doc_width;
		double doc_height;

		scale = pps_document_misc_get_widget_dpi (GTK_WIDGET (view)) / 72.0;

		pps_document_get_max_page_size (document, &doc_width, &doc_height);

		/* If the width is ok and the height is pretty close, try to fit it in */
		if (pps_document_get_n_pages (document) > 1 &&
		    doc_width < doc_height &&
		    gtk_widget_get_width (GTK_WIDGET (view)) > (2 * doc_width * scale) &&
		    gtk_widget_get_height (GTK_WIDGET (view)) > (doc_height * scale * 0.9)) {
			odd_left = pps_document_model_get_dual_page_odd_pages_left (priv->model);
			dual = TRUE;
		}
	} break;
	case PPS_PAGE_LAYOUT_DUAL:
		odd_left = pps_document_model_get_dual_page_odd_pages_left (priv->model);
		if (pps_document_get_n_pages (document) > 1)
			dual = TRUE;
		break;
	case PPS_PAGE_LAYOUT_SINGLE:
		break;
	default:
		g_assert_not_reached ();
	}

	if (odd_left_out)
		*odd_left_out = odd_left;

	return dual;
}

/*
 * Get the page paired with a given page in dual page mode.
 *
 * Returns false if the view isn't in dual page mode.
 */
static gboolean
get_dual_page_other_page (PpsView *view,
                          gint page,
                          gint *other_page_out,
                          gboolean *odd_left_out)
{
	gboolean odd_left;

	if (!is_dual_page (view, &odd_left))
		return FALSE;

	if (other_page_out)
		*other_page_out = (page % 2 == !odd_left) ? page + 1 : page - 1;

	if (odd_left_out)
		*odd_left_out = odd_left;

	return TRUE;
}

static void
scroll_to_view_point (PpsView *view,
                      gdouble x,
                      gdouble y)
{
	gdouble page_size;
	gdouble upper, lower;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	page_size = gtk_adjustment_get_page_size (priv->vadjustment);
	upper = gtk_adjustment_get_upper (priv->vadjustment);
	lower = gtk_adjustment_get_lower (priv->vadjustment);

	if (pps_document_model_get_continuous (priv->model)) {
		gtk_adjustment_clamp_page (priv->vadjustment,
		                           y, y + page_size);
	} else {
		gtk_adjustment_set_value (priv->vadjustment,
		                          CLAMP (y, lower, upper - page_size));
	}

	page_size = gtk_adjustment_get_page_size (priv->hadjustment);
	upper = gtk_adjustment_get_upper (priv->hadjustment);
	lower = gtk_adjustment_get_lower (priv->hadjustment);

	if (is_dual_page (view, NULL)) {
		gtk_adjustment_clamp_page (priv->hadjustment, x,
		                           x + page_size);
	} else {
		gtk_adjustment_set_value (priv->hadjustment,
		                          CLAMP (x, lower, upper - page_size));
	}
}

static void
pps_view_queue_rescroll_to_current_page (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->needs_scrolling_to_current_page = TRUE;
	gtk_widget_queue_allocate (GTK_WIDGET (view));
}

static void
pps_view_adjustment_to_page_position (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GdkRectangle page_area, visible_area;
	gdouble x, y;

	pps_view_get_page_extents (view, priv->current_page, &page_area);
	x = MAX (0, page_area.x - priv->spacing);
	y = MAX (0, page_area.y - priv->spacing);

	visible_area.height = gtk_widget_get_height (GTK_WIDGET (view));
	visible_area.width = gtk_widget_get_width (GTK_WIDGET (view));
	visible_area.y = gtk_adjustment_get_value (priv->vadjustment);
	visible_area.x = gtk_adjustment_get_value (priv->hadjustment);

	if (!priv->keep_scroll_of_current_page || !gdk_rectangle_intersect (&page_area, &visible_area, NULL)) {
		scroll_to_view_point (view, x, y);
	}
	priv->keep_scroll_of_current_page = FALSE;
}

static void
pps_view_scroll_to_doc_point (PpsView *view, PpsDocumentPoint *doc_point)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble x, y;

	transform_page_point_to_view_point (view, doc_point->page_index, &doc_point->point_on_page, &x, &y);
	priv->current_page = doc_point->page_index;
	scroll_to_view_point (view, x, y);

	/*
	 * When the adjustments' values don't change due to multiple doc points
	 * mapped to the same view point, which is possible with non-continuous
	 * mode, we still want to re-allocate.
	 */
	gtk_widget_queue_allocate (GTK_WIDGET (view));
}

static void
pps_view_update_adjustment_value (PpsView *view,
                                  GtkOrientation orientation)
{
	GtkAdjustment *adjustment;
	gint req_size, alloc_size;
	gdouble page_size, value, new_value, upper, factor, zoom_center;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (orientation == GTK_ORIENTATION_HORIZONTAL) {
		req_size = priv->requisition.width;
		alloc_size = gtk_widget_get_width (GTK_WIDGET (view));
		adjustment = priv->hadjustment;
		zoom_center = priv->zoom_center_x;
	} else {
		req_size = priv->requisition.height;
		alloc_size = gtk_widget_get_height (GTK_WIDGET (view));
		adjustment = priv->vadjustment;
		zoom_center = priv->zoom_center_y;
	}

	factor = 1.0;
	value = gtk_adjustment_get_value (adjustment);
	upper = gtk_adjustment_get_upper (adjustment);
	page_size = gtk_adjustment_get_page_size (adjustment);
	if (zoom_center < 0)
		zoom_center = page_size * 0.5;

	if (upper != .0) {
		switch (priv->pending_scroll) {
		case SCROLL_TO_KEEP_POSITION:
			factor = value / upper;
			break;
		case SCROLL_TO_CENTER:
			factor = (value + zoom_center) / upper;
			break;
		}
	}

	upper = MAX (alloc_size, req_size);
	page_size = alloc_size;

	switch (priv->pending_scroll) {
	case SCROLL_TO_KEEP_POSITION:
		if (adw_animation_get_state (priv->scroll_animation_vertical) != ADW_ANIMATION_PLAYING && adw_animation_get_state (priv->scroll_animation_horizontal) != ADW_ANIMATION_PLAYING && !priv->keep_scroll_of_current_page) {
			new_value = CLAMP (upper * factor, 0, upper - page_size);
		} else {
			new_value = value;
		}
		break;
	case SCROLL_TO_CENTER:
		new_value = CLAMP (upper * factor - zoom_center, 0, upper - page_size);
		if (orientation == GTK_ORIENTATION_HORIZONTAL)
			priv->zoom_center_x = -1.0;
		else
			priv->zoom_center_y = -1.0;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	gtk_adjustment_configure (adjustment, new_value, 0, upper,
	                          alloc_size * 0.1, alloc_size * 0.9, page_size);
}

static void
pps_view_update_adjustment_values (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	pps_view_update_adjustment_value (view, GTK_ORIENTATION_HORIZONTAL);
	pps_view_update_adjustment_value (view, GTK_ORIENTATION_VERTICAL);

	if (priv->needs_scrolling_to_current_page) {
		pps_view_adjustment_to_page_position (view);
		priv->needs_scrolling_to_current_page = FALSE;
	}
}

static void
view_update_range_and_current_page (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	gint start = priv->start_page;
	gint end = priv->end_page;
	gboolean odd_left;

	if (pps_document_get_n_pages (document) <= 0 ||
	    !pps_document_check_dimensions (document))
		return;

	if (pps_document_model_get_continuous (priv->model)) {
		GdkRectangle current_area, unused, page_area;
		gboolean found = FALSE;
		gint area_max = -1, area;
		gint best_current_page = -1;
		gint n_pages;
		int i, j = 0;

		if (!(priv->vadjustment && priv->hadjustment))
			return;

		current_area.x = gtk_adjustment_get_value (priv->hadjustment);
		current_area.width = gtk_adjustment_get_page_size (priv->hadjustment);
		current_area.y = gtk_adjustment_get_value (priv->vadjustment);
		current_area.height = gtk_adjustment_get_page_size (priv->vadjustment);

		n_pages = pps_document_get_n_pages (document);
		for (i = 0; i < n_pages; i++) {

			pps_view_get_page_extents (view, i, &page_area);

			if (gdk_rectangle_intersect (&current_area, &page_area, &unused)) {
				area = unused.width * unused.height;

				if (!found) {
					area_max = area;
					priv->start_page = i;
					found = TRUE;
					best_current_page = i;
				}
				if (area > area_max) {
					best_current_page = (area == area_max) ? MIN (i, best_current_page) : i;
					area_max = area;
				}

				priv->end_page = i;
				j = 0;
			} else if (found && priv->current_page <= priv->end_page) {
				if (is_dual_page (view, NULL) && j < 1) {
					/* In dual mode  we stop searching
					 * after two consecutive non-visible pages.
					 */
					j++;
					continue;
				}
				break;
			}
		}

		if (priv->pending_scroll == SCROLL_TO_KEEP_POSITION) {
			best_current_page = MAX (best_current_page, priv->start_page);

			if (best_current_page >= 0 && priv->current_page != best_current_page) {
				priv->current_page = best_current_page;
				pps_document_model_set_page (priv->model, best_current_page);
				if (!priv->caret_enabled)
					priv->cursor_page = best_current_page;
			}
		}
	} else if (is_dual_page (view, &odd_left)) {
		if (priv->current_page % 2 == !odd_left) {
			priv->start_page = priv->current_page;
			if (priv->current_page + 1 < pps_document_get_n_pages (document))
				priv->end_page = priv->start_page + 1;
			else
				priv->end_page = priv->start_page;
		} else {
			if (priv->current_page < 1)
				priv->start_page = priv->current_page;
			else
				priv->start_page = priv->current_page - 1;
			priv->end_page = priv->current_page;
		}
	} else {
		priv->start_page = priv->current_page;
		priv->end_page = priv->current_page;
	}

	if (priv->start_page == -1 || priv->end_page == -1)
		return;

	if (start < priv->start_page || end > priv->end_page) {
		pps_view_check_cursor_blink (view);
	}

	// Change window annot state
	for (gint i = start; i < priv->start_page && start != -1; i++) {
		hide_annotation_windows (view, i);
	}
	for (gint i = priv->start_page; i <= priv->end_page; i++) {
		show_annotation_windows (view, i);
	}
	for (gint i = priv->end_page + 1; i <= end && end != -1; i++) {
		hide_annotation_windows (view, i);
	}
	pps_page_cache_set_page_range (priv->page_cache,
	                               priv->start_page,
	                               priv->end_page);
	pps_pixbuf_cache_set_page_range (priv->pixbuf_cache,
	                                 priv->start_page,
	                                 priv->end_page,
	                                 priv->selection_info.selections);

	for (gint i = 0; i < (priv->end_page + 1 - priv->start_page) - (gint) priv->page_widgets->len; i++) {
		PpsViewPage *page = pps_view_page_new ();

		gtk_widget_set_parent (GTK_WIDGET (page), GTK_WIDGET (view));
		pps_view_page_setup (page, priv->model, priv->annots_context,
		                     priv->search_context, priv->page_cache,
		                     priv->pixbuf_cache);

		g_ptr_array_add (priv->page_widgets, page);
	}

	guint start_widget = priv->start_page % priv->page_widgets->len;
	guint end_widget = priv->end_page % priv->page_widgets->len;

	for (guint i = 0; i < priv->page_widgets->len; i++) {
		PpsViewPage *page = g_ptr_array_index (priv->page_widgets, i);
		gint page_index = -1;

		if (start_widget <= end_widget) {
			if (start_widget <= i && i <= end_widget)
				page_index = priv->start_page - start_widget + i;
		} else {
			if (i <= end_widget)
				page_index = priv->end_page - end_widget + i;
			else if (start_widget <= i)
				page_index = priv->start_page - start_widget + i;
		}

		if (page_index != -1) {
			pps_view_page_set_page (page, page_index);
		}
	}

	/* make sure the correct page keeps focus */
	if (gtk_widget_is_focus (GTK_WIDGET (view)) || gtk_widget_get_focus_child (GTK_WIDGET (view))) {
		gtk_widget_grab_focus (GTK_WIDGET (view));
	}
}

static void
pps_view_set_scroll_adjustment (PpsView *view,
                                GtkOrientation orientation,
                                GtkAdjustment *adjustment)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkAdjustment **to_set;
	const gchar *prop_name;

	if (orientation == GTK_ORIENTATION_HORIZONTAL) {
		to_set = &priv->hadjustment;
		prop_name = "hadjustment";
	} else {
		to_set = &priv->vadjustment;
		prop_name = "vadjustment";
	}

	if (adjustment && adjustment == *to_set)
		return;

	if (*to_set) {
		g_signal_handlers_disconnect_by_data (*to_set, view);
		g_object_unref (*to_set);
	}

	if (!adjustment)
		adjustment = gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

	g_signal_connect_swapped (adjustment, "value-changed",
	                          G_CALLBACK (gtk_widget_queue_allocate),
	                          view);
	g_signal_connect (adjustment, "value-changed",
	                  G_CALLBACK (pps_interrupt_scroll_animation_cb),
	                  view);

	*to_set = g_object_ref_sink (adjustment);

	g_object_notify (G_OBJECT (view), prop_name);
}

static void
get_scroll_offset (PpsView *view,
                   guint *scroll_x,
                   guint *scroll_y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (scroll_x != NULL)
		*scroll_x = (guint) gtk_adjustment_get_value (priv->hadjustment);

	if (scroll_y != NULL)
		*scroll_y = (guint) gtk_adjustment_get_value (priv->vadjustment);
}

static void
add_scroll_binding_keypad (GtkWidgetClass *widget_class,
                           guint keyval,
                           GdkModifierType modifiers,
                           GtkScrollType scroll,
                           GtkOrientation orientation)
{
	guint keypad_keyval = keyval - GDK_KEY_Left + GDK_KEY_KP_Left;

	gtk_widget_class_add_binding_signal (widget_class, keyval, modifiers,
	                                     "scroll", "(ii)", scroll, orientation);
	gtk_widget_class_add_binding_signal (widget_class, keypad_keyval, modifiers,
	                                     "scroll", "(ii)", scroll, orientation);
}

static gdouble
compute_scroll_increment (PpsView *view,
                          GtkScrollType scroll)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	GtkAdjustment *adjustment = priv->vadjustment;
	cairo_region_t *text_region, *region;
	gint page;
	GdkRectangle rect;
	PpsRectangle doc_rect;
	GdkRectangle page_area;
	gdouble fraction = 1.0;
	guint scroll_x, scroll_y;

	get_scroll_offset (view, &scroll_x, &scroll_y);

	if (scroll != GTK_SCROLL_PAGE_BACKWARD && scroll != GTK_SCROLL_PAGE_FORWARD)
		return gtk_adjustment_get_page_size (adjustment);

	page = scroll == GTK_SCROLL_PAGE_BACKWARD ? priv->start_page : priv->end_page;

	text_region = pps_page_cache_get_text_mapping (priv->page_cache, page);
	if (!text_region || cairo_region_is_empty (text_region))
		return gtk_adjustment_get_page_size (adjustment);

	pps_view_get_page_extents (view, page, &page_area);
	rect.x = page_area.x + scroll_x;
	rect.y = scroll_y + (scroll == GTK_SCROLL_PAGE_BACKWARD ? 5 : gtk_widget_get_height (GTK_WIDGET (view)) - 5);
	rect.width = page_area.width;
	rect.height = 1;
	transform_view_rect_to_doc_rect (view, &rect, &page_area, &doc_rect);

	/* Convert the doc rectangle into a GdkRectangle */
	rect.x = doc_rect.x1;
	rect.y = doc_rect.y1;
	rect.width = doc_rect.x2 - doc_rect.x1;
	rect.height = MAX (1, doc_rect.y2 - doc_rect.y1);
	region = cairo_region_create_rectangle (&rect);

	cairo_region_intersect (region, text_region);
	if (cairo_region_num_rectangles (region)) {
		PpsRenderContext *rc;
		PpsPage *pps_page;
		cairo_region_t *sel_region;

		cairo_region_get_rectangle (region, 0, &rect);
		pps_page = pps_document_get_page (document, page);
		rc = pps_render_context_new (pps_page, pps_document_model_get_rotation (priv->model), 0., PPS_RENDER_ANNOTS_ALL);
		pps_render_context_set_target_size (rc,
		                                    page_area.width,
		                                    page_area.height);
		g_object_unref (pps_page);
		/* Get the selection region to know the height of the line */
		doc_rect.x1 = doc_rect.x2 = rect.x + 0.5;
		doc_rect.y1 = doc_rect.y2 = rect.y + 0.5;

		sel_region = pps_selection_get_selection_region (PPS_SELECTION (document),
		                                                 rc, PPS_SELECTION_STYLE_LINE,
		                                                 &doc_rect);

		g_object_unref (rc);

		if (cairo_region_num_rectangles (sel_region) > 0) {
			cairo_region_get_rectangle (sel_region, 0, &rect);
			fraction = 1 - (rect.height / gtk_adjustment_get_page_size (adjustment));
			/* jump the full page height if the line is too large a
			 * fraction of the page */
			if (fraction < SCROLL_PAGE_THRESHOLD)
				fraction = 1.0;
		}
		cairo_region_destroy (sel_region);
	}
	cairo_region_destroy (region);

	return gtk_adjustment_get_page_size (adjustment) * fraction;
}

static void
pps_view_first_page (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	pps_document_model_set_page (priv->model, 0);
}

static void
pps_view_last_page (PpsView *view)
{
	gint n_pages;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!document)
		return;

	n_pages = pps_document_get_n_pages (document);
	if (n_pages <= 1)
		return;

	pps_document_model_set_page (priv->model, n_pages - 1);
}

static void
pps_scroll_vertical_animation_cb (gdouble progress, PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	priv->pending_scroll_animation = TRUE;
	gtk_adjustment_set_value (priv->vadjustment, progress);
	priv->pending_scroll_animation = FALSE;
}

static void
pps_scroll_horizontal_animation_cb (gdouble progress, PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	priv->pending_scroll_animation = TRUE;
	gtk_adjustment_set_value (priv->hadjustment, progress);
	priv->pending_scroll_animation = FALSE;
}

static void
pps_interrupt_scroll_animation_cb (GtkAdjustment *adjustment, PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	if (!priv->pending_scroll_animation) {
		adw_animation_pause (priv->scroll_animation_vertical);
		adw_animation_pause (priv->scroll_animation_horizontal);
	}
}

static void
pps_view_cancel_kinetic_scrolling_deceleration (PpsView *view)
{
	/* Workaround for GTK issue https://gitlab.gnome.org/GNOME/gtk/-/issues/187:
	 * If we update the view (zoom/scale) while scrolling is still active we'd see
	 * a sudden jump of scroll position due to kinetic deceleration still using
	 * previous/outdated value.
	 * Unfortunately gtk_scrolled_window_cancel_deceleration is private so we need
	 * to workaround that by using public gtk_scrolled_window_set_kinetic_scrolling
	 * which calls gtk_scrolled_window_cancel_deceleration for us.
	 */
	GtkWidget *parent_window = gtk_widget_get_parent (GTK_WIDGET (view));

	if (GTK_IS_SCROLLED_WINDOW (parent_window) &&
	    gtk_scrolled_window_get_kinetic_scrolling (GTK_SCROLLED_WINDOW (parent_window))) {
		gtk_scrolled_window_set_kinetic_scrolling (GTK_SCROLLED_WINDOW (parent_window), FALSE);
		gtk_scrolled_window_set_kinetic_scrolling (GTK_SCROLLED_WINDOW (parent_window), TRUE);
	}
}

static gboolean
pps_view_scroll (PpsView *view,
                 GtkScrollType scroll,
                 GtkOrientation orientation)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkAdjustment *adjustment;
	gdouble increment, upper, lower, page_size, step_increment, prev_value, new_value;
	gboolean first_page = FALSE, last_page = FALSE;
	AdwAnimation *animation;

	if (priv->key_binding_handled || priv->caret_enabled)
		return FALSE;

	if (pps_view_page_fits (view, orientation)) {
		switch (scroll) {
		case GTK_SCROLL_PAGE_BACKWARD:
		case GTK_SCROLL_STEP_BACKWARD:
			pps_view_previous_page (view);
			break;
		case GTK_SCROLL_PAGE_FORWARD:
		case GTK_SCROLL_STEP_FORWARD:
			pps_view_next_page (view);
			break;
		case GTK_SCROLL_START:
			pps_view_first_page (view);
			break;
		case GTK_SCROLL_END:
			pps_view_last_page (view);
			break;
		default:
			break;
		}
		return TRUE;
	}

	/* Assign values for increment and vertical adjustment */
	adjustment = orientation == GTK_ORIENTATION_HORIZONTAL ? priv->hadjustment : priv->vadjustment;
	animation = orientation == GTK_ORIENTATION_HORIZONTAL ? priv->scroll_animation_horizontal : priv->scroll_animation_vertical;
	new_value = gtk_adjustment_get_value (adjustment);
	prev_value = new_value;
	upper = gtk_adjustment_get_upper (adjustment);
	lower = gtk_adjustment_get_lower (adjustment);
	page_size = gtk_adjustment_get_page_size (adjustment);
	step_increment = gtk_adjustment_get_step_increment (adjustment);

	/* Assign boolean for first and last page */
	if (priv->current_page == 0)
		first_page = TRUE;
	if (priv->current_page == pps_document_get_n_pages (pps_document_model_get_document (priv->model)) - 1)
		last_page = TRUE;

	switch (scroll) {
	case GTK_SCROLL_PAGE_BACKWARD:
		/* Do not jump backwards if at the first page */
		if (new_value == lower && first_page) {
			/* Do nothing */
			/* At the top of a page, assign the upper bound limit of previous page */
		} else if (new_value == lower) {
			new_value = upper - page_size;
			pps_view_previous_page (view);
			/* Jump to the top */
		} else {
			increment = compute_scroll_increment (view, GTK_SCROLL_PAGE_BACKWARD);
			new_value = MAX (new_value - increment, lower);
		}
		break;
	case GTK_SCROLL_PAGE_FORWARD:
		/* Do not jump forward if at the last page */
		if (new_value == (upper - page_size) && last_page) {
			/* Do nothing */
			/* At the bottom of a page, assign the lower bound limit of next page */
		} else if (new_value == (upper - page_size)) {
			new_value = 0;
			pps_view_next_page (view);
			/* Jump to the bottom */
		} else {
			increment = compute_scroll_increment (view, GTK_SCROLL_PAGE_FORWARD);
			new_value = MIN (new_value + increment, upper - page_size);
		}
		break;
	case GTK_SCROLL_STEP_BACKWARD:
		new_value -= step_increment;
		break;
	case GTK_SCROLL_STEP_FORWARD:
		new_value += step_increment;
		break;
	case GTK_SCROLL_STEP_DOWN:
		new_value -= step_increment / 10;
		break;
	case GTK_SCROLL_STEP_UP:
		new_value += step_increment / 10;
		break;
	case GTK_SCROLL_START:
		new_value = lower;
		if (!first_page)
			pps_view_first_page (view);
		break;
	case GTK_SCROLL_END:
		new_value = upper - page_size;
		if (!last_page)
			pps_view_last_page (view);
		break;
	default:
		break;
	}

	new_value = CLAMP (new_value, lower, upper - page_size);

	pps_view_cancel_kinetic_scrolling_deceleration (view);

	if (adw_animation_get_state (animation) == ADW_ANIMATION_PLAYING) {
		new_value += adw_timed_animation_get_value_to (ADW_TIMED_ANIMATION (animation)) - prev_value;
	}
	adw_animation_reset (animation);
	adw_timed_animation_set_value_from (ADW_TIMED_ANIMATION (animation), prev_value);
	adw_timed_animation_set_value_to (ADW_TIMED_ANIMATION (animation), new_value);
	adw_animation_play (animation);
	return TRUE;
}

#define MARGIN 5

void
_pps_view_ensure_rectangle_is_visible (PpsView *view, gint page, GdkRectangle *rect)
{
	GtkAdjustment *adjustment;
	gdouble adj_value;
	int value;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	int widget_width = gtk_widget_get_width (GTK_WIDGET (view));
	int widget_height = gtk_widget_get_height (GTK_WIDGET (view));

	if (!pps_document_model_get_continuous (priv->model)) {
		pps_document_model_set_page (priv->model, page);
	}

	priv->pending_scroll = SCROLL_TO_KEEP_POSITION;

	adjustment = priv->vadjustment;
	adj_value = gtk_adjustment_get_value (adjustment);

	if (rect->y < adj_value) {
		value = MAX (gtk_adjustment_get_lower (adjustment),
		             rect->y - MARGIN);
		gtk_adjustment_set_value (priv->vadjustment, value);
	} else if (rect->y + rect->height > adj_value + widget_height) {
		value = MIN (gtk_adjustment_get_upper (adjustment),
		             rect->y + rect->height - widget_height + MARGIN);
		gtk_adjustment_set_value (priv->vadjustment, value);
	}

	adjustment = priv->hadjustment;
	adj_value = gtk_adjustment_get_value (adjustment);

	if (rect->x < adj_value) {
		value = MAX (gtk_adjustment_get_lower (adjustment),
		             rect->x - MARGIN);
		gtk_adjustment_set_value (priv->hadjustment, value);
	} else if (rect->x + rect->height > adj_value + widget_width) {
		value = MIN (gtk_adjustment_get_upper (adjustment),
		             rect->x + rect->width - widget_width + MARGIN);
		gtk_adjustment_set_value (priv->hadjustment, value);
	}
}

/*** Geometry computations ***/
void
_get_page_size_for_scale_and_rotation (PpsDocument *document,
                                       gint page,
                                       gdouble scale,
                                       gint rotation,
                                       gint *page_width,
                                       gint *page_height)
{
	gdouble w, h;
	gint width, height;

	pps_document_get_page_size (document, page, &w, &h);

	width = (gint) (w * scale + 0.5);
	height = (gint) (h * scale + 0.5);

	if (page_width)
		*page_width = (rotation == 0 || rotation == 180) ? width : height;
	if (page_height)
		*page_height = (rotation == 0 || rotation == 180) ? height : width;
}

static void
pps_view_get_page_size (PpsView *view,
                        gint page,
                        gint *page_width,
                        gint *page_height)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	_get_page_size_for_scale_and_rotation (pps_document_model_get_document (priv->model),
	                                       page,
	                                       pps_document_model_get_scale (priv->model),
	                                       pps_document_model_get_rotation (priv->model),
	                                       page_width,
	                                       page_height);
}

static void
pps_view_get_max_page_size (PpsView *view,
                            gint *max_width,
                            gint *max_height)
{
	double w, h, scale;
	gint width, height, rotation;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	pps_document_get_max_page_size (pps_document_model_get_document (priv->model), &w, &h);
	scale = pps_document_model_get_scale (priv->model);
	rotation = pps_document_model_get_rotation (priv->model);

	width = (gint) (w * scale + 0.5);
	height = (gint) (h * scale + 0.5);

	if (max_width)
		*max_width = (rotation == 0 || rotation == 180) ? width : height;
	if (max_height)
		*max_height = (rotation == 0 || rotation == 180) ? height : width;
}

static void
get_page_y_offset (PpsView *view, int page, int *y_offset)
{
	int offset = 0;
	gboolean odd_left;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_return_if_fail (y_offset != NULL);

	if (is_dual_page (view, &odd_left)) {
		pps_view_get_height_to_page (view, page, NULL, &offset);
		offset += ((page + !odd_left) / 2 + 1) * priv->spacing;
	} else {
		pps_view_get_height_to_page (view, page, &offset, NULL);
		offset += (page + 1) * priv->spacing;
	}

	*y_offset = offset;
}

void
pps_view_get_page_extents (PpsView *view,
                           gint page,
                           GdkRectangle *page_area)
{
	GtkWidget *widget = GTK_WIDGET (view);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	int width, height, widget_width, widget_height;

	widget_width = gtk_widget_get_width (widget);
	widget_height = gtk_widget_get_height (widget);

	/* Get the size of the page */
	pps_view_get_page_size (view, page, &width, &height);
	page_area->width = width;
	page_area->height = height;

	if (pps_document_model_get_continuous (priv->model)) {
		gint max_width;
		gint x, y;
		gboolean odd_left;

		pps_view_get_max_page_size (view, &max_width, NULL);
		/* Get the location of the bounding box */
		if (is_dual_page (view, &odd_left)) {
			gboolean right_page;

			right_page = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR && page % 2 == !odd_left) ||
			             (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL && page % 2 == odd_left);

			x = priv->spacing + (right_page ? 0 : 1) * (max_width + priv->spacing);
			x = x + MAX (0, widget_width - (max_width * 2 + priv->spacing * 3)) / 2;
			if (right_page)
				x = x + (max_width - width);
		} else {
			x = priv->spacing;
			x = x + MAX (0, widget_width - (width + priv->spacing * 2)) / 2;
		}

		get_page_y_offset (view, page, &y);

		page_area->x = x;
		page_area->y = y;
	} else {
		gint x, y;
		gint other_page;
		gboolean odd_left;

		if (get_dual_page_other_page (view, page, &other_page, &odd_left)) {
			gint width_2, height_2;
			gint max_width = width;
			gint max_height = height;

			/* First, we get the bounding box of the two pages */
			if (0 <= other_page &&
			    other_page < pps_document_get_n_pages (pps_document_model_get_document (priv->model))) {
				pps_view_get_page_size (view, other_page,
				                        &width_2, &height_2);
				if (width_2 > width)
					max_width = width_2;
				if (height_2 > height)
					max_height = height_2;
			}

			/* Find the offsets */
			x = priv->spacing;
			y = priv->spacing;

			/* Adjust for being the left or right page */
			if ((gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR && page % 2 == !odd_left) ||
			    (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL && page % 2 == odd_left))
				x = x + max_width - width;
			else
				x = x + max_width + priv->spacing;

			y = y + (max_height - height) / 2;

			/* Adjust for extra allocation */
			x = x + MAX (0, widget_width -
			                    (max_width * 2 + priv->spacing * 3)) /
			            2;
			y = y + MAX (0, widget_height - (height + priv->spacing * 2)) / 2;
		} else {
			x = priv->spacing;
			y = priv->spacing;

			/* Adjust for extra allocation */
			x = x + MAX (0, widget_width - (width + priv->spacing * 2)) / 2;
			y = y + MAX (0, widget_height - (height + priv->spacing * 2)) / 2;
		}

		page_area->x = x;
		page_area->y = y;
	}
}

static void
get_doc_page_size (PpsView *view,
                   gint page,
                   gdouble *width,
                   gdouble *height)
{
	double w, h;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gint rotation = pps_document_model_get_rotation (priv->model);

	pps_document_get_page_size (pps_document_model_get_document (priv->model), page, &w, &h);
	if (rotation == 0 || rotation == 180) {
		if (width)
			*width = w;
		if (height)
			*height = h;
	} else {
		if (width)
			*width = h;
		if (height)
			*height = w;
	}
}

/**
 * pps_view_get_point_on_page:
 * @view: a #PpsView
 * @page_index: the index of the page where view_point_x and view_point_y
 * are located. Passing a different page is considered a programmers error
 * @view_point_x: the x coordinate over the view
 * @view_point_y: the y coordinate over the view
 *
 * This API is intended to, in some future time, be part of a PpsViewPage.
 * For now, it's private API within libview
 *
 * Returns: a #PpsPoint to the place in the page corresponding to
 * view_point_x and view_point_y
 *
 * Since: 48.0
 */
PpsPoint
pps_view_get_point_on_page (PpsView *view,
                            gint page_index,
                            gdouble view_point_x,
                            gdouble view_point_y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsPoint point_on_page;
	GdkRectangle page_area;
	double x, y, width, height, scale;
	PpsDocument *document = pps_document_model_get_document (priv->model);

	g_assert (page_index >= 0);

	scale = pps_document_model_get_scale (priv->model);

	view_point_x += gtk_adjustment_get_value (priv->hadjustment);
	view_point_y += gtk_adjustment_get_value (priv->vadjustment);

	pps_view_get_page_extents (view, page_index, &page_area);

	x = MAX ((view_point_x - (double) page_area.x) / scale, 0);
	y = MAX ((view_point_y - (double) page_area.y) / scale, 0);

	pps_document_get_page_size (document, priv->current_page,
	                            &width, &height);

	switch (pps_document_model_get_rotation (priv->model)) {
	case 0:
		point_on_page.x = x;
		point_on_page.y = y;
		break;
	case 90:
		point_on_page.x = y;
		point_on_page.y = height - x;
		break;
	case 180:
		point_on_page.x = width - x;
		point_on_page.y = height - y;
		break;
	case 270:
		point_on_page.x = width - y;
		point_on_page.y = x;
		break;
	default:
		g_assert_not_reached ();
	}

	return point_on_page;
}

/**
 * pps_view_get_document_point_for_view_point:
 * @view: a #PpsView
 * @view_point_x: the x coordinate over the view
 * @view_point_y: the y coordinate over the view
 *
 * Returns: (nullable) (transfer full): a pointer to a #PpsDocumentPoint that
 * represents the location in the document for @view_point_x and @view_point_y.
 * If the location is not in a page in the document, it returns NULL.
 *
 * Since: 48.0
 */
PpsDocumentPoint *
pps_view_get_document_point_for_view_point (PpsView *view,
                                            gdouble view_point_x,
                                            gdouble view_point_y)
{
	PpsDocumentPoint *document_point;
	gint page_index;
	PpsPoint point_on_page;

	find_page_at_location (view, view_point_x, view_point_y,
	                       &page_index, NULL, NULL);
	if (page_index == -1)
		return NULL;

	point_on_page = pps_view_get_point_on_page (view, page_index,
	                                            view_point_x,
	                                            view_point_y);

	document_point = g_new (PpsDocumentPoint, 1);
	document_point->page_index = page_index;
	document_point->point_on_page = point_on_page;
	return document_point;
}

static void
transform_view_rect_to_doc_rect (PpsView *view,
                                 GdkRectangle *view_rect,
                                 GdkRectangle *page_area,
                                 PpsRectangle *doc_rect)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble scale = pps_document_model_get_scale (priv->model);

	doc_rect->x1 = MAX ((double) (view_rect->x - page_area->x) / scale, 0);
	doc_rect->y1 = MAX ((double) (view_rect->y - page_area->y) / scale, 0);
	doc_rect->x2 = doc_rect->x1 + (double) view_rect->width / scale;
	doc_rect->y2 = doc_rect->y1 + (double) view_rect->height / scale;
}

static void
transform_page_point_by_rotation_scale (PpsView *view,
                                        int page,
                                        PpsPoint *point_on_page,
                                        gdouble *view_point_x,
                                        gdouble *view_point_y)
{
	GdkRectangle page_area;
	double x, y, view_x, view_y, scale;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	switch (pps_document_model_get_rotation (priv->model)) {
	case 0:
		x = point_on_page->x;
		y = point_on_page->y;

		break;
	case 90: {
		gdouble width;

		get_doc_page_size (view, page, &width, NULL);
		x = width - point_on_page->y;
		y = point_on_page->x;
	} break;
	case 180: {
		gdouble width, height;

		get_doc_page_size (view, page, &width, &height);
		x = width - point_on_page->x;
		y = height - point_on_page->y;
	} break;
	case 270: {
		gdouble height;

		get_doc_page_size (view, page, NULL, &height);
		x = point_on_page->y;
		y = height - point_on_page->x;
	} break;
	default:
		g_assert_not_reached ();
	}

	pps_view_get_page_extents (view, page, &page_area);
	scale = pps_document_model_get_scale (priv->model);

	view_x = CLAMP ((gint) (x * scale + 0.5), 0, page_area.width);
	view_y = CLAMP ((gint) (y * scale + 0.5), 0, page_area.height);

	*view_point_x = view_x;
	*view_point_y = view_y;
}

static void
transform_page_point_to_view_point (PpsView *view,
                                    int page,
                                    PpsPoint *point_on_page,
                                    gdouble *view_point_x,
                                    gdouble *view_point_y)
{
	GdkRectangle page_area;
	transform_page_point_by_rotation_scale (view, page, point_on_page, view_point_x, view_point_y);

	pps_view_get_page_extents (view, page, &page_area);

	*view_point_x = *view_point_x + page_area.x;
	*view_point_y = *view_point_y + page_area.y;
}

void
_pps_view_transform_doc_rect_to_view_rect (PpsView *view,
                                           int page,
                                           const PpsRectangle *doc_rect,
                                           GdkRectangle *view_rect)
{
	GdkRectangle page_area;
	double x, y, w, h, scale;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	switch (pps_document_model_get_rotation (priv->model)) {
	case 0:
		x = doc_rect->x1;
		y = doc_rect->y1;
		w = doc_rect->x2 - doc_rect->x1;
		h = doc_rect->y2 - doc_rect->y1;

		break;
	case 90: {
		gdouble width;

		get_doc_page_size (view, page, &width, NULL);
		x = width - doc_rect->y2;
		y = doc_rect->x1;
		w = doc_rect->y2 - doc_rect->y1;
		h = doc_rect->x2 - doc_rect->x1;
	} break;
	case 180: {
		gdouble width, height;

		get_doc_page_size (view, page, &width, &height);
		x = width - doc_rect->x2;
		y = height - doc_rect->y2;
		w = doc_rect->x2 - doc_rect->x1;
		h = doc_rect->y2 - doc_rect->y1;
	} break;
	case 270: {
		gdouble height;

		get_doc_page_size (view, page, NULL, &height);
		x = doc_rect->y1;
		y = height - doc_rect->x2;
		w = doc_rect->y2 - doc_rect->y1;
		h = doc_rect->x2 - doc_rect->x1;
	} break;
	default:
		g_assert_not_reached ();
	}

	pps_view_get_page_extents (view, page, &page_area);
	scale = pps_document_model_get_scale (priv->model);

	view_rect->x = (gint) (x * scale + 0.5) + page_area.x;
	view_rect->y = (gint) (y * scale + 0.5) + page_area.y;
	view_rect->width = (gint) (w * scale + 0.5);
	view_rect->height = (gint) (h * scale + 0.5);
}

static void
find_page_at_location (PpsView *view,
                       gdouble x,
                       gdouble y,
                       gint *page,
                       gint *x_offset,
                       gint *y_offset)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	int i;

	if (pps_document_model_get_document (priv->model) == NULL)
		return;

	x += gtk_adjustment_get_value (priv->hadjustment);
	y += gtk_adjustment_get_value (priv->vadjustment);

	g_assert (page);

	for (i = priv->start_page; i >= 0 && i <= priv->end_page; i++) {
		GdkRectangle page_area;

		pps_view_get_page_extents (view, i, &page_area);

		if ((x >= page_area.x) &&
		    (x < page_area.x + page_area.width) &&
		    (y >= page_area.y) &&
		    (y < page_area.y + page_area.height)) {
			*page = i;
			if (x_offset)
				*x_offset = x - page_area.x;
			if (y_offset)
				*y_offset = y - page_area.y;
			return;
		}
	}

	*page = -1;
}

static gboolean
location_in_text (PpsView *view,
                  gdouble x,
                  gdouble y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	cairo_region_t *region;
	gint page = -1;
	gint x_offset = 0, y_offset = 0;
	gdouble scale = pps_document_model_get_scale (priv->model);

	find_page_at_location (view, x, y, &page, &x_offset, &y_offset);

	if (page == -1)
		return FALSE;

	region = pps_page_cache_get_text_mapping (priv->page_cache, page);

	if (region)
		return cairo_region_contains_point (region, x_offset / scale, y_offset / scale);
	else
		return FALSE;
}

static void
get_page_point_from_offset (PpsView *view,
                            gint page,
                            gint x_offset,
                            gint y_offset,
                            gdouble *x_new,
                            gdouble *y_new)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble width, height, scale;
	double x, y;
	gint rotation;

	get_doc_page_size (view, page, &width, &height);
	scale = pps_document_model_get_scale (priv->model);
	rotation = pps_document_model_get_rotation (priv->model);

	x_offset = x_offset / scale;
	y_offset = y_offset / scale;

	if (rotation == 0) {
		x = x_offset;
		y = y_offset;
	} else if (rotation == 90) {
		x = y_offset;
		y = width - x_offset;
	} else if (rotation == 180) {
		x = width - x_offset;
		y = height - y_offset;
	} else if (rotation == 270) {
		x = height - y_offset;
		y = x_offset;
	} else {
		g_assert_not_reached ();
	}

	*x_new = x;
	*y_new = y;
}

static void
pps_view_get_area_from_mapping (PpsView *view,
                                guint page,
                                PpsMappingList *mapping_list,
                                gconstpointer data,
                                GdkRectangle *area)
{
	PpsMapping *mapping;
	guint scroll_x, scroll_y;

	get_scroll_offset (view, &scroll_x, &scroll_y);
	mapping = pps_mapping_list_find (mapping_list, data);
	_pps_view_transform_doc_rect_to_view_rect (view, page, &mapping->area, area);
	area->x -= scroll_x;
	area->y -= scroll_y;
}

static void
pps_child_free (PpsViewChild *child)
{
	g_slice_free (PpsViewChild, child);
}

static void
pps_view_put (PpsView *view,
              GtkWidget *child_widget,
              guint page,
              PpsRectangle *doc_rect)
{
	PpsViewChild *child;

	child = g_slice_new (PpsViewChild);

	child->page = page;
	child->doc_rect = *doc_rect;

	g_object_set_data_full (G_OBJECT (child_widget), "pps-child",
	                        child, (GDestroyNotify) pps_child_free);

	gtk_widget_set_parent (child_widget, GTK_WIDGET (view));
}

/*** Hyperref ***/
static PpsMapping *
get_link_mapping_at_location (PpsView *view,
                              gdouble x,
                              gdouble y,
                              gint *page)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	g_autofree PpsDocumentPoint *doc_point = NULL;
	PpsMappingList *link_mapping;

	if (!PPS_IS_DOCUMENT_LINKS (pps_document_model_get_document (priv->model)))
		return NULL;

	doc_point = pps_view_get_document_point_for_view_point (view, x, y);
	if (!doc_point)
		return NULL;

	if (page)
		*page = doc_point->page_index;

	link_mapping = pps_page_cache_get_link_mapping (priv->page_cache,
	                                                doc_point->page_index);
	if (link_mapping)
		return pps_mapping_list_get (link_mapping, &doc_point->point_on_page);

	return NULL;
}

static PpsLink *
pps_view_get_link_at_location (PpsView *view,
                               gdouble x,
                               gdouble y)
{
	PpsMapping *mapping;

	mapping = get_link_mapping_at_location (view, x, y, NULL);

	return mapping ? mapping->data : NULL;
}

static void
goto_fitr_dest (PpsView *view, PpsLinkDest *dest)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocumentPoint doc_point;
	gdouble left, top;
	gboolean change_left, change_top;
	int widget_width = gtk_widget_get_width (GTK_WIDGET (view));
	int widget_height = gtk_widget_get_height (GTK_WIDGET (view));

	left = pps_link_dest_get_left (dest, &change_left);
	top = pps_link_dest_get_top (dest, &change_top);

	if (priv->allow_links_change_zoom) {
		gdouble doc_width, doc_height;
		gdouble zoom;

		doc_width = pps_link_dest_get_right (dest) - left;
		doc_height = pps_link_dest_get_bottom (dest) - top;

		zoom = zoom_for_size_fit_page (doc_width,
		                               doc_height,
		                               widget_width,
		                               widget_height);

		pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FREE);
		pps_document_model_set_scale (priv->model, zoom);

		/* center the target box within the view */
		left -= (widget_width / zoom - doc_width) / 2;
		top -= (widget_height / zoom - doc_height) / 2;
	}

	doc_point.page_index = pps_link_dest_get_page (dest);
	doc_point.point_on_page.x = change_left ? left : 0;
	doc_point.point_on_page.y = change_top ? top : 0;

	pps_view_scroll_to_doc_point (view, &doc_point);
}

static void
goto_fitv_dest (PpsView *view, PpsLinkDest *dest)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	PpsDocumentPoint doc_point;
	double left;
	gboolean change_left;

	doc_point.page_index = pps_link_dest_get_page (dest);

	left = pps_link_dest_get_left (dest, &change_left);
	doc_point.point_on_page.x = change_left ? left : 0;
	doc_point.point_on_page.y = 0;

	if (priv->allow_links_change_zoom) {
		gdouble doc_width, doc_height;
		double zoom;

		pps_document_get_page_size (document, doc_point.page_index, &doc_width, &doc_height);

		zoom = zoom_for_size_fit_height (doc_width - doc_point.point_on_page.x, doc_height,
		                                 gtk_widget_get_width (GTK_WIDGET (view)),
		                                 gtk_widget_get_height (GTK_WIDGET (view)));

		pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FREE);
		pps_document_model_set_scale (priv->model, zoom);
	}

	pps_view_scroll_to_doc_point (view, &doc_point);
}

static void
goto_fith_dest (PpsView *view, PpsLinkDest *dest)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocumentPoint doc_point;
	gdouble top;
	gboolean change_top;

	doc_point.page_index = pps_link_dest_get_page (dest);

	top = pps_link_dest_get_top (dest, &change_top);
	doc_point.point_on_page.x = 0;
	doc_point.point_on_page.y = change_top ? top : 0;

	if (priv->allow_links_change_zoom)
		pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FIT_WIDTH);

	pps_view_scroll_to_doc_point (view, &doc_point);
}

static void
goto_fit_dest (PpsView *view, PpsLinkDest *dest)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	int page;

	page = pps_link_dest_get_page (dest);

	if (priv->allow_links_change_zoom)
		pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FIT_PAGE);

	pps_view_scroll_to_page (view, page);
}

static void
goto_xyz_dest (PpsView *view, PpsLinkDest *dest)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocumentPoint doc_point;
	gdouble zoom, left, top;
	gboolean change_zoom, change_left, change_top;

	zoom = pps_link_dest_get_zoom (dest, &change_zoom);

	if (priv->allow_links_change_zoom && change_zoom && zoom > 1) {
		pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FREE);
		pps_document_model_set_scale (priv->model, zoom);
	}

	left = pps_link_dest_get_left (dest, &change_left);
	top = pps_link_dest_get_top (dest, &change_top);

	doc_point.page_index = pps_link_dest_get_page (dest);
	doc_point.point_on_page.x = change_left ? left : 0;
	doc_point.point_on_page.y = change_top ? top : 0;

	pps_view_scroll_to_doc_point (view, &doc_point);
}

static void
goto_dest (PpsView *view, PpsLinkDest *dest)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsLinkDestType type;
	int page, n_pages, current_page;

	page = pps_link_dest_get_page (dest);
	n_pages = pps_document_get_n_pages (pps_document_model_get_document (priv->model));

	if (page < 0 || page >= n_pages)
		return;

	current_page = priv->current_page;

	type = pps_link_dest_get_dest_type (dest);

	switch (type) {
	case PPS_LINK_DEST_TYPE_PAGE:
		pps_document_model_set_page (priv->model, page);
		break;
	case PPS_LINK_DEST_TYPE_FIT:
		goto_fit_dest (view, dest);
		break;
	case PPS_LINK_DEST_TYPE_FITH:
		goto_fith_dest (view, dest);
		break;
	case PPS_LINK_DEST_TYPE_FITV:
		goto_fitv_dest (view, dest);
		break;
	case PPS_LINK_DEST_TYPE_FITR:
		goto_fitr_dest (view, dest);
		break;
	case PPS_LINK_DEST_TYPE_XYZ:
		goto_xyz_dest (view, dest);
		break;
	case PPS_LINK_DEST_TYPE_PAGE_LABEL:
		pps_document_model_set_page_by_label (priv->model, pps_link_dest_get_page_label (dest));
		break;
	default:
		g_assert_not_reached ();
	}

	if (current_page != priv->current_page)
		pps_document_model_set_page (priv->model, priv->current_page);
}

static void
pps_view_goto_dest (PpsView *view, PpsLinkDest *dest)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsLinkDestType type;
	g_autoptr (PpsLinkDest) final_dest = NULL;

	type = pps_link_dest_get_dest_type (dest);

	if (type == PPS_LINK_DEST_TYPE_NAMED) {
		const gchar *named_dest;

		named_dest = pps_link_dest_get_named_dest (dest);
		final_dest = pps_document_links_find_link_dest (PPS_DOCUMENT_LINKS (pps_document_model_get_document (priv->model)),
		                                                named_dest);

		if (!final_dest)
			return;
	}

	goto_dest (view, final_dest ? final_dest : dest);
}

static void
pps_view_link_to_current_view (PpsView *view, PpsLink **backlink)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsLinkDest *backlink_dest = NULL;
	PpsLinkAction *backlink_action = NULL;
	gint backlink_page = priv->start_page;
	gdouble zoom = pps_document_model_get_scale (priv->model);
	GdkRectangle backlink_page_area;
	gboolean is_dual = pps_document_model_get_page_layout (priv->model) == PPS_PAGE_LAYOUT_DUAL;
	gint x_offset, y_offset;
	guint scroll_x, scroll_y;

	if (priv->start_page == -1) {
		*backlink = NULL;
		return;
	}

	get_scroll_offset (view, &scroll_x, &scroll_y);
	pps_view_get_page_extents (view, backlink_page, &backlink_page_area);
	x_offset = backlink_page_area.x;
	y_offset = backlink_page_area.y;

	if (!pps_document_model_get_continuous (priv->model) && is_dual && scroll_x > backlink_page_area.width) {
		/* For dual-column, non-continuous mode, priv->start_page is always
		 * the page in the left-hand column, even if that page isn't visible.
		 * We adjust for that here when we know the page can't be visible due
		 * to horizontal scroll. */
		backlink_page = backlink_page + 1;

		/* get right-hand page extents */
		pps_view_get_page_extents (view, backlink_page, &backlink_page_area);
		x_offset = backlink_page_area.x;
	}

	gdouble backlink_dest_x = (scroll_x - x_offset) / zoom;
	gdouble backlink_dest_y = (scroll_y - y_offset) / zoom;

	backlink_dest = pps_link_dest_new_xyz (backlink_page, backlink_dest_x,
	                                       backlink_dest_y, zoom, TRUE,
	                                       TRUE, TRUE);

	backlink_action = pps_link_action_new_dest (backlink_dest);
	g_object_unref (backlink_dest);

	*backlink = pps_link_new ("Backlink", backlink_action);
	g_object_unref (backlink_action);
}

void
pps_view_handle_link (PpsView *view, PpsLink *link)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsLinkAction *action = NULL;
	PpsLinkActionType type;

	action = pps_link_get_action (link);
	if (!action)
		return;

	type = pps_link_action_get_action_type (action);

	switch (type) {
	case PPS_LINK_ACTION_TYPE_GOTO_DEST: {
		/* Build a synthetic Link representing our current view into the
		 * document. */

		PpsLinkDest *dest;
		g_autoptr (PpsLink) backlink = NULL;

		pps_view_link_to_current_view (view, &backlink);

		g_signal_emit (view, signals[SIGNAL_HANDLE_LINK], 0, link, backlink);

		dest = pps_link_action_get_dest (action);
		pps_view_goto_dest (view, dest);
	} break;
	case PPS_LINK_ACTION_TYPE_LAYERS_STATE: {
		GList *show, *hide, *toggle;
		GList *l;
		PpsDocumentLayers *document_layers;

		document_layers = PPS_DOCUMENT_LAYERS (pps_document_model_get_document (priv->model));

		show = pps_link_action_get_show_list (action);
		for (l = show; l; l = g_list_next (l)) {
			pps_document_layers_show_layer (document_layers, PPS_LAYER (l->data));
		}

		hide = pps_link_action_get_hide_list (action);
		for (l = hide; l; l = g_list_next (l)) {
			pps_document_layers_hide_layer (document_layers, PPS_LAYER (l->data));
		}

		toggle = pps_link_action_get_toggle_list (action);
		for (l = toggle; l; l = g_list_next (l)) {
			PpsLayer *layer = PPS_LAYER (l->data);

			if (pps_document_layers_layer_is_visible (document_layers, layer)) {
				pps_document_layers_hide_layer (document_layers, layer);
			} else {
				pps_document_layers_show_layer (document_layers, layer);
			}
		}

		g_signal_emit (view, signals[SIGNAL_LAYERS_CHANGED], 0);
		pps_view_reload (view);
	} break;
	case PPS_LINK_ACTION_TYPE_GOTO_REMOTE:
	case PPS_LINK_ACTION_TYPE_EXTERNAL_URI:
	case PPS_LINK_ACTION_TYPE_LAUNCH:
	case PPS_LINK_ACTION_TYPE_NAMED:
	case PPS_LINK_ACTION_TYPE_RESET_FORM:
		g_signal_emit (view, signals[SIGNAL_EXTERNAL_LINK], 0, action);
		break;
	}
}

static char *
tip_from_action_named (PpsLinkAction *action)
{
	const gchar *name = pps_link_action_get_name (action);

	if (g_ascii_strcasecmp (name, "FirstPage") == 0) {
		return g_strdup (_ ("Go to first page"));
	} else if (g_ascii_strcasecmp (name, "PrevPage") == 0) {
		return g_strdup (_ ("Go to previous page"));
	} else if (g_ascii_strcasecmp (name, "NextPage") == 0) {
		return g_strdup (_ ("Go to next page"));
	} else if (g_ascii_strcasecmp (name, "LastPage") == 0) {
		return g_strdup (_ ("Go to last page"));
	} else if (g_ascii_strcasecmp (name, "GoToPage") == 0) {
		return g_strdup (_ ("Go to page"));
	} else if (g_ascii_strcasecmp (name, "Find") == 0) {
		return g_strdup (_ ("Find"));
	}

	return NULL;
}

static char *
tip_from_link (PpsView *view, PpsLink *link)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsLinkAction *action;
	PpsLinkActionType type;
	char *msg = NULL;
	char *page_label;
	const char *title;

	action = pps_link_get_action (link);
	title = pps_link_get_title (link);

	if (!action)
		return title ? g_strdup (title) : NULL;

	type = pps_link_action_get_action_type (action);

	switch (type) {
	case PPS_LINK_ACTION_TYPE_GOTO_DEST:
		page_label = pps_document_links_get_dest_page_label (PPS_DOCUMENT_LINKS (pps_document_model_get_document (priv->model)),
		                                                     pps_link_action_get_dest (action));
		if (page_label) {
			msg = g_strdup_printf (_ ("Go to page %s"), page_label);
			g_free (page_label);
		}
		break;
	case PPS_LINK_ACTION_TYPE_GOTO_REMOTE:
		if (title) {
			msg = g_strdup_printf (_ ("Go to %s on file “%s”"), title,
			                       pps_link_action_get_filename (action));
		} else {
			msg = g_strdup_printf (_ ("Go to file “%s”"),
			                       pps_link_action_get_filename (action));
		}
		break;
	case PPS_LINK_ACTION_TYPE_EXTERNAL_URI:
		msg = g_strdup (pps_link_action_get_uri (action));
		break;
	case PPS_LINK_ACTION_TYPE_LAUNCH:
		msg = g_strdup_printf (_ ("Launch %s"),
		                       pps_link_action_get_filename (action));
		break;
	case PPS_LINK_ACTION_TYPE_NAMED:
		msg = tip_from_action_named (action);
		break;
	case PPS_LINK_ACTION_TYPE_RESET_FORM:
		msg = g_strdup_printf (_ ("Reset form"));
		break;
	default:
		if (title)
			msg = g_strdup (title);
		break;
	}

	return msg;
}

static gboolean
handle_link_preview (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GdkRectangle link_area;
	PpsLinkAction *action;
	PpsLinkDest *dest;
	PpsLinkDestType type;
	GtkWidget *popover, *spinner;
	GdkTexture *page_texture = NULL;
	guint link_dest_page;
	PpsPoint link_dest_doc;
	gdouble link_dest_x, link_dest_y;
	gdouble scale;
	gdouble device_scale = 1;
	PpsLink *link = priv->link_preview.link;

	pps_view_link_preview_popover_cleanup (view);

	if (!link)
		return G_SOURCE_REMOVE;

	action = pps_link_get_action (link);
	if (!action)
		return G_SOURCE_REMOVE;

	dest = pps_link_action_get_dest (action);
	if (!dest)
		return G_SOURCE_REMOVE;

	type = pps_link_dest_get_dest_type (dest);
	if (type == PPS_LINK_DEST_TYPE_NAMED) {
		dest = pps_document_links_find_link_dest (PPS_DOCUMENT_LINKS (pps_document_model_get_document (priv->model)),
		                                          pps_link_dest_get_named_dest (dest));
		if (!dest)
			return G_SOURCE_REMOVE;
	}

	/* Handle the case that page doesn't exist */
	if (pps_link_dest_get_page (dest) == -1) {
		if (type == PPS_LINK_DEST_TYPE_NAMED)
			g_object_unref (dest);
		return G_SOURCE_REMOVE;
	}

	/* Init popover */
	priv->link_preview.popover = popover = gtk_popover_new ();
	gtk_popover_set_position (GTK_POPOVER (popover), GTK_POS_TOP);
	gtk_widget_set_parent (popover, GTK_WIDGET (view));
	get_link_area (view, priv->link_preview.source_page, link, &link_area);
	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &link_area);
	gtk_popover_set_autohide (GTK_POPOVER (popover), FALSE);

	spinner = adw_spinner_new ();
	gtk_popover_set_child (GTK_POPOVER (popover), spinner);

	/* Start thumbnailing job async */
	link_dest_page = pps_link_dest_get_page (dest);
	device_scale = gdk_surface_get_scale (gtk_native_get_surface (gtk_widget_get_native (GTK_WIDGET (view))));
	scale = pps_document_model_get_scale (priv->model);
	priv->link_preview.job = pps_job_thumbnail_texture_new (pps_document_model_get_document (priv->model),
	                                                        link_dest_page,
	                                                        pps_document_model_get_rotation (priv->model),
	                                                        scale * device_scale);

	link_dest_doc.x = pps_link_dest_get_left (dest, NULL);
	link_dest_doc.y = pps_link_dest_get_top (dest, NULL);
	transform_page_point_by_rotation_scale (view, link_dest_page,
	                                        &link_dest_doc, &link_dest_x, &link_dest_y);
	priv->link_preview.left = link_dest_x;
	priv->link_preview.top = link_dest_y;

	page_texture = pps_pixbuf_cache_get_texture (priv->pixbuf_cache, link_dest_page);

	if (page_texture)
		link_preview_set_thumbnail (page_texture, view);
	else {
		g_signal_connect (priv->link_preview.job, "finished",
		                  G_CALLBACK (link_preview_job_finished_cb),
		                  view);
		pps_job_scheduler_push_job (priv->link_preview.job, PPS_JOB_PRIORITY_LOW);
	}

	if (type == PPS_LINK_DEST_TYPE_NAMED)
		g_object_unref (dest);

	priv->link_preview.delay_timeout_id =
	    g_timeout_add_once (LINK_PREVIEW_DELAY_MS,
	                        (GSourceOnceFunc) link_preview_delayed_show,
	                        view);
	g_source_set_name_by_id (priv->link_preview.delay_timeout_id,
	                         "[papers] link_preview_timeout");

	return G_SOURCE_REMOVE;
}

static void
pps_view_handle_cursor_over_xy (PpsView *view, gint x, gint y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsLink *link;
	PpsAnnotation *annot = NULL;
	PpsMedia *media;
	gboolean is_link = FALSE;

	if (gtk_event_controller_get_propagation_phase (priv->signing_drag_gesture) != GTK_PHASE_NONE) {
		pps_view_set_cursor (view, PPS_VIEW_CURSOR_ADD);
	} else if (gtk_gesture_is_active (GTK_GESTURE (priv->middle_clicked_drag_gesture))) {
		pps_view_set_cursor (view, PPS_VIEW_CURSOR_DRAG);
	} else if ((link = pps_view_get_link_at_location (view, x, y))) {
		is_link = TRUE;

		if (priv->link_preview.link != link) {
			priv->link_preview.link = link;
			find_page_at_location (view, x, y, &priv->link_preview.source_page, NULL, NULL);
			gtk_widget_add_tick_callback (GTK_WIDGET (view), (GtkTickCallback) handle_link_preview, NULL, NULL);
		}

		pps_view_set_cursor (view, PPS_VIEW_CURSOR_LINK);
	} else if ((media = pps_view_get_media_at_location (view, x, y))) {
		if (!pps_view_find_player_for_media (view, media))
			pps_view_set_cursor (view, PPS_VIEW_CURSOR_LINK);
		else
			pps_view_set_cursor (view, PPS_VIEW_CURSOR_NORMAL);
	} else if ((annot = get_annotation_at_location (view, x, y))) {
		pps_view_set_cursor (view, PPS_VIEW_CURSOR_LINK);
	} else if (location_in_text (view, x, y)) {
		pps_view_set_cursor (view, PPS_VIEW_CURSOR_IBEAM);
	} else {
		pps_view_set_cursor (view, PPS_VIEW_CURSOR_NORMAL);
	}

	if (!is_link) {
		priv->link_preview.link = NULL;
		pps_view_link_preview_popover_cleanup (view);
	}
}

/*** Images ***/
static PpsImage *
pps_view_get_image_at_location (PpsView *view,
                                gdouble x,
                                gdouble y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	g_autofree PpsDocumentPoint *doc_point = NULL;
	PpsMappingList *image_mapping;

	if (!PPS_IS_DOCUMENT_IMAGES (pps_document_model_get_document (priv->model)))
		return NULL;

	doc_point = pps_view_get_document_point_for_view_point (view, x, y);
	if (!doc_point)
		return NULL;

	image_mapping = pps_page_cache_get_image_mapping (priv->page_cache,
	                                                  doc_point->page_index);

	if (image_mapping)
		return pps_mapping_list_get_data (image_mapping,
		                                  &doc_point->point_on_page);
	else
		return NULL;
}

/*** Focus ***/
static gboolean
pps_view_get_focused_area (PpsView *view,
                           GdkRectangle *area)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	guint scroll_x, scroll_y;

	if (!priv->focused_element)
		return FALSE;

	get_scroll_offset (view, &scroll_x, &scroll_y);
	_pps_view_transform_doc_rect_to_view_rect (view,
	                                           priv->focused_element_page,
	                                           &priv->focused_element->area,
	                                           area);
	area->x -= scroll_x + 1;
	area->y -= scroll_y + 1;
	area->width += 1;
	area->height += 1;

	return TRUE;
}

void
_pps_view_set_focused_element (PpsView *view,
                               PpsMapping *element_mapping,
                               gint page)
{
	GdkRectangle view_rect;
	cairo_region_t *region = NULL;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	guint scroll_x, scroll_y;

	if (pps_view_get_focused_area (view, &view_rect))
		region = cairo_region_create_rectangle (&view_rect);

	g_clear_pointer (&priv->focused_element, pps_mapping_free);
	if (element_mapping)
		priv->focused_element = pps_mapping_copy (element_mapping);
	priv->focused_element_page = page;

	get_scroll_offset (view, &scroll_x, &scroll_y);

	if (pps_view_get_focused_area (view, &view_rect)) {
		if (!region)
			region = cairo_region_create_rectangle (&view_rect);
		else
			cairo_region_union_rectangle (region, &view_rect);

		view_rect.x += scroll_x;
		view_rect.y += scroll_y;
		_pps_view_ensure_rectangle_is_visible (view, page, &view_rect);
	}

	g_clear_pointer (&region, cairo_region_destroy);
}

/* Media */
static PpsMapping *
get_media_mapping_at_location (PpsView *view,
                               gdouble x,
                               gdouble y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	g_autofree PpsDocumentPoint *doc_point = NULL;
	PpsMappingList *media_mapping;

	if (!PPS_IS_DOCUMENT_MEDIA (pps_document_model_get_document (priv->model)))
		return NULL;

	doc_point = pps_view_get_document_point_for_view_point (view, x, y);
	if (!doc_point)
		return NULL;

	media_mapping = pps_page_cache_get_media_mapping (priv->page_cache,
	                                                  doc_point->page_index);

	return media_mapping ? pps_mapping_list_get (media_mapping, &doc_point->point_on_page) : NULL;
}

static PpsMedia *
pps_view_get_media_at_location (PpsView *view,
                                gdouble x,
                                gdouble y)
{
	PpsMapping *media_mapping;

	media_mapping = get_media_mapping_at_location (view, x, y);

	return media_mapping ? media_mapping->data : NULL;
}

static gboolean
pps_view_find_player_for_media (PpsView *view,
                                PpsMedia *media)
{
	for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (view));
	     child != NULL;
	     child = gtk_widget_get_next_sibling (child)) {
		if (!GTK_IS_VIDEO (child))
			continue;

		if (g_object_get_data (G_OBJECT (child), "media") == media)
			return TRUE;
	}

	return FALSE;
}

static void
pps_view_handle_media (PpsView *view,
                       PpsMedia *media)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkWidget *player;
	PpsMappingList *media_mapping;
	PpsMapping *mapping;
	guint page;
	GFile *uri;

	page = pps_media_get_page_index (media);
	media_mapping = pps_page_cache_get_media_mapping (priv->page_cache, page);

	/* TODO: focus? */

	if (pps_view_find_player_for_media (view, media))
		return;

	uri = g_file_new_for_uri (pps_media_get_uri (media));
	player = gtk_video_new_for_file (uri);
	gtk_video_set_autoplay (GTK_VIDEO (player), TRUE);
	g_object_unref (uri);

	g_object_set_data_full (G_OBJECT (player), "media",
	                        g_object_ref (media),
	                        (GDestroyNotify) g_object_unref);

	mapping = pps_mapping_list_find (media_mapping, media);

	pps_view_put (view, player, page, &mapping->area);
}

/* Annotations */
static GtkWidget *
pps_view_create_annotation_window (PpsView *view,
                                   PpsAnnotationMarkup *annot)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkWindow *parent = GTK_WINDOW (gtk_widget_get_native (GTK_WIDGET (view)));
	GtkWidget *window;

	if (!pps_annotation_markup_has_popup (annot)) {
		PpsRectangle popup_rect, area;

		pps_annotation_get_area (PPS_ANNOTATION (annot), &area);

		popup_rect.x1 = area.x2;
		popup_rect.y1 = area.y2;
		popup_rect.x2 = popup_rect.x1 + ANNOT_POPUP_WINDOW_DEFAULT_WIDTH;
		popup_rect.y2 = popup_rect.y1 + ANNOT_POPUP_WINDOW_DEFAULT_HEIGHT;
		g_object_set (annot,
		              "rectangle", &popup_rect,
		              "has_popup", TRUE,
		              NULL);
	}

	window = pps_annotation_window_new (annot, parent);
	g_object_set_data_full (G_OBJECT (annot), "popup",
	                        g_object_ref_sink (window),
	                        NULL);

	pps_annotation_window_set_enable_spellchecking (PPS_ANNOTATION_WINDOW (window),
	                                                priv->enable_spellchecking);

	return window;
}

static void
show_annotation_windows (PpsView *view,
                         gint page)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GListModel *model;

	if (priv->annots_context == NULL)
		return;

	model = pps_annotations_context_get_annots_model (priv->annots_context);

	for (gint i = 0; i < g_list_model_get_n_items (model); i++) {
		g_autoptr (PpsAnnotation) annot = g_list_model_get_item (model, i);
		PpsAnnotationWindow *window;
		PpsAnnotationMarkup *annot_markup;

		if (pps_annotation_get_page_index (annot) != page)
			continue;

		if (!PPS_IS_ANNOTATION_MARKUP (annot))
			continue;

		annot_markup = PPS_ANNOTATION_MARKUP (annot);
		if (!pps_annotation_markup_has_popup (annot_markup))
			continue;

		window = g_object_get_data (G_OBJECT (annot), "popup");
		if (window) {
			gboolean opened = pps_annotation_window_is_open (window);
			gtk_widget_set_visible (GTK_WIDGET (window), opened);
		} else {
			pps_view_create_annotation_window (view, annot_markup);
		}
	}
}

static void
hide_annotation_windows (PpsView *view,
                         gint page)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GListModel *model = pps_annotations_context_get_annots_model (priv->annots_context);

	for (gint i = 0; i < g_list_model_get_n_items (model); i++) {
		g_autoptr (PpsAnnotation) annot = g_list_model_get_item (model, i);
		GtkWidget *window;

		if (pps_annotation_get_page_index (annot) != page)
			continue;

		if (!PPS_IS_ANNOTATION_MARKUP (annot))
			continue;

		window = g_object_get_data (G_OBJECT (annot), "popup");
		if (window)
			gtk_widget_set_visible (window, FALSE);
	}
}

static PpsAnnotation *
get_annotation_at_location (PpsView *view,
                            gdouble x,
                            gdouble y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	g_autofree PpsDocumentPoint *doc_point = NULL;
	PpsDocumentAnnotations *doc_annots;

	if (priv->annots_context == NULL)
		return NULL;

	if (!PPS_IS_DOCUMENT_ANNOTATIONS (document))
		return NULL;

	doc_annots = PPS_DOCUMENT_ANNOTATIONS (document);

	if (!doc_annots)
		return NULL;
	doc_point = pps_view_get_document_point_for_view_point (view, x, y);
	if (!doc_point)
		return NULL;

	return pps_annotations_context_get_annot_at_doc_point (priv->annots_context, doc_point);
}

static PpsMapping *
get_annotation_mapping_at_location (PpsView *view,
                                    gdouble x,
                                    gdouble y)
{
	PpsMapping *annotation_mapping = g_new (PpsMapping, 1);
	PpsAnnotation *annot;

	annot = get_annotation_at_location (view, x, y);
	if (!annot) {
		g_free (annotation_mapping);
		return NULL;
	}

	annotation_mapping->data = annot;
	pps_annotation_get_area (annot, &annotation_mapping->area);

	return annotation_mapping;
}

static gboolean
present_annotation_window_idle (gpointer user_data)
{
	GtkWindow *window = GTK_WINDOW (user_data);
	gtk_window_present (window);
	g_object_unref (window);
	return G_SOURCE_REMOVE;
}

static void
pps_view_handle_annotation (PpsView *view,
                            PpsAnnotation *annot,
                            gdouble x,
                            gdouble y,
                            guint32 timestamp)
{
	if (PPS_IS_ANNOTATION_MARKUP (annot)) {
		GtkWidget *window;
		PpsAnnotationMarkup *annot_markup = PPS_ANNOTATION_MARKUP (annot);

		if (!pps_annotation_markup_can_have_popup (annot_markup)) {
			return;
		}

		pps_annotation_markup_set_popup_is_open (annot_markup, TRUE);
		window = g_object_get_data (G_OBJECT (annot), "popup");
		if (!window)
			window = pps_view_create_annotation_window (view, annot_markup);
		pps_annotation_window_show (PPS_ANNOTATION_WINDOW (window));
		/* Present the window in an idle callback to ensure it gets focus
		 * even when opened from a popup menu that might grab focus when closing */
		g_idle_add (present_annotation_window_idle, g_object_ref (window));
	}

	if (PPS_IS_ANNOTATION_ATTACHMENT (annot)) {
		PpsAttachment *attachment;

		attachment = pps_annotation_attachment_get_attachment (PPS_ANNOTATION_ATTACHMENT (annot));
		if (attachment) {
			GError *error = NULL;
			GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (view));
			GdkAppLaunchContext *context = gdk_display_get_app_launch_context (display);
			gdk_app_launch_context_set_timestamp (context, timestamp);

			pps_attachment_open (attachment,
			                     G_APP_LAUNCH_CONTEXT (context),
			                     &error);

			if (error) {
				g_warning ("%s", error->message);
				g_error_free (error);
			}

			g_clear_object (&context);
		}
	}
}

void
pps_view_open_annotation_editor (PpsView *view,
                                 PpsAnnotation *annot)
{
	g_return_if_fail (PPS_IS_VIEW (view));
	g_return_if_fail (PPS_IS_ANNOTATION (annot));

	/* Reuse the existing annotation handling logic to open the editor
	 * Pass placeholder coordinates (0, 0) and current time since they're not
	 * needed for opening the annotation window */
	pps_view_handle_annotation (view, annot, 0.0, 0.0, GDK_CURRENT_TIME);
}

void
pps_view_focus_annotation (PpsView *view,
                           PpsAnnotation *annot)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsMapping mapping;

	if (!PPS_IS_DOCUMENT_ANNOTATIONS (pps_document_model_get_document (priv->model)))
		return;

	mapping.data = g_object_ref (annot);
	pps_annotation_get_area (annot, &mapping.area);

	_pps_view_set_focused_element (view, &mapping,
	                               pps_annotation_get_page_index (annot));
	g_object_unref (annot);
}

static void
pps_view_rerender_annotation (PpsView *view,
                              PpsAnnotation *annot)
{
	PpsRectangle doc_rect;
	guint page_index;
	GdkRectangle view_rect;
	cairo_region_t *region;
	guint scroll_x, scroll_y;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsAnnotationEditingState state = pps_document_model_get_annotation_editing_state (priv->model);

	/* it is not necessary to render again if the annotation is hidden */
	if (state & PPS_ANNOTATION_EDITING_STATE_TEXT && PPS_IS_ANNOTATION_FREE_TEXT (annot)) {
		return;
	}
	if (PPS_IS_ANNOTATION_STAMP (annot)) {
		/* Skip re-render while the overlay is in image mode (page already renders
		 * without the stamp). Re-rendering on every drag position update would
		 * cause visible flicker. */
		PpsRenderAnnotsFlags rendered = pps_pixbuf_cache_rendered_state (priv->pixbuf_cache,
		                                                                 (gint) pps_annotation_get_page_index (annot));
		if (!(rendered & PPS_RENDER_ANNOTS_STAMP))
			return;
	}
	if (state & PPS_ANNOTATION_EDITING_STATE_INK && PPS_IS_ANNOTATION_INK (annot)) {
		return;
	}

	g_return_if_fail (PPS_IS_VIEW (view));
	g_return_if_fail (PPS_IS_ANNOTATION (annot));

	get_scroll_offset (view, &scroll_x, &scroll_y);
	page_index = pps_annotation_get_page_index (annot);
	pps_annotation_get_area (annot, &doc_rect);
	_pps_view_transform_doc_rect_to_view_rect (view, page_index,
	                                           &doc_rect, &view_rect);
	view_rect.x -= scroll_x;
	view_rect.y -= scroll_y;
	region = cairo_region_create_rectangle (&view_rect);
	pps_view_reload_page (view, page_index, region);
	cairo_region_destroy (region);
}

static void
pps_view_annot_changed_cb (PpsAnnotation *annot,
                           GParamSpec *pspec,
                           PpsView *view)
{
	pps_view_rerender_annotation (view, annot);
}

static void
pps_view_connect_annot_signals (PpsView *view,
                                PpsAnnotation *annot)
{
	g_signal_connect_object (annot,
	                         "notify::rgba",
	                         G_CALLBACK (pps_view_annot_changed_cb),
	                         view,
	                         G_CONNECT_DEFAULT);
	g_signal_connect_object (annot,
	                         "notify::area",
	                         G_CALLBACK (pps_view_annot_changed_cb),
	                         view,
	                         G_CONNECT_DEFAULT);
	g_signal_connect_object (annot,
	                         "notify::hidden",
	                         G_CALLBACK (pps_view_annot_changed_cb),
	                         view,
	                         G_CONNECT_DEFAULT);

	if (PPS_IS_ANNOTATION_MARKUP (annot)) {
		g_signal_connect_object (annot,
		                         "notify::opacity",
		                         G_CALLBACK (pps_view_annot_changed_cb),
		                         view,
		                         G_CONNECT_DEFAULT);
	}

	if (PPS_IS_ANNOTATION_TEXT_MARKUP (annot)) {
		g_signal_connect_object (annot,
		                         "notify::type",
		                         G_CALLBACK (pps_view_annot_changed_cb),
		                         view,
		                         G_CONNECT_DEFAULT);
	}

	if (PPS_IS_ANNOTATION_FREE_TEXT (annot)) {
		g_signal_connect_object (annot,
		                         "notify::font-desc",
		                         G_CALLBACK (pps_view_annot_changed_cb),
		                         view,
		                         G_CONNECT_DEFAULT);
		g_signal_connect_object (annot,
		                         "notify::contents",
		                         G_CALLBACK (pps_view_annot_changed_cb),
		                         view,
		                         G_CONNECT_DEFAULT);
		g_signal_connect_object (annot,
		                         "notify::font-rgba",
		                         G_CALLBACK (pps_view_annot_changed_cb),
		                         view,
		                         G_CONNECT_DEFAULT);
	}

	if (PPS_IS_ANNOTATION_INK (annot)) {
		g_signal_connect_object (annot,
		                         "notify::ink-list",
		                         G_CALLBACK (pps_view_annot_changed_cb),
		                         view,
		                         G_CONNECT_DEFAULT);
	}

	if (PPS_IS_ANNOTATION_TEXT (annot)) {
		g_signal_connect_object (annot,
		                         "notify::icon",
		                         G_CALLBACK (pps_view_annot_changed_cb),
		                         view,
		                         G_CONNECT_DEFAULT);
	}
}

static void
pps_view_editing_state_changed (PpsDocumentModel *model,
                                GParamSpec *pspec,
                                PpsView *view)
{
	gtk_widget_queue_allocate (GTK_WIDGET (view));
}

static void
pps_view_annot_added_cb (PpsView *view,
                         gpointer *user_data)
{
	PpsAnnotation *annot = PPS_ANNOTATION (user_data);

	if (pps_view_has_selection (view))
		clear_selection (view);

	pps_view_rerender_annotation (view, annot);
	pps_view_connect_annot_signals (view, annot);

	if (PPS_IS_ANNOTATION_TEXT (annot)) {
		GtkWidget *window = pps_view_create_annotation_window (view, PPS_ANNOTATION_MARKUP (annot));
		pps_annotation_window_show (PPS_ANNOTATION_WINDOW (window));
	}
}

static void
pps_view_annot_removed_cb (PpsView *view,
                           gpointer *user_data)
{
	PpsAnnotation *annot = PPS_ANNOTATION (user_data);
	GtkWindow *window = g_object_get_data (G_OBJECT (annot), "popup");

	// This is a hack to fix
	// https://gitlab.gnome.org/GNOME/papers/-/issues/383
	// in stable
	if (window)
		gtk_window_destroy (window);

	_pps_view_set_focused_element (view, NULL, -1);

	pps_view_rerender_annotation (view, annot);
}

static void
pps_view_annots_loaded_cb (PpsAnnotationsContext *annot_context,
                           PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	GListModel *model = pps_annotations_context_get_annots_model (priv->annots_context);
	gint i;
	PpsAnnotation *annot;

	for (i = 0, annot = g_list_model_get_item (model, i);
	     annot != NULL;
	     annot = g_list_model_get_item (model, ++i)) {
		pps_view_connect_annot_signals (view, annot);
	}
}

/**
 * pps_view_set_annotations_context:
 * @view: a #PpsView
 * @context: (not nullable): the #PpsAnnotationsContext to set
 *
 * Since: 48.0
 */
void
pps_view_set_annotations_context (PpsView *view,
                                  PpsAnnotationsContext *context)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_return_if_fail (PPS_IS_VIEW (view));
	g_return_if_fail (PPS_IS_ANNOTATIONS_CONTEXT (context));

	if (priv->annots_context) {
		g_signal_handlers_disconnect_by_data (priv->annots_context, view);
	}
	g_set_object (&priv->annots_context, context);
	g_signal_connect_object (priv->annots_context, "annot-added",
	                         G_CALLBACK (pps_view_annot_added_cb),
	                         view, G_CONNECT_SWAPPED);
	g_signal_connect_object (priv->annots_context, "annot-removed",
	                         G_CALLBACK (pps_view_annot_removed_cb),
	                         view, G_CONNECT_SWAPPED);
	g_signal_connect_object (priv->annots_context, "annots-loaded",
	                         G_CALLBACK (pps_view_annots_loaded_cb),
	                         view, G_CONNECT_DEFAULT);
}

/* Caret navigation */
#define CURSOR_ON_MULTIPLIER 2
#define CURSOR_OFF_MULTIPLIER 1
#define CURSOR_PEND_MULTIPLIER 3
#define CURSOR_DIVIDER 3

static inline gboolean
cursor_is_in_visible_page (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	return (priv->cursor_page == priv->current_page ||
	        (priv->cursor_page >= priv->start_page &&
	         priv->cursor_page <= priv->end_page));
}

static gboolean
cursor_should_blink (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble scale = pps_document_model_get_scale (priv->model);

	if (priv->caret_enabled &&
	    pps_document_model_get_rotation (priv->model) == 0 &&
	    cursor_is_in_visible_page (view) &&
	    gtk_widget_has_focus (GTK_WIDGET (view)) &&
	    priv->pixbuf_cache &&
	    !pps_pixbuf_cache_get_selection_region (priv->pixbuf_cache, priv->cursor_page, scale)) {
		GtkSettings *settings;
		gboolean blink;

		settings = gtk_widget_get_settings (GTK_WIDGET (view));
		g_object_get (settings, "gtk-cursor-blink", &blink, NULL);

		return blink;
	}

	return FALSE;
}

static gint
get_cursor_blink_time (PpsView *view)
{
	GtkSettings *settings = gtk_widget_get_settings (GTK_WIDGET (view));
	gint time;

	g_object_get (settings, "gtk-cursor-blink-time", &time, NULL);

	return time;
}

static gint
get_cursor_blink_timeout_id (PpsView *view)
{
	GtkSettings *settings = gtk_widget_get_settings (GTK_WIDGET (view));
	gint timeout;

	g_object_get (settings, "gtk-cursor-blink-timeout", &timeout, NULL);

	return timeout;
}

static gboolean
get_caret_cursor_area (PpsView *view,
                       gint page,
                       gint offset,
                       GdkRectangle *area)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsRectangle *areas = NULL;
	PpsRectangle *doc_rect;
	guint n_areas = 0;
	gdouble cursor_aspect_ratio;
	gint stem_width;
	guint scroll_x, scroll_y;

	if (!priv->caret_enabled || pps_document_model_get_rotation (priv->model) != 0)
		return FALSE;

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_layout (priv->page_cache, page, &areas, &n_areas);
	if (!areas)
		return FALSE;

	if (offset > n_areas)
		return FALSE;

	get_scroll_offset (view, &scroll_x, &scroll_y);

	doc_rect = areas + offset;
	if (offset == n_areas ||
	    ((doc_rect->x1 == doc_rect->x2 || doc_rect->y1 == doc_rect->y2) && offset > 0)) {
		PpsRectangle *prev;
		PpsRectangle last_rect;

		/* Special characters like \n have an empty bounding box
		 * and the end of a page doesn't have any bounding box,
		 * use the size of the previous area.
		 */
		prev = areas + offset - 1;
		last_rect.x1 = prev->x2;
		last_rect.y1 = prev->y1;
		last_rect.x2 = prev->x2 + (prev->x2 - prev->x1);
		last_rect.y2 = prev->y2;

		_pps_view_transform_doc_rect_to_view_rect (view, page, &last_rect, area);
	} else {
		_pps_view_transform_doc_rect_to_view_rect (view, page, doc_rect, area);
	}

	area->x -= scroll_x;
	area->y -= scroll_y;

	g_object_get (gtk_settings_get_for_display (gtk_widget_get_display (GTK_WIDGET (view))),
	              "gtk-cursor-aspect-ratio", &cursor_aspect_ratio,
	              NULL);

	stem_width = area->height * cursor_aspect_ratio + 1;
	area->x -= (stem_width / 2);
	area->width = stem_width;

	return TRUE;
}

static void
show_cursor (PpsView *view)
{
	GtkWidget *widget;
	GdkRectangle view_rect;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (priv->cursor_visible)
		return;

	widget = GTK_WIDGET (view);
	priv->cursor_visible = TRUE;
	if (gtk_widget_has_focus (widget) &&
	    get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &view_rect)) {
		gtk_widget_queue_draw (widget);
	}
}

static void
hide_cursor (PpsView *view)
{
	GtkWidget *widget;
	GdkRectangle view_rect;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!priv->cursor_visible)
		return;

	widget = GTK_WIDGET (view);
	priv->cursor_visible = FALSE;
	if (gtk_widget_has_focus (widget) &&
	    get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &view_rect)) {
		gtk_widget_queue_draw (widget);
	}
}

static gboolean
blink_cb (PpsView *view)
{
	gint blink_timeout;
	guint blink_time;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	blink_timeout = get_cursor_blink_timeout_id (view);
	if (priv->cursor_blink_time > 1000 * blink_timeout && blink_timeout < G_MAXINT / 1000) {
		/* We've blinked enough without the user doing anything, stop blinking */
		show_cursor (view);
		priv->cursor_blink_timeout_id = 0;

		return G_SOURCE_REMOVE;
	}

	blink_time = get_cursor_blink_time (view);
	if (priv->cursor_visible) {
		hide_cursor (view);
		blink_time *= CURSOR_OFF_MULTIPLIER;
	} else {
		show_cursor (view);
		priv->cursor_blink_time += blink_time;
		blink_time *= CURSOR_ON_MULTIPLIER;
	}

	priv->cursor_blink_timeout_id = g_timeout_add (blink_time / CURSOR_DIVIDER, (GSourceFunc) blink_cb, view);

	return G_SOURCE_REMOVE;
}

static void
pps_view_check_cursor_blink (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	if (cursor_should_blink (view)) {
		if (priv->cursor_blink_timeout_id == 0) {
			show_cursor (view);
			priv->cursor_blink_timeout_id = g_timeout_add (get_cursor_blink_time (view) * CURSOR_ON_MULTIPLIER / CURSOR_DIVIDER,
			                                               (GSourceFunc) blink_cb, view);
		}

		return;
	}

	g_clear_handle_id (&priv->cursor_blink_timeout_id, g_source_remove);

	priv->cursor_visible = TRUE;
	priv->cursor_blink_time = 0;
}

static void
pps_view_pend_cursor_blink (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	if (!cursor_should_blink (view))
		return;

	g_clear_handle_id (&priv->cursor_blink_timeout_id, g_source_remove);

	show_cursor (view);
	priv->cursor_blink_timeout_id = g_timeout_add (get_cursor_blink_time (view) * CURSOR_PEND_MULTIPLIER / CURSOR_DIVIDER,
	                                               (GSourceFunc) blink_cb, view);
}

static void
preload_pages_for_caret_navigation (PpsView *view)
{
	gint n_pages;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!document)
		return;

	/* Upload to the cache the first and last pages,
	 * this information is needed to position the cursor
	 * in the beginning/end of the document, for example
	 * when pressing <Ctr>Home/End
	 */
	n_pages = pps_document_get_n_pages (document);

	/* For documents with at least 3 pages, those are already cached anyway */
	if (n_pages > 0 && n_pages <= 3)
		return;

	pps_page_cache_ensure_page (priv->page_cache, 0);
	pps_page_cache_ensure_page (priv->page_cache, n_pages - 1);
}

/**
 * pps_view_supports_caret_navigation:
 * @view: a #PpsView
 *
 * Returns: whether the document supports caret navigation
 */
gboolean
pps_view_supports_caret_navigation (PpsView *view)
{
	PpsDocumentTextInterface *iface;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!document || !PPS_IS_DOCUMENT_TEXT (document))
		return FALSE;

	iface = PPS_DOCUMENT_TEXT_GET_IFACE (document);
	if (!iface->get_text_layout || !iface->get_text)
		return FALSE;

	return TRUE;
}

/**
 * pps_view_set_caret_navigation_enabled:
 * @view: a #PpsView
 * @enabled: whether to enable caret navigation mode
 *
 * Enables or disables caret navigation mode for the document.
 */
void
pps_view_set_caret_navigation_enabled (PpsView *view,
                                       gboolean enabled)
{
	g_return_if_fail (PPS_IS_VIEW (view));
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (priv->caret_enabled != enabled) {
		priv->caret_enabled = enabled;
		if (priv->caret_enabled)
			preload_pages_for_caret_navigation (view);

		pps_view_check_cursor_blink (view);

		if (cursor_is_in_visible_page (view))
			gtk_widget_queue_draw (GTK_WIDGET (view));
	}
}

/**
 * pps_view_get_caret_navigation_enabled:
 * @view: a #PpsView
 *
 * Returns: whether caret navigation mode is enabled for the document
 */
gboolean
pps_view_is_caret_navigation_enabled (PpsView *view)
{
	g_return_val_if_fail (PPS_IS_VIEW (view), FALSE);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	return priv->caret_enabled;
}

/**
 * pps_view_set_caret_cursor_position:
 * @view: a #PpsView
 * @page:
 * @offset:
 */
void
pps_view_set_caret_cursor_position (PpsView *view,
                                    guint page,
                                    guint offset)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	g_return_if_fail (PPS_IS_VIEW (view));
	g_return_if_fail (PPS_IS_DOCUMENT (document));
	g_return_if_fail (page < pps_document_get_n_pages (document));

	if (priv->cursor_page != page || priv->cursor_offset != offset) {
		priv->cursor_page = page;
		priv->cursor_offset = offset;

		g_signal_emit (view, signals[SIGNAL_CURSOR_MOVED], 0,
		               priv->cursor_page, priv->cursor_offset);

		if (priv->caret_enabled && cursor_is_in_visible_page (view))
			gtk_widget_queue_draw (GTK_WIDGET (view));
	}
}
/*** GtkWidget implementation ***/

static void
pps_view_size_request_continuous_dual_page (PpsView *view,
                                            GtkRequisition *requisition)
{
	gint n_pages;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	n_pages = pps_document_get_n_pages (pps_document_model_get_document (priv->model)) + 1;
	get_page_y_offset (view, n_pages, &requisition->height);

	if (pps_document_model_get_sizing_mode (priv->model) == PPS_SIZING_FREE) {
		gint max_width;

		pps_view_get_max_page_size (view, &max_width, NULL);
		requisition->width = max_width * 2 + priv->spacing * 3;
	} else {
		requisition->width = 1;
	}
}

static void
pps_view_size_request_continuous (PpsView *view,
                                  GtkRequisition *requisition)
{
	gint n_pages;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	n_pages = pps_document_get_n_pages (pps_document_model_get_document (priv->model));
	get_page_y_offset (view, n_pages, &requisition->height);

	if (pps_document_model_get_sizing_mode (priv->model) == PPS_SIZING_FREE) {
		gint max_width;

		pps_view_get_max_page_size (view, &max_width, NULL);
		requisition->width = max_width + priv->spacing * 2;
	} else {
		requisition->width = 1;
	}
}

static void
pps_view_size_request_dual_page (PpsView *view,
                                 GtkRequisition *requisition)
{
	gint width, height;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	PpsSizingMode sizing_mode = pps_document_model_get_sizing_mode (priv->model);

	/* Find the largest of the two. */
	pps_view_get_page_size (view,
	                        priv->current_page,
	                        &width, &height);
	if (priv->current_page + 1 < pps_document_get_n_pages (document)) {
		gint width_2, height_2;
		pps_view_get_page_size (view,
		                        priv->current_page + 1,
		                        &width_2, &height_2);
		if (width_2 > width) {
			width = width_2;
			height = height_2;
		}
	}

	if (sizing_mode == PPS_SIZING_FIT_PAGE) {
		requisition->height = 1;
	} else {
		requisition->height = height + priv->spacing * 2;
	}

	if (sizing_mode == PPS_SIZING_FREE) {
		requisition->width = width * 2 + priv->spacing * 3;
	} else {
		requisition->width = 1;
	}
}

static void
pps_view_size_request_single_page (PpsView *view,
                                   GtkRequisition *requisition)
{
	gint width, height;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsSizingMode sizing_mode = pps_document_model_get_sizing_mode (priv->model);

	pps_view_get_page_size (view, priv->current_page, &width, &height);

	if (sizing_mode == PPS_SIZING_FIT_PAGE) {
		requisition->height = 1;
	} else {
		requisition->height = height + (2 * priv->spacing);
	}

	if (sizing_mode == PPS_SIZING_FREE) {
		requisition->width = width + (2 * priv->spacing);
	} else {
		requisition->width = 1;
	}
}

static void
pps_view_size_request (GtkWidget *widget,
                       GtkRequisition *requisition)
{
	PpsView *view = PPS_VIEW (widget);
	gboolean dual_page, continuous;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (pps_document_model_get_document (priv->model) == NULL) {
		priv->requisition.width = 1;
		priv->requisition.height = 1;
	} else {
		dual_page = is_dual_page (view, NULL);
		continuous = pps_document_model_get_continuous (priv->model);

		if (continuous && dual_page)
			pps_view_size_request_continuous_dual_page (view, &priv->requisition);
		else if (continuous)
			pps_view_size_request_continuous (view, &priv->requisition);
		else if (dual_page)
			pps_view_size_request_dual_page (view, &priv->requisition);
		else
			pps_view_size_request_single_page (view, &priv->requisition);
	}

	if (requisition)
		*requisition = priv->requisition;
}

static void
pps_view_measure (GtkWidget *widget,
                  GtkOrientation orientation,
                  int for_size,
                  int *minimum,
                  int *natural,
                  int *minimum_baseline,
                  int *natural_baseline)
{
	GtkRequisition requisition;

	pps_view_size_request (widget, &requisition);

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		*minimum = *natural = requisition.width;

	if (orientation == GTK_ORIENTATION_VERTICAL)
		*minimum = *natural = requisition.height;
}

static void
pps_view_size_allocate (GtkWidget *widget,
                        int width,
                        int height,
                        int baseline)
{
	PpsView *view = PPS_VIEW (widget);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsSizingMode sizing_mode = pps_document_model_get_sizing_mode (priv->model);
	GdkRectangle view_area;
	guint scroll_x, scroll_y;
	gint x, y;

	if (!pps_document_model_get_document (priv->model))
		return;

	if (sizing_mode == PPS_SIZING_FIT_WIDTH ||
	    sizing_mode == PPS_SIZING_FIT_PAGE ||
	    sizing_mode == PPS_SIZING_AUTOMATIC) {
		pps_view_zoom_for_size (view, width, height);
		pps_view_size_request (widget, NULL);
	}

	pps_view_update_adjustment_values (view);
	view_update_range_and_current_page (view);
	priv->pending_scroll = SCROLL_TO_KEEP_POSITION;

	get_scroll_offset (view, &scroll_x, &scroll_y);

	view_area.x = 0;
	view_area.y = 0;
	view_area.width = width;
	view_area.height = height;

	for (guint i = 0; i < priv->page_widgets->len; i++) {
		GdkRectangle page_area;
		PpsViewPage *page = g_ptr_array_index (priv->page_widgets, i);
		gint page_index = pps_view_page_get_page (page);

		if (page_index < 0) {
			gtk_widget_set_child_visible (GTK_WIDGET (page), FALSE);
			continue;
		}

		if (!pps_document_model_get_continuous (priv->model) && !(priv->start_page <= page_index && page_index <= priv->end_page)) {
			gtk_widget_set_child_visible (GTK_WIDGET (page), FALSE);
			continue;
		}

		pps_view_get_page_extents (view, page_index, &page_area);
		page_area.x -= scroll_x;
		page_area.y -= scroll_y;

		// TODO: should use CSS padding box
		gtk_widget_set_child_visible (GTK_WIDGET (page), gdk_rectangle_intersect (&page_area, &view_area, NULL));
		gtk_widget_size_allocate (GTK_WIDGET (page), &page_area, baseline);
	}

	for (GtkWidget *child = gtk_widget_get_first_child (widget);
	     child != NULL;
	     child = gtk_widget_get_next_sibling (child)) {
		PpsViewChild *data = g_object_get_data (G_OBJECT (child), "pps-child");
		GdkRectangle view_area;

		if (!data || !gtk_widget_get_visible (child))
			continue;

		_pps_view_transform_doc_rect_to_view_rect (view, data->page, &data->doc_rect, &view_area);
		view_area.x -= scroll_x;
		view_area.y -= scroll_y;

		gtk_widget_set_size_request (child, view_area.width, view_area.height);
		// TODO: this is a temporary solution to eliminate the warning
		gtk_widget_measure (child, GTK_ORIENTATION_HORIZONTAL, view_area.height, NULL, NULL, NULL, NULL);
		gtk_widget_size_allocate (child, &view_area, baseline);
	}

	if (pps_document_misc_get_pointer_position (GTK_WIDGET (view), &x, &y))
		pps_view_handle_cursor_over_xy (view, x, y);

	if (priv->link_preview.popover)
		gtk_popover_present (GTK_POPOVER (priv->link_preview.popover));
}

static gboolean
scroll_to_zoom_cb (GtkEventControllerScroll *self, gdouble dx, gdouble dy, GtkWidget *widget)
{
	PpsView *view = PPS_VIEW (widget);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	guint state;
	gint x, y;

	state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (self)) & gtk_accelerator_get_default_mod_mask ();

	if (state == GDK_CONTROL_MASK) {
		pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FREE);

		pps_document_misc_get_pointer_position (widget, &x, &y);

		priv->zoom_center_x = x;
		priv->zoom_center_y = y;

		gdouble delta = dx + dy;

		if (gtk_event_controller_scroll_get_unit (self) == GDK_SCROLL_UNIT_SURFACE)
			delta = delta / 20.;

		gdouble factor = pow (delta < 0 ? ZOOM_IN_FACTOR : ZOOM_OUT_FACTOR, fabs (delta));

		if (pps_view_can_zoom (view, factor))
			pps_view_zoom (view, factor);
		return TRUE;
	}

	return FALSE;
}

/* This is based on the deprecated function gtk_draw_insertion_cursor. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void
draw_caret_cursor (PpsView *view,
                   GtkSnapshot *snapshot)
{
	GdkRectangle view_rect;
	GdkRGBA cursor_color;
	GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (view));
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &view_rect))
		return;

	gtk_style_context_get_color (context, &cursor_color);

	gtk_snapshot_save (snapshot);

	gtk_snapshot_append_color (snapshot, &cursor_color,
	                           &GRAPHENE_RECT_INIT (
				       view_rect.x,
				       view_rect.y,
				       view_rect.width,
				       view_rect.height));

	gtk_snapshot_restore (snapshot);
}
G_GNUC_END_IGNORE_DEPRECATIONS

static gboolean
should_draw_caret_cursor (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble scale = pps_document_model_get_scale (priv->model);
	GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (view))));

	return (priv->caret_enabled &&
	        priv->cursor_visible &&
	        focus && gtk_widget_get_parent (focus) == GTK_WIDGET (view) &&
	        !pps_pixbuf_cache_get_selection_region (priv->pixbuf_cache, priv->cursor_page, scale));
}

/*
 * TODO: This should live in PpsViewPage, but for that PpsView and its
 * PpsViewPages either need a shared location for focused elements or the widget
 * focus needs to be moved to PpsViewPage. The latter will need to happen in the
 * future for accessibility reasons, as well. So, this will be easily moved at
 * one point.
 */
static void
draw_focus (PpsView *view,
            GtkSnapshot *snapshot)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkWidget *widget = GTK_WIDGET (view);
	GdkRectangle rect;

	if (!gtk_widget_has_focus (widget))
		return;

	if (priv->start_page > priv->focused_element_page || priv->focused_element_page < priv->end_page)
		return;

	if (!pps_view_get_focused_area (view, &rect))
		return;

	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	gtk_snapshot_render_focus (snapshot,
	                           gtk_widget_get_style_context (widget),
	                           rect.x,
	                           rect.y,
	                           rect.width,
	                           rect.height);
	G_GNUC_END_IGNORE_DEPRECATIONS
}

static void
draw_signing_rect (PpsView *view,
                   GtkSnapshot *snapshot)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GskRoundedRect outline;
	graphene_rect_t rect;
	GdkRGBA bg_color;
	GdkRGBA border_color;

	if (!gtk_gesture_is_active (GTK_GESTURE (priv->signing_drag_gesture)))
		return;

	rect = GRAPHENE_RECT_INIT (priv->signing_info.start_x,
	                           priv->signing_info.start_y,
	                           priv->signing_info.stop_x - priv->signing_info.start_x,
	                           priv->signing_info.stop_y - priv->signing_info.start_y);

	get_accent_color (&bg_color, NULL);
	border_color.alpha = 0.2;
	get_accent_color (&border_color, NULL);
	bg_color.alpha = 0.35;

	gtk_snapshot_save (snapshot);
	gsk_rounded_rect_init_from_rect (&outline, &rect, 1);
	gtk_snapshot_append_color (snapshot, &bg_color, &rect);
	gtk_snapshot_append_border (snapshot, &outline, (float[4]) { 1, 1, 1, 1 }, (GdkRGBA[4]) { border_color, border_color, border_color, border_color });
	gtk_snapshot_restore (snapshot);
}

static void
pps_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	int width, height;
	PpsView *view = PPS_VIEW (widget);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	guint scroll_x, scroll_y;

	get_scroll_offset (view, &scroll_x, &scroll_y);

	width = gtk_widget_get_width (widget);
	height = gtk_widget_get_height (widget);

	if (pps_document_model_get_document (priv->model) == NULL)
		return;

	gtk_snapshot_push_clip (snapshot, &GRAPHENE_RECT_INIT (0, 0, width, height));

	/* snapshot child widgets */
	GTK_WIDGET_CLASS (pps_view_parent_class)->snapshot (widget, snapshot);

	if (priv->focused_element)
		draw_focus (view, snapshot);

	if (should_draw_caret_cursor (view))
		draw_caret_cursor (view, snapshot);

	draw_signing_rect (view, snapshot);

	gtk_snapshot_pop (snapshot);
}

static void
pps_view_set_focused_element_at_location (PpsView *view,
                                          gdouble x,
                                          gdouble y)
{
	PpsMapping *mapping;
	gint page;

	mapping = get_annotation_mapping_at_location (view, x, y);
	if (mapping) {
		page = pps_annotation_get_page_index ((PpsAnnotation *) mapping->data);
		_pps_view_set_focused_element (view, mapping, page);
		g_free (mapping);
		return;
	}

	mapping = get_link_mapping_at_location (view, x, y, &page);
	if (mapping) {
		_pps_view_set_focused_element (view, mapping, page);
		return;
	}

	_pps_view_set_focused_element (view, NULL, -1);
}

static gboolean
pps_view_do_popup_menu (PpsView *view,
                        gdouble x,
                        gdouble y)
{
	GList *items = NULL;
	PpsLink *link;
	PpsImage *image;
	PpsAnnotation *annot;

	/* Only set focused element if there's no active text selection.
	 * This preserves the selection for annotation tools in the context menu
	 * and ensures consistency between mouse and touch interactions. */
	if (!pps_view_has_selection (view))
		pps_view_set_focused_element_at_location (view, x, y);

	image = pps_view_get_image_at_location (view, x, y);
	if (image)
		items = g_list_prepend (items, image);

	link = pps_view_get_link_at_location (view, x, y);
	if (link)
		items = g_list_prepend (items, link);

	annot = get_annotation_at_location (view, x, y);
	if (annot)
		items = g_list_prepend (items, annot);

	g_signal_emit (view, signals[SIGNAL_POPUP_MENU], 0, items, x, y);

	g_list_free (items);

	return TRUE;
}

static void
get_link_area (PpsView *view,
               gint page,
               PpsLink *link,
               GdkRectangle *area)
{
	PpsMappingList *link_mapping;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	link_mapping = pps_page_cache_get_link_mapping (priv->page_cache, page);
	pps_view_get_area_from_mapping (view, page,
	                                link_mapping,
	                                link, area);
}

static void
link_preview_set_thumbnail (GdkTexture *page_texture,
                            PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkWidget *popover = priv->link_preview.popover;
	GtkWidget *picture;
	GtkSnapshot *snapshot;
	gdouble x, y;         /* position of the link on destination page */
	gint pwidth, pheight; /* dimensions of destination page */
	gint vwidth, vheight; /* dimensions of main view */
	gint width, height;   /* dimensions of popup */
	gint left, top;
	gdouble scale;
	GtkNative *native = gtk_widget_get_native (GTK_WIDGET (view));
	gdouble fractional_scale = gdk_surface_get_scale (gtk_native_get_surface (native));
	g_autoptr (GdkPaintable) paintable = NULL;

	scale = gdk_surface_get_scale (gtk_native_get_surface (gtk_widget_get_native (GTK_WIDGET (view))));

	x = priv->link_preview.left;
	y = priv->link_preview.top;

	pwidth = (gint) gdk_texture_get_width (page_texture) / scale;
	pheight = (gint) gdk_texture_get_height (page_texture) / scale;

	vwidth = gtk_widget_get_width (GTK_WIDGET (view));
	vheight = gtk_widget_get_height (GTK_WIDGET (view));

	/* Horizontally, we try to display the full width of the destination
	 * page. This is needed to make the popup useful for two-column papers.
	 * Vertically, we limit the height to maximally LINK_PREVIEW_PAGE_RATIO
	 * of the main view. The idea is avoid the popup dominte the main view,
	 * and the reader can see context both in the popup and the main page.
	 */
	width = MIN (pwidth, vwidth);
	height = MIN (pheight, (int) (vheight * LINK_PREVIEW_PAGE_RATIO));

	/* Position on the destination page that will be in the top left
	 * corner of the popup. We choose the link destination to be centered
	 * horizontally, and slightly above the center vertically. This is a
	 * compromise given that a link contains only (x,y) information for a
	 * single point, and some links have their (x,y) point to the top left
	 * of their main content (e.g. section headers, bibliographic
	 * references, footnotes, and tables), while other links have their
	 * (x,y) point to the center right of the main contents (e.g.
	 * equations). Also, figures usually have their (x,y) point to the
	 * caption below the figure, so seeing a little of the figure above is
	 * often enough to remind the reader of the rest of the figure.
	 */
	left = x - width * LINK_PREVIEW_HORIZONTAL_LINK_POS;
	top = y - height * LINK_PREVIEW_VERTICAL_LINK_POS;

	/* link preview destination should stay within the destination page: */
	left = MIN (MAX (0, left), pwidth - width);
	top = MIN (MAX (0, top), pheight - height);

	snapshot = gtk_snapshot_new ();
	gtk_snapshot_push_clip (snapshot, &GRAPHENE_RECT_INIT (0, 0, width, height));

	/* snap the texture to a physical pixel so it is not blurred */
	gtk_snapshot_save (snapshot);
	gtk_snapshot_scale (snapshot, 1 / fractional_scale, 1 / fractional_scale);
	draw_surface (snapshot,
	              page_texture,
	              &GRAPHENE_POINT_INIT (-left * fractional_scale, -top * fractional_scale),
	              &GRAPHENE_RECT_INIT (0, 0, ceil (pwidth * fractional_scale), ceil (pheight * fractional_scale)),
	              pps_document_model_get_inverted_colors (priv->model));
	gtk_snapshot_restore (snapshot);

	gtk_snapshot_pop (snapshot);

	paintable = gtk_snapshot_free_to_paintable (snapshot, NULL);
	picture = gtk_picture_new_for_paintable (paintable);
	gtk_widget_set_size_request (popover, width, height);
	gtk_popover_set_child (GTK_POPOVER (popover), picture);
}

static void
link_preview_delayed_show (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	gtk_popover_popup (GTK_POPOVER (priv->link_preview.popover));

	priv->link_preview.delay_timeout_id = 0;
}

static void
link_preview_job_finished_cb (PpsJobThumbnailTexture *job,
                              PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_clear_object (&priv->link_preview.job);

	if (!pps_job_is_succeeded (PPS_JOB (job), NULL)) {
		pps_view_link_preview_popover_cleanup (view);
		return;
	}

	link_preview_set_thumbnail (pps_job_thumbnail_texture_get_texture (job),
	                            view);
}

static void
pps_view_link_preview_popover_cleanup (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_clear_handle_id (&priv->link_preview.delay_timeout_id, g_source_remove);

	if (priv->link_preview.job) {
		pps_job_cancel (priv->link_preview.job);
		g_clear_object (&priv->link_preview.job);
	}

	if (priv->link_preview.popover) {
		gtk_popover_popdown (GTK_POPOVER (priv->link_preview.popover));
		g_clear_pointer (&priv->link_preview.popover, gtk_widget_unparent);
	}
}

static gboolean
pps_view_query_tooltip (GtkWidget *widget,
                        gint x,
                        gint y,
                        gboolean keyboard_tip,
                        GtkTooltip *tooltip)
{
	PpsView *view = PPS_VIEW (widget);
	PpsLink *link;
	PpsAnnotation *annot;
	gchar *text;
	guint scroll_x, scroll_y;

	annot = get_annotation_at_location (view, x, y);
	if (annot) {
		const gchar *contents;
		const gchar *label = NULL;
		const gchar *modified;
		GdkRectangle view_area;
		guint page_index;
		PpsRectangle annot_area;
		g_autofree gchar *tooltip_text = NULL;
		g_autofree gchar *escaped_label = NULL;
		g_autofree gchar *escaped_contents = NULL;

		get_scroll_offset (view, &scroll_x, &scroll_y);

		contents = pps_annotation_get_contents (annot);
		modified = pps_annotation_get_modified (annot);
		if (PPS_IS_ANNOTATION_MARKUP (annot))
			label = pps_annotation_markup_get_label (PPS_ANNOTATION_MARKUP (annot));

		page_index = pps_annotation_get_page_index (annot);
		pps_annotation_get_area (annot, &annot_area);
		_pps_view_transform_doc_rect_to_view_rect (view,
		                                           page_index,
		                                           &annot_area,
		                                           &view_area);
		view_area.x -= scroll_x;
		view_area.y -= scroll_y;

		if (label && *label != '\0')
			escaped_label = g_markup_escape_text (label, -1);
		if (contents && *contents != '\0')
			escaped_contents = g_markup_escape_text (contents, -1);

		/* Build rich tooltip with author, date, and contents */
		if (escaped_label || modified || escaped_contents) {
			GString *markup = g_string_new (NULL);

			if (escaped_label)
				g_string_append_printf (markup, "<b>%s</b>", escaped_label);

			if (modified && *modified != '\0') {
				if (markup->len > 0)
					g_string_append_c (markup, '\n');
				g_string_append (markup, modified);
			}

			if (escaped_contents) {
				if (markup->len > 0)
					g_string_append_c (markup, '\n');
				g_string_append (markup, escaped_contents);
			}

			tooltip_text = g_string_free (markup, FALSE);
			gtk_tooltip_set_markup (tooltip, tooltip_text);
			gtk_tooltip_set_tip_area (tooltip, &view_area);
			return TRUE;
		}
	}

	link = pps_view_get_link_at_location (view, x, y);
	if (!link)
		return FALSE;

	text = tip_from_link (view, link);
	if (text && g_utf8_validate (text, -1, NULL)) {
		GdkRectangle link_area;
		gint page;

		find_page_at_location (view, x, y, &page, NULL, NULL);
		get_link_area (view, page, link, &link_area);
		gtk_tooltip_set_text (tooltip, text);
		gtk_tooltip_set_tip_area (tooltip, &link_area);
		g_free (text);

		return TRUE;
	}
	g_free (text);

	return FALSE;
}

gint
_pps_view_get_caret_cursor_offset_at_doc_point (PpsView *view,
                                                gint page,
                                                gdouble doc_x,
                                                gdouble doc_y)
{
	PpsRectangle *areas = NULL;
	guint n_areas = 0;
	gint offset = -1;
	gint first_line_offset;
	gint last_line_offset = -1;
	PpsRectangle *rect;
	guint i;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	pps_page_cache_get_text_layout (priv->page_cache, page, &areas, &n_areas);
	if (!areas)
		return -1;

	i = 0;
	while (i < n_areas && offset == -1) {
		rect = areas + i;

		first_line_offset = -1;
		while (doc_y >= rect->y1 && doc_y <= rect->y2) {
			if (first_line_offset == -1) {
				if (doc_x <= rect->x1) {
					/* Location is before the start of the line */
					if (last_line_offset != -1) {
						PpsRectangle *last = areas + last_line_offset;
						gint dx1, dx2;

						/* If there's a previous line, check distances */

						dx1 = doc_x - last->x2;
						dx2 = rect->x1 - doc_x;

						if (dx1 < dx2)
							offset = last_line_offset;
						else
							offset = i;
					} else {
						offset = i;
					}

					last_line_offset = i + 1;
					break;
				}
				first_line_offset = i;
			}
			last_line_offset = i + 1;

			if (doc_x >= rect->x1 && doc_x <= rect->x2) {
				/* Location is inside the line. Position the caret before
				 * or after the character, depending on whether the point
				 * falls within the left or right half of the bounding box.
				 */
				if (doc_x <= rect->x1 + (rect->x2 - rect->x1) / 2)
					offset = i;
				else
					offset = i + 1;
				break;
			}

			i++;
			rect = areas + i;
		}

		if (first_line_offset == -1)
			i++;
	}

	if (last_line_offset == -1)
		return -1;

	if (offset == -1)
		offset = last_line_offset;

	return offset;
}

static gboolean
position_caret_cursor_at_doc_point (PpsView *view,
                                    gint page,
                                    gdouble doc_x,
                                    gdouble doc_y)
{
	gint offset;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	offset = _pps_view_get_caret_cursor_offset_at_doc_point (view, page, doc_x, doc_y);
	if (offset == -1)
		return FALSE;

	if (priv->cursor_offset != offset || priv->cursor_page != page) {
		priv->cursor_offset = offset;
		priv->cursor_page = page;

		return TRUE;
	}

	return FALSE;
}

static gboolean
position_caret_cursor_at_location (PpsView *view,
                                   gdouble x,
                                   gdouble y)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	g_autofree PpsDocumentPoint *doc_point = NULL;

	if (!priv->caret_enabled || pps_document_model_get_rotation (priv->model) != 0)
		return FALSE;

	if (!priv->page_cache)
		return FALSE;

	doc_point = pps_view_get_document_point_for_view_point (view, x, y);
	if (!doc_point)
		return FALSE;

	return position_caret_cursor_at_doc_point (view, doc_point->page_index,
	                                           doc_point->point_on_page.x,
	                                           doc_point->point_on_page.y);
}

static gboolean
position_caret_cursor_for_event (PpsView *view,
                                 gdouble x,
                                 gdouble y,
                                 gboolean redraw)
{
	GdkRectangle area;
	GdkRectangle prev_area = { 0, 0, 0, 0 };
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (redraw)
		get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &prev_area);

	if (!position_caret_cursor_at_location (view, x, y))
		return FALSE;

	if (!get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &area))
		return FALSE;

	priv->cursor_line_offset = area.x;

	g_signal_emit (view, signals[SIGNAL_CURSOR_MOVED], 0, priv->cursor_page, priv->cursor_offset);

	if (redraw) {
		gtk_widget_queue_draw (GTK_WIDGET (view));
	}

	return TRUE;
}

static void
pps_view_button_press_event (GtkGestureClick *self,
                             int n_press,
                             double x,
                             double y,
                             gpointer user_data)
{
	GtkEventController *controller = GTK_EVENT_CONTROLLER (self);
	GtkWidget *widget = gtk_event_controller_get_widget (controller);
	guint button;

	PpsView *view = PPS_VIEW (widget);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	graphene_point_t loc;
	gint first_page, last_page;

	pps_view_link_preview_popover_cleanup (view);

	if (!document || pps_document_get_n_pages (document) <= 0)
		return;

	if (pps_document_model_get_annotation_editing_state (priv->model) & (PPS_ANNOTATION_EDITING_STATE_INK | PPS_ANNOTATION_EDITING_STATE_INSERT_TEXT)) {
		gtk_gesture_set_state (GTK_GESTURE (self), GTK_EVENT_SEQUENCE_DENIED);
		return;
	}

	loc = GRAPHENE_POINT_INIT (x + gtk_adjustment_get_value (priv->hadjustment),
	                           y + gtk_adjustment_get_value (priv->vadjustment));

	if (get_selection_page_range (view, &loc, &loc, &first_page, &last_page) &&
	    first_page == last_page) {
		for (guint i = 0; i < priv->page_widgets->len; i++) {
			PpsViewPage *view_page = g_ptr_array_index (priv->page_widgets, i);

			if (pps_view_page_get_page (view_page) == first_page) {
				gtk_widget_grab_focus (GTK_WIDGET (view_page));
				break;
			}
		}
	} else if (!gtk_widget_has_focus (widget)) {
		gtk_widget_grab_focus (widget);
	}

	button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (self));

	if (gdk_event_triggers_context_menu (gtk_event_controller_get_current_event (controller))) {
		pps_view_do_popup_menu (view, x, y);
		return;
	}

	switch (button) {
	case GDK_BUTTON_PRIMARY: {
		PpsMapping *link;
		PpsMedia *media;
		gint page;

		/* Don't do text selection if clicking on an annotation */
		PpsAnnotation *annot_at_loc = get_annotation_at_location (view, x, y);
		if (!annot_at_loc && PPS_IS_SELECTION (document)) {
			/*
			 * Do not “simplify” by removing SelectionInfo->style.
			 * It is required for the double/triple tap and drag
			 * gesture that is not handled below, but in a separate
			 * Gtk.GestureDrag. (see selection_{begin,update,end}_cb)
			 */
			switch (n_press % 3) {
			case 1:
				priv->selection_info.style = PPS_SELECTION_STYLE_GLYPH;
				break;
			case 2:
				priv->selection_info.style = PPS_SELECTION_STYLE_WORD;
				break;
			case 0:
				priv->selection_info.style = PPS_SELECTION_STYLE_LINE;
				break;
			}
			if (n_press > 1) {
				/* In case of WORD or LINE, compute selections */
				compute_selections (view,
				                    priv->selection_info.style,
				                    loc.x, loc.y,
				                    loc.x, loc.y);
			}
		}

		if ((media = pps_view_get_media_at_location (view, x, y))) {
			pps_view_handle_media (view, media);
		} else if ((link = get_link_mapping_at_location (view, x, y, &page))) {
			_pps_view_set_focused_element (view, link, page);
		} else {
			_pps_view_set_focused_element (view, NULL, -1);

			if (position_caret_cursor_for_event (view, x, y, TRUE)) {
				priv->cursor_blink_time = 0;
				pps_view_pend_cursor_blink (view);
			}
		}
	}
		return;
	case GDK_BUTTON_MIDDLE:
		pps_view_set_focused_element_at_location (view, x, y);
		return;
	}
}

static gboolean
pps_view_scroll_drag_release (PpsView *view)
{
	gdouble dhadj_value, dvadj_value;
	gdouble oldhadjustment, oldvadjustment;
	gdouble h_page_size, v_page_size;
	gdouble h_upper, v_upper;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->drag_info.momentum_x /= 1.2;
	priv->drag_info.momentum_y /= 1.2; /* Alter these constants to change "friction" */

	h_page_size = gtk_adjustment_get_page_size (priv->hadjustment);
	v_page_size = gtk_adjustment_get_page_size (priv->vadjustment);

	dhadj_value = h_page_size *
	              (gdouble) priv->drag_info.momentum_x / gtk_widget_get_width (GTK_WIDGET (view));
	dvadj_value = v_page_size *
	              (gdouble) priv->drag_info.momentum_y / gtk_widget_get_height (GTK_WIDGET (view));

	oldhadjustment = gtk_adjustment_get_value (priv->hadjustment);
	oldvadjustment = gtk_adjustment_get_value (priv->vadjustment);

	h_upper = gtk_adjustment_get_upper (priv->hadjustment);
	v_upper = gtk_adjustment_get_upper (priv->vadjustment);

	/* When we reach the edges, we need either to absorb some momentum and bounce by
	 * multiplying it on -0.5 or stop scrolling by setting momentum to 0. */
	if (((oldhadjustment + dhadj_value) > (h_upper - h_page_size)) ||
	    ((oldhadjustment + dhadj_value) < 0))
		priv->drag_info.momentum_x = 0;
	if (((oldvadjustment + dvadj_value) > (v_upper - v_page_size)) ||
	    ((oldvadjustment + dvadj_value) < 0))
		priv->drag_info.momentum_y = 0;

	gtk_adjustment_set_value (priv->hadjustment,
	                          MIN (oldhadjustment + dhadj_value,
	                               h_upper - h_page_size));
	gtk_adjustment_set_value (priv->vadjustment,
	                          MIN (oldvadjustment + dvadj_value,
	                               v_upper - v_page_size));

	if (((priv->drag_info.momentum_x < 1) && (priv->drag_info.momentum_x > -1)) &&
	    ((priv->drag_info.momentum_y < 1) && (priv->drag_info.momentum_y > -1))) {
		priv->drag_info.release_timeout_id = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
middle_clicked_drag_begin_cb (GtkGestureDrag *self,
                              gdouble start_x,
                              gdouble start_y,
                              PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->drag_info.last_offset_x = 0;
	priv->drag_info.last_offset_y = 0;

	pps_view_set_cursor (view, PPS_VIEW_CURSOR_DRAG);
}

static void
middle_clicked_drag_update_cb (GtkGestureDrag *self,
                               gdouble offset_x,
                               gdouble offset_y,
                               PpsView *view)
{
	gdouble delta_x, delta_y, delta_h_adjustment, delta_v_adjustment;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	gtk_gesture_set_state (GTK_GESTURE (self), GTK_EVENT_SEQUENCE_CLAIMED);

	delta_x = offset_x - priv->drag_info.last_offset_x;
	delta_y = offset_y - priv->drag_info.last_offset_y;

	delta_h_adjustment = gtk_adjustment_get_page_size (priv->hadjustment) *
	                     delta_x / gtk_widget_get_width (GTK_WIDGET (view));
	delta_v_adjustment = gtk_adjustment_get_page_size (priv->vadjustment) *
	                     delta_y / gtk_widget_get_height (GTK_WIDGET (view));

	/* We will update the drag event's start position if
	 * the adjustment value is changed, but only if the
	 * change was not caused by this function. */

	/* clamp scrolling to visible area */
	gtk_adjustment_set_value (priv->hadjustment, MIN (gtk_adjustment_get_value (priv->hadjustment) - delta_h_adjustment,
	                                                  gtk_adjustment_get_upper (priv->hadjustment) -
	                                                      gtk_adjustment_get_page_size (priv->hadjustment)));
	gtk_adjustment_set_value (priv->vadjustment, MIN (gtk_adjustment_get_value (priv->vadjustment) - delta_v_adjustment,
	                                                  gtk_adjustment_get_upper (priv->vadjustment) -
	                                                      gtk_adjustment_get_page_size (priv->vadjustment)));

	priv->drag_info.last_offset_x = offset_x;
	priv->drag_info.last_offset_y = offset_y;
}

static void
middle_clicked_end_swipe_cb (GtkGestureSwipe *gesture,
                             gdouble velocity_x,
                             gdouble velocity_y,
                             PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->drag_info.momentum_x = -velocity_x / 100;
	priv->drag_info.momentum_y = -velocity_y / 100;

	priv->drag_info.release_timeout_id =
	    g_timeout_add (20, (GSourceFunc) pps_view_scroll_drag_release, view);
}

static void
pps_view_remove_all (PpsView *view)
{
	GtkWidget *child;

	/* We start by removing the preview popover since the reference
	   priv->link_preview.popover must be cleared */
	pps_view_link_preview_popover_cleanup (view);

	child = gtk_widget_get_first_child (GTK_WIDGET (view));

	while (child != NULL) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);

		if (g_object_get_data (G_OBJECT (child), "pps-child"))
			gtk_widget_unparent (child);

		child = next;
	}
}

static void
selection_update_idle_cb (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	compute_selections (view,
	                    priv->selection_info.style,
	                    priv->selection_info.start_x, priv->selection_info.start_y,
	                    priv->motion_x, priv->motion_y);
	priv->selection_update_id = 0;
}

static gboolean
selection_scroll_timeout_cb (PpsView *view)
{
	gint x, y, shift_x = 0, shift_y = 0;
	GtkWidget *widget = GTK_WIDGET (view);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	int widget_width = gtk_widget_get_width (widget);
	int widget_height = gtk_widget_get_height (widget);

	if (!pps_document_misc_get_pointer_position (widget, &x, &y))
		return G_SOURCE_CONTINUE;

	if (y + SCROLL_THRESHOLD > widget_height) {
		shift_y = (y + SCROLL_THRESHOLD - widget_height) / 2;
	} else if (y < SCROLL_THRESHOLD) {
		shift_y = (y - SCROLL_THRESHOLD) / 2;
	}

	if (shift_y)
		gtk_adjustment_set_value (priv->vadjustment,
		                          CLAMP (gtk_adjustment_get_value (priv->vadjustment) + shift_y,
		                                 gtk_adjustment_get_lower (priv->vadjustment),
		                                 gtk_adjustment_get_upper (priv->vadjustment) -
		                                     gtk_adjustment_get_page_size (priv->vadjustment)));

	if (x + SCROLL_THRESHOLD > widget_width) {
		shift_x = (x + SCROLL_THRESHOLD - widget_width) / 2;
	} else if (x < SCROLL_THRESHOLD) {
		shift_x = (x - SCROLL_THRESHOLD) / 2;
	}

	if (shift_x)
		gtk_adjustment_set_value (priv->hadjustment,
		                          CLAMP (gtk_adjustment_get_value (priv->hadjustment) + shift_x,
		                                 gtk_adjustment_get_lower (priv->hadjustment),
		                                 gtk_adjustment_get_upper (priv->hadjustment) -
		                                     gtk_adjustment_get_page_size (priv->hadjustment)));

	return G_SOURCE_CONTINUE;
}

static void
selection_update_cb (GtkGestureDrag *selection_gesture,
                     gdouble offset_x,
                     gdouble offset_y,
                     PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GdkEventSequence *sequence = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (selection_gesture));
	gdouble x, y;

	if (!gtk_drag_check_threshold (GTK_WIDGET (view), 0, 0, offset_x, offset_y) && gtk_gesture_get_sequence_state (GTK_GESTURE (selection_gesture), sequence) != GTK_EVENT_SEQUENCE_CLAIMED)
		return;

	if (g_list_length (priv->selection_info.selections) > 0)
		gtk_gesture_set_state (GTK_GESTURE (selection_gesture),
		                       GTK_EVENT_SEQUENCE_CLAIMED);

	/* Schedule timeout to scroll during drag, and scroll once to allow
	   arbitrary speed. */
	if (!priv->selection_scroll_id)
		priv->selection_scroll_id = g_timeout_add (SCROLL_TIME,
		                                           (GSourceFunc) selection_scroll_timeout_cb,
		                                           view);
	else
		selection_scroll_timeout_cb (view);

	gtk_gesture_drag_get_start_point (selection_gesture, &x, &y);

	priv->motion_x = x + offset_x + gtk_adjustment_get_value (priv->hadjustment);
	priv->motion_y = y + offset_y + gtk_adjustment_get_value (priv->vadjustment);

	/* Queue an idle to handle the motion.  We do this because
	 * handling any selection events in the motion could be slower
	 * than new motion events reach us.  We always put it in the
	 * idle to make sure we catch up and don't visibly lag the
	 * mouse. */
	if (!priv->selection_update_id)
		priv->selection_update_id =
		    g_idle_add_once ((GSourceOnceFunc) selection_update_idle_cb,
		                     view);
}

static void
selection_begin_cb (GtkGestureDrag *selection_gesture,
                    gdouble x,
                    gdouble y,
                    PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkEventController *controller = GTK_EVENT_CONTROLLER (selection_gesture);
	GdkModifierType state = gtk_event_controller_get_current_event_state (controller);

	/* Selection in rotated documents has never worked */
	if (!PPS_IS_SELECTION (pps_document_model_get_document (priv->model)) || pps_document_model_get_rotation (priv->model) != 0) {
		gtk_gesture_set_state (GTK_GESTURE (selection_gesture),
		                       GTK_EVENT_SEQUENCE_DENIED);
		return;
	}

	if (state & GDK_SHIFT_MASK) {
		gtk_gesture_set_state (GTK_GESTURE (selection_gesture),
		                       GTK_EVENT_SEQUENCE_CLAIMED);

		selection_update_cb (selection_gesture, 0, 0, view);
	} else {
		priv->selection_info.start_x = x + gtk_adjustment_get_value (priv->hadjustment);
		priv->selection_info.start_y = y + gtk_adjustment_get_value (priv->vadjustment);
	}
}

static void
selection_end_cb (GtkGestureDrag *selection_gesture,
                  gdouble offset_x,
                  gdouble offset_y,
                  PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (priv->selection_info.selections) {
		g_clear_object (&priv->link_selected);
		pps_view_update_primary_selection (view);
	}

	g_clear_handle_id (&priv->selection_scroll_id, g_source_remove);
	g_clear_handle_id (&priv->selection_update_id, g_source_remove);
}

static void
signing_update_cb (GtkGestureDrag *signing_gesture,
                   gdouble offset_x,
                   gdouble offset_y,
                   PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->signing_info.stop_x = priv->signing_info.start_x + offset_x;
	priv->signing_info.stop_y = priv->signing_info.start_y + offset_y;
	gtk_widget_queue_draw (GTK_WIDGET (view));
}

static void
signing_begin_cb (GtkGestureDrag *signing_gesture,
                  gdouble x,
                  gdouble y,
                  PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	gtk_gesture_set_state (GTK_GESTURE (signing_gesture),
	                       GTK_EVENT_SEQUENCE_CLAIMED);

	priv->signing_info.start_x = x;
	priv->signing_info.start_y = y;
	priv->signing_info.stop_x = x;
	priv->signing_info.stop_y = y;
}

static void
signing_end_cb (GtkGestureDrag *selection_gesture,
                gdouble x,
                gdouble y,
                PpsView *view)
{
	pps_view_stop_signature_rect (view);
}

static void
pps_view_motion_notify_event (GtkEventControllerMotion *controller,
                              gdouble x,
                              gdouble y,
                              PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GdkModifierType modifier = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (controller));

	if (!pps_document_model_get_document (priv->model) || (modifier & BUTTON_MODIFIER_MASK) != GDK_NO_MODIFIER_MASK)
		return;

	pps_view_handle_cursor_over_xy (view, x, y);
}

void
pps_view_set_enable_spellchecking (PpsView *view,
                                   gboolean enabled)
{
#ifdef HAVE_LIBSPELLING
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GListModel *model =
	    pps_annotations_context_get_annots_model (priv->annots_context);

	g_return_if_fail (PPS_IS_VIEW (view));

	priv->enable_spellchecking = enabled;

	for (gint i = 0; i < g_list_model_get_n_items (model); i++) {
		g_autoptr (PpsAnnotation) annot = g_list_model_get_item (model, i);
		PpsAnnotationWindow *window;

		if (!PPS_IS_ANNOTATION_MARKUP (annot))
			continue;

		window = g_object_get_data (G_OBJECT (annot), "popup");
		if (window) {
			pps_annotation_window_set_enable_spellchecking (window, priv->enable_spellchecking);
		}
	}
#endif
}

gboolean
pps_view_get_enable_spellchecking (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_return_val_if_fail (PPS_IS_VIEW (view), FALSE);

	return priv->enable_spellchecking;
}

static void
pps_view_button_release_event (GtkGestureClick *self,
                               gint n_press,
                               gdouble x,
                               gdouble y,
                               PpsView *view)
{
	GtkEventController *controller = GTK_EVENT_CONTROLLER (self);
	guint32 time = gtk_event_controller_get_current_event_time (controller);
	PpsLink *link = NULL;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (self));
	GdkModifierType state = gtk_event_controller_get_current_event_state (controller);

	/* Clear selection on primary button clicks (context menu events are handled separately
	 * in button_press and preserve selection for annotation tools) */
	if (button == GDK_BUTTON_PRIMARY && !(state & GDK_SHIFT_MASK) && n_press == 1) {
		clear_selection (view);
	}

	if (pps_document_model_get_document (priv->model) &&
	    (button == GDK_BUTTON_PRIMARY ||
	     button == GDK_BUTTON_MIDDLE)) {
		link = pps_view_get_link_at_location (view, x, y);
	}

	if (button == GDK_BUTTON_PRIMARY) {
		PpsAnnotation *annot = get_annotation_at_location (view, x, y);

		if (annot) {
			/* Free text annotations require double-click to enter edit mode */
			if (PPS_IS_ANNOTATION_FREE_TEXT (annot) && n_press == 2) {
				PpsAnnotationWidgetFactory *factory;
				GtkWidget *widget;

				pps_document_model_set_annotation_editing_state (priv->model,
				                                                 PPS_ANNOTATION_EDITING_STATE_TEXT);

				/* Focus the annotation widget so user can start typing immediately */
				factory = PPS_ANNOTATION_WIDGET_FACTORY (priv->widget_factories[ANNOT_FACTORY]);
				widget = pps_annotation_widget_factory_get_widget_for_annot (factory, annot);
				if (widget) {
					pps_overlay_annotation_grab_focus (PPS_OVERLAY_ANNOTATION (widget), -1, -1);
				}
			} else {
				/* Other annotations (sticky notes, attachments) work on single click */
				pps_view_handle_annotation (view, annot, x, y, time);
			}
		}
	}

	if (priv->selection_info.selections) {
		g_clear_object (&priv->link_selected);
		pps_view_update_primary_selection (view);

		position_caret_cursor_for_event (view, x, y, FALSE);
	} else if (link) {
		if (button == GDK_BUTTON_MIDDLE) {
			PpsLinkAction *action;
			PpsLinkActionType type;

			action = pps_link_get_action (link);
			if (!action)
				return;

			type = pps_link_action_get_action_type (action);
			if (type == PPS_LINK_ACTION_TYPE_GOTO_DEST) {
				g_signal_emit (view,
				               signals[SIGNAL_EXTERNAL_LINK],
				               0, action);
			}
		} else {
			pps_view_handle_link (view, link);
		}
	}
}

static void
context_longpress_gesture_pressed_cb (GtkGestureLongPress *gesture,
                                      gdouble x,
                                      gdouble y,
                                      PpsView *view)
{
	/* pps_view_do_popup_menu handles setting focused element based on selection state,
	 * unifying the experience for touch and pointer input */
	pps_view_do_popup_menu (view, x, y);
}

static gint
go_to_next_page (PpsView *view,
                 gint page)
{
	int n_pages;
	gboolean dual_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!document)
		return -1;

	n_pages = pps_document_get_n_pages (document);

	dual_page = is_dual_page (view, NULL);
	page += dual_page ? 2 : 1;

	if (page < n_pages)
		return page;

	if (dual_page && page == n_pages)
		return page - 1;

	return -1;
}

static gint
go_to_previous_page (PpsView *view,
                     gint page)
{
	gboolean dual_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!pps_document_model_get_document (priv->model))
		return -1;

	dual_page = is_dual_page (view, NULL);
	page -= dual_page ? 2 : 1;

	if (page >= 0)
		return page;

	if (dual_page && page == -1)
		return 0;

	return -1;
}

static gboolean
cursor_go_to_page_start (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	priv->cursor_offset = 0;

	return TRUE;
}

static gboolean
cursor_go_to_page_end (PpsView *view)
{
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);
	if (!log_attrs)
		return FALSE;

	priv->cursor_offset = n_attrs;

	return TRUE;
}

static gboolean
cursor_go_to_next_page (PpsView *view)
{
	gint new_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	new_page = go_to_next_page (view, priv->cursor_page);
	if (new_page != -1) {
		priv->cursor_page = new_page;
		return cursor_go_to_page_start (view);
	}

	return FALSE;
}

static gboolean
cursor_go_to_previous_page (PpsView *view)
{
	gint new_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	new_page = go_to_previous_page (view, priv->cursor_page);
	if (new_page != -1) {
		priv->cursor_page = new_page;
		return cursor_go_to_page_end (view);
	}
	return FALSE;
}

static gboolean
cursor_go_to_document_start (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	priv->cursor_page = 0;
	return cursor_go_to_page_start (view);
}

static gboolean
cursor_go_to_document_end (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!document)
		return FALSE;

	priv->cursor_page = pps_document_get_n_pages (document) - 1;
	return cursor_go_to_page_end (view);
}

static gboolean
cursor_backward_char (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);
	if (!log_attrs)
		return FALSE;

	if (priv->cursor_offset == 0)
		return cursor_go_to_previous_page (view);

	do {
		priv->cursor_offset--;
	} while (priv->cursor_offset >= 0 && !log_attrs[priv->cursor_offset].is_cursor_position);

	return TRUE;
}

static gboolean
cursor_forward_char (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);
	if (!log_attrs)
		return FALSE;

	if (priv->cursor_offset >= n_attrs)
		return cursor_go_to_next_page (view);

	do {
		priv->cursor_offset++;
	} while (priv->cursor_offset <= n_attrs && !log_attrs[priv->cursor_offset].is_cursor_position);

	return TRUE;
}

static gboolean
cursor_backward_word_start (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;
	gint i, j;

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);
	if (!log_attrs)
		return FALSE;

	/* Skip current word starts */
	for (i = priv->cursor_offset; i >= 0 && log_attrs[i].is_word_start; i--)
		;
	if (i <= 0) {
		if (cursor_go_to_previous_page (view))
			return cursor_backward_word_start (view);
		return FALSE;
	}

	/* Move to the beginning of the word */
	for (j = i; j >= 0 && !log_attrs[j].is_word_start; j--)
		;
	priv->cursor_offset = MAX (0, j);

	return TRUE;
}

static gboolean
cursor_forward_word_end (PpsView *view)
{
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;
	gint i, j;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);
	if (!log_attrs)
		return FALSE;

	/* Skip current word ends */
	for (i = priv->cursor_offset; i < n_attrs && log_attrs[i].is_word_end; i++)
		;
	if (i >= n_attrs) {
		if (cursor_go_to_next_page (view))
			return cursor_forward_word_end (view);
		return FALSE;
	}

	/* Move to the end of the word. */
	for (j = i; j < n_attrs && !log_attrs[j].is_word_end; j++)
		;
	priv->cursor_offset = MIN (j, n_attrs);

	return TRUE;
}

static gboolean
cursor_go_to_line_start (PpsView *view)
{
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;
	gint i;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);
	if (!log_attrs)
		return FALSE;

	for (i = priv->cursor_offset; i >= 0 && !log_attrs[i].is_mandatory_break; i--)
		;
	priv->cursor_offset = MAX (0, i);

	return TRUE;
}

static gboolean
cursor_backward_line (PpsView *view)
{
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!cursor_go_to_line_start (view))
		return FALSE;

	if (priv->cursor_offset == 0)
		return cursor_go_to_previous_page (view);

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);

	do {
		priv->cursor_offset--;
	} while (priv->cursor_offset >= 0 && !log_attrs[priv->cursor_offset].is_mandatory_break);
	priv->cursor_offset = MAX (0, priv->cursor_offset);

	return TRUE;
}

static gboolean
cursor_go_to_line_end (PpsView *view)
{
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;
	gint i;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!priv->page_cache)
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);
	if (!log_attrs)
		return FALSE;

	for (i = priv->cursor_offset + 1; i <= n_attrs && !log_attrs[i].is_mandatory_break; i++)
		;
	priv->cursor_offset = MIN (i, n_attrs);

	if (priv->cursor_offset == n_attrs)
		return TRUE;

	do {
		priv->cursor_offset--;
	} while (priv->cursor_offset >= 0 && !log_attrs[priv->cursor_offset].is_cursor_position);

	return TRUE;
}

static gboolean
cursor_forward_line (PpsView *view)
{
	PangoLogAttr *log_attrs = NULL;
	gulong n_attrs;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!cursor_go_to_line_end (view))
		return FALSE;

	pps_page_cache_get_text_log_attrs (priv->page_cache, priv->cursor_page, &log_attrs, &n_attrs);

	if (priv->cursor_offset == n_attrs)
		return cursor_go_to_next_page (view);

	do {
		priv->cursor_offset++;
	} while (priv->cursor_offset <= n_attrs && !log_attrs[priv->cursor_offset].is_cursor_position);

	return TRUE;
}

static gboolean
cursor_clear_selection (PpsView *view,
                        gboolean forward)
{
	GList *l;
	PpsViewSelection *selection;
	cairo_rectangle_int_t rect;
	cairo_region_t *region, *tmp_region = NULL;
	gdouble doc_x, doc_y;
	GdkRectangle area;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	/* When clearing the selection, move the cursor to
	 * the limits of the selection region.
	 */
	if (!priv->selection_info.selections)
		return FALSE;

	l = forward ? g_list_last (priv->selection_info.selections) : priv->selection_info.selections;
	selection = (PpsViewSelection *) l->data;

	region = selection->covered_region;

	/* The selection boundary is not in the current page */
	if (!region || cairo_region_is_empty (region)) {
		PpsRenderContext *rc;
		PpsPage *page;
		gdouble scale = pps_document_model_get_scale (priv->model);
		gint rotation = pps_document_model_get_rotation (priv->model);
		PpsDocument *document = pps_document_model_get_document (priv->model);

		page = pps_document_get_page (document, selection->page);
		rc = pps_render_context_new (page, rotation, scale, PPS_RENDER_ANNOTS_ALL);
		g_object_unref (page);

		tmp_region = pps_selection_get_selection_region (PPS_SELECTION (document),
		                                                 rc,
		                                                 PPS_SELECTION_STYLE_GLYPH,
		                                                 &(selection->rect));
		g_object_unref (rc);

		if (!tmp_region || cairo_region_is_empty (tmp_region)) {
			cairo_region_destroy (tmp_region);
			return FALSE;
		}

		region = tmp_region;
	}

	cairo_region_get_rectangle (region,
	                            forward ? cairo_region_num_rectangles (region) - 1 : 0,
	                            &rect);

	if (tmp_region) {
		cairo_region_destroy (tmp_region);
		region = NULL;
	}

	get_page_point_from_offset (view, selection->page,
	                            forward ? rect.x + rect.width : rect.x,
	                            rect.y + (rect.height / 2), &doc_x, &doc_y);

	position_caret_cursor_at_doc_point (view, selection->page, doc_x, doc_y);

	if (get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &area))
		priv->cursor_line_offset = area.x;

	return TRUE;
}

static gboolean
pps_view_move_cursor (PpsView *view,
                      GtkMovementStep step,
                      gint count,
                      gboolean extend_selections)
{
	GdkRectangle rect;
	GdkRectangle prev_rect;
	gint prev_offset;
	gint prev_page;
	GdkRectangle select_start_rect;
	gint select_start_offset = 0;
	gint select_start_page = 0;
	gboolean changed_page;
	gboolean clear_selections = FALSE;
	const gboolean forward = count >= 0;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	guint scroll_x, scroll_y;

	if (!priv->caret_enabled || pps_document_model_get_rotation (priv->model) != 0)
		return FALSE;

	priv->key_binding_handled = TRUE;
	priv->cursor_blink_time = 0;

	prev_offset = priv->cursor_offset;
	prev_page = priv->cursor_page;

	get_scroll_offset (view, &scroll_x, &scroll_y);

	if (extend_selections) {
		select_start_offset = priv->cursor_offset;
		select_start_page = priv->cursor_page;
	}

	clear_selections = !extend_selections && pps_view_has_selection (view);

	switch (step) {
	case GTK_MOVEMENT_VISUAL_POSITIONS:
		if (!clear_selections || !cursor_clear_selection (view, count > 0)) {
			while (count > 0) {
				cursor_forward_char (view);
				count--;
			}
			while (count < 0) {
				cursor_backward_char (view);
				count++;
			}
		}
		break;
	case GTK_MOVEMENT_WORDS:
		if (!clear_selections || cursor_clear_selection (view, count > 0)) {
			while (count > 0) {
				cursor_forward_word_end (view);
				count--;
			}
			while (count < 0) {
				cursor_backward_word_start (view);
				count++;
			}
		}
		break;
	case GTK_MOVEMENT_DISPLAY_LINES:
		if (!clear_selections || cursor_clear_selection (view, count > 0)) {
			while (count > 0) {
				cursor_forward_line (view);
				count--;
			}
			while (count < 0) {
				cursor_backward_line (view);
				count++;
			}
		}
		break;
	case GTK_MOVEMENT_DISPLAY_LINE_ENDS:
		if (!clear_selections || cursor_clear_selection (view, count > 0)) {
			if (count > 0)
				cursor_go_to_line_end (view);
			else if (count < 0)
				cursor_go_to_line_start (view);
		}
		break;
	case GTK_MOVEMENT_BUFFER_ENDS:
		/* If we are selecting and there is a previous selection,
		   set the new selection's start point to the start point
		   of the previous selection */
		if (extend_selections && pps_view_has_selection (view)) {
			if (cursor_clear_selection (view, FALSE)) {
				select_start_offset = priv->cursor_offset;
				select_start_page = priv->cursor_page;
			}
		}

		if (count > 0)
			cursor_go_to_document_end (view);
		else if (count < 0)
			cursor_go_to_document_start (view);
		break;
	default:
		g_assert_not_reached ();
	}

	pps_view_pend_cursor_blink (view);

	/* Notify the user that it was not possible to move the caret cursor */
	if (!clear_selections &&
	    prev_offset == priv->cursor_offset && prev_page == priv->cursor_page) {
		gtk_widget_error_bell (GTK_WIDGET (view));
		return TRUE;
	}

	/* Scroll to make the caret visible */
	if (!get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &rect))
		return TRUE;

	gtk_widget_grab_focus (GTK_WIDGET (view));

	if (!pps_document_model_get_continuous (priv->model)) {
		changed_page = FALSE;
		if (prev_page < priv->cursor_page) {
			pps_view_next_page (view);
			cursor_go_to_page_start (view);
			changed_page = TRUE;
		} else if (prev_page > priv->cursor_page) {
			pps_view_previous_page (view);
			cursor_go_to_page_end (view);
			_pps_view_ensure_rectangle_is_visible (view, priv->cursor_page, &rect);
			changed_page = TRUE;
		}

		if (changed_page) {
			rect.x += scroll_x;
			rect.y += scroll_y;
			_pps_view_ensure_rectangle_is_visible (view, priv->cursor_page, &rect);
			g_signal_emit (view, signals[SIGNAL_CURSOR_MOVED], 0, priv->cursor_page, priv->cursor_offset);
			clear_selection (view);
			return TRUE;
		}
	}

	if (step == GTK_MOVEMENT_DISPLAY_LINES) {
		const gint prev_cursor_offset = priv->cursor_offset;

		position_caret_cursor_at_location (view,
		                                   MAX (rect.x, priv->cursor_line_offset),
		                                   rect.y + (rect.height / 2));
		/* Make sure we didn't move the cursor in the wrong direction
		 * in case the visual order isn't the same as the logical one,
		 * in order to avoid cursor movement loops */
		if ((forward && prev_cursor_offset > priv->cursor_offset) ||
		    (!forward && prev_cursor_offset < priv->cursor_offset)) {
			priv->cursor_offset = prev_cursor_offset;
		}
		if (!clear_selections &&
		    prev_offset == priv->cursor_offset && prev_page == priv->cursor_page) {
			gtk_widget_error_bell (GTK_WIDGET (view));
			return TRUE;
		}

		if (!get_caret_cursor_area (view, priv->cursor_page, priv->cursor_offset, &rect))
			return TRUE;
	} else {
		priv->cursor_line_offset = rect.x;
	}

	get_caret_cursor_area (view, prev_page, prev_offset, &prev_rect);

	rect.x += scroll_x;
	rect.y += scroll_y;

	_pps_view_ensure_rectangle_is_visible (view, priv->cursor_page, &rect);

	g_signal_emit (view, signals[SIGNAL_CURSOR_MOVED], 0, priv->cursor_page, priv->cursor_offset);

	gtk_widget_queue_draw (GTK_WIDGET (view));

	/* Select text */
	if (extend_selections && PPS_IS_SELECTION (pps_document_model_get_document (priv->model))) {
		gdouble start_x, start_y, end_x, end_y;

		if (!get_caret_cursor_area (view, select_start_page, select_start_offset, &select_start_rect))
			return TRUE;

		start_x = select_start_rect.x + scroll_x;
		start_y = select_start_rect.y + (select_start_rect.height / 2) + scroll_y;

		end_x = rect.x;
		end_y = rect.y + rect.height / 2;

		if (!priv->selection_info.selections) {
			priv->selection_info.start_x = start_x;
			priv->selection_info.start_y = start_y;
		}

		compute_selections (view,
		                    PPS_SELECTION_STYLE_GLYPH,
		                    priv->selection_info.start_x, priv->selection_info.start_y,
		                    end_x, end_y);
	} else if (clear_selections)
		clear_selection (view);

	return TRUE;
}

#if 0
static gboolean
current_event_is_space_key_press (void)
{
	GdkEvent *current_event;
	guint     keyval;
	gboolean  is_space_key_press;

	current_event = gtk_get_current_event ();
	if (!current_event)
		return FALSE;

	is_space_key_press = current_event->type == GDK_KEY_PRESS &&
		gdk_event_get_keyval (current_event, &keyval) &&
		(keyval == GDK_KEY_space || keyval == GDK_KEY_KP_Space);
	gdk_event_free (current_event);

	return is_space_key_press;
}
#endif

static gboolean
pps_view_activate_link (PpsView *view,
                        PpsLink *link)
{
#if 0
	/* Most of the GtkWidgets emit activate on both Space and Return key press,
	 * but we don't want to activate links on Space for consistency with the Web.
	 */
	if (current_event_is_space_key_press ())
		return FALSE;
#endif
	pps_view_handle_link (view, link);

	return TRUE;
}

static void
pps_view_activate (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!priv->focused_element)
		return;

	if (PPS_IS_DOCUMENT_LINKS (document) &&
	    PPS_IS_LINK (priv->focused_element->data)) {
		priv->key_binding_handled = pps_view_activate_link (view, PPS_LINK (priv->focused_element->data));
		return;
	}
}

static void
pps_view_focus_in (GtkEventControllerFocus *self,
                   gpointer user_data)
{
	PpsView *view = PPS_VIEW (user_data);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (priv->pixbuf_cache)
		pps_pixbuf_cache_style_changed (priv->pixbuf_cache);

	pps_view_check_cursor_blink (view);
	gtk_widget_queue_draw (GTK_WIDGET (view));
}

static void
pps_view_focus_out (GtkEventControllerFocus *self,
                    gpointer user_data)
{
	PpsView *view = PPS_VIEW (user_data);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (priv->pixbuf_cache)
		pps_pixbuf_cache_style_changed (priv->pixbuf_cache);

	pps_view_check_cursor_blink (view);
	gtk_widget_queue_draw (GTK_WIDGET (view));
}

/*** Drawing ***/

static void
accent_changed_cb (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (priv->pixbuf_cache)
		pps_pixbuf_cache_style_changed (priv->pixbuf_cache);

	for (guint i = 0; i < priv->page_widgets->len; i++) {
		PpsViewPage *view_page = g_ptr_array_index (priv->page_widgets, i);
		gtk_widget_queue_draw (GTK_WIDGET (view_page));
	}
}

static void
pps_view_inverted_changed_cb (PpsDocumentModel *model,
                              GParamSpec *pspec,
                              PpsView *view)
{
	if (pps_document_model_get_inverted_colors (model)) {
		gtk_widget_add_css_class (GTK_WIDGET (view), PPS_STYLE_CLASS_INVERTED);
	} else {
		gtk_widget_remove_css_class (GTK_WIDGET (view), PPS_STYLE_CLASS_INVERTED);
	}
}

#ifdef HAVE_TRANSPARENT_SELECTION
static void
state_flags_changed_cb (PpsView *view,
                        GtkStateFlags flags)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkStateFlags new_flags = gtk_widget_get_state_flags (GTK_WIDGET (view));

	if (((new_flags ^ flags) & GTK_STATE_FLAG_FOCUS_WITHIN) == 0)
		return;

	if (priv->pixbuf_cache)
		pps_pixbuf_cache_style_changed (priv->pixbuf_cache);

	for (guint i = 0; i < priv->page_widgets->len; i++) {
		PpsViewPage *view_page = g_ptr_array_index (priv->page_widgets, i);
		gtk_widget_queue_draw (GTK_WIDGET (view_page));
	}
}
#endif

/*
 * TODO: This was copied over to PpsViewPage and can be removed here after that
 * gained support for handling pointer hovering, including the link preview
 * popover which still calls this here in PpsView.
 */
static void
draw_surface (GtkSnapshot *snapshot,
              GdkTexture *texture,
              const graphene_point_t *point,
              const graphene_rect_t *area,
              gboolean inverted)
{
	gboolean snap_texture = gdk_texture_get_height (texture) == floor (area->size.height);

	gtk_snapshot_save (snapshot);
	gtk_snapshot_translate (snapshot, point);

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

/*** GObject functions ***/

static void
pps_view_finalize (GObject *object)
{
	PpsView *view = PPS_VIEW (object);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_clear_list (&priv->selection_info.selections,
	              (GDestroyNotify) pps_view_selection_free);

	g_clear_object (&priv->link_selected);

	g_clear_object (&priv->dnd_image);

	G_OBJECT_CLASS (pps_view_parent_class)->finalize (object);
}

static void
pps_view_dispose (GObject *object)
{
	PpsView *view = PPS_VIEW (object);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	pps_view_remove_all (view);

	if (priv->model) {
		g_signal_handlers_disconnect_by_data (priv->model, view);
		g_clear_object (&priv->model);
	}

	for (int i = 0; i < WIDGET_FACTORY_COUNT; i++) {
		g_clear_object (&priv->widget_factories[i]);
	}

	g_clear_object (&priv->pixbuf_cache);
	g_clear_object (&priv->page_cache);
	g_clear_object (&priv->scroll_animation_vertical);
	g_clear_object (&priv->scroll_animation_horizontal);

	g_clear_pointer (&priv->page_widgets, g_ptr_array_unref);
	g_clear_pointer (&priv->focused_element, pps_mapping_free);

	g_clear_handle_id (&priv->update_cursor_idle_id, g_source_remove);
	g_clear_handle_id (&priv->selection_scroll_id, g_source_remove);
	g_clear_handle_id (&priv->selection_update_id, g_source_remove);
	g_clear_handle_id (&priv->drag_info.release_timeout_id, g_source_remove);
	g_clear_handle_id (&priv->cursor_blink_timeout_id, g_source_remove);
	g_clear_handle_id (&priv->child_focus_idle_id, g_source_remove);

	pps_view_link_preview_popover_cleanup (view);

	gtk_scrollable_set_hadjustment (GTK_SCROLLABLE (view), NULL);
	gtk_scrollable_set_vadjustment (GTK_SCROLLABLE (view), NULL);

	G_OBJECT_CLASS (pps_view_parent_class)->dispose (object);
}

static void
pps_view_get_property (GObject *object,
                       guint prop_id,
                       GValue *value,
                       GParamSpec *pspec)
{
	PpsView *view = PPS_VIEW (object);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	switch (prop_id) {
	case PROP_CAN_ZOOM_IN:
		g_value_set_boolean (value, priv->can_zoom_in);
		break;
	case PROP_CAN_ZOOM_OUT:
		g_value_set_boolean (value, priv->can_zoom_out);
		break;
	case PROP_ANNOTATIONS_CONTEXT:
		g_value_set_object (value, priv->annots_context);
		break;
	case PROP_MODEL:
		g_value_set_object (value, priv->model);
		break;
	case PROP_SEARCH_CONTEXT:
		g_value_set_object (value, priv->search_context);
		break;
	case PROP_HADJUSTMENT:
		g_value_set_object (value, priv->hadjustment);
		break;
	case PROP_VADJUSTMENT:
		g_value_set_object (value, priv->vadjustment);
		break;
	case PROP_HSCROLL_POLICY:
		g_value_set_enum (value, priv->hscroll_policy);
		break;
	case PROP_VSCROLL_POLICY:
		g_value_set_enum (value, priv->vscroll_policy);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pps_view_set_property (GObject *object,
                       guint prop_id,
                       const GValue *value,
                       GParamSpec *pspec)
{
	PpsView *view = PPS_VIEW (object);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	switch (prop_id) {
	case PROP_ANNOTATIONS_CONTEXT:
		pps_view_set_annotations_context (view, (PpsAnnotationsContext *) g_value_get_object (value));
		break;
	case PROP_MODEL:
		pps_view_set_model (view, (PpsDocumentModel *) g_value_get_object (value));
		break;
	case PROP_SEARCH_CONTEXT:
		pps_view_set_search_context (view, (PpsSearchContext *) g_value_get_object (value));
		break;
	case PROP_HADJUSTMENT:
		pps_view_set_scroll_adjustment (view, GTK_ORIENTATION_HORIZONTAL,
		                                (GtkAdjustment *) g_value_get_object (value));
		break;
	case PROP_VADJUSTMENT:
		pps_view_set_scroll_adjustment (view, GTK_ORIENTATION_VERTICAL,
		                                (GtkAdjustment *) g_value_get_object (value));
		break;
	case PROP_HSCROLL_POLICY:
		priv->hscroll_policy = g_value_get_enum (value);
		gtk_widget_queue_resize (GTK_WIDGET (view));
		break;
	case PROP_VSCROLL_POLICY:
		priv->vscroll_policy = g_value_get_enum (value);
		gtk_widget_queue_resize (GTK_WIDGET (view));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* This is hardcoded into cairo, see https://github.com/ImageMagick/cairo/blob/2fd08a0e6a452f72d1b7780ff9f88632e3bd64fe/src/cairo-image-surface.c#L62C2-L62C8 */
#define MAX_IMAGE_SIZE 32767

static void
view_update_scale_limits (PpsView *view)
{
	gdouble max_width, max_height;
	gdouble max_scale;
	gdouble dpi;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	if (!document)
		return;

	dpi = pps_document_misc_get_widget_dpi (GTK_WIDGET (view)) / 72.0;

	pps_document_get_max_page_size (document, &max_width, &max_height);
	max_scale = sqrt (priv->pixbuf_cache_size / (max_width * 4 * max_height));
	max_scale = MIN (max_scale, MAX_IMAGE_SIZE / max_height);
	max_scale = MIN (max_scale, MAX_IMAGE_SIZE / max_width);

	pps_document_model_set_min_scale (priv->model, MIN_SCALE * dpi);
	pps_document_model_set_max_scale (priv->model, max_scale);
}

static void
page_swipe_cb (GtkGestureSwipe *gesture,
               gdouble velocity_x,
               gdouble velocity_y,
               PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (pps_document_model_get_continuous (priv->model))
		return;

	GtkTextDirection direction = gtk_widget_get_direction (GTK_WIDGET (view)) || gtk_widget_get_default_direction ();
	gdouble angle = atan2 (velocity_x, velocity_y);
	gdouble speed = sqrt (pow (velocity_x, 2) + pow (velocity_y, 2));

	if (speed > 30) {
		gtk_gesture_set_state (GTK_GESTURE (gesture),
		                       GTK_EVENT_SEQUENCE_CLAIMED);
		if ((G_PI_4 < angle && angle <= 3 * G_PI_4) ||
		    (direction == GTK_TEXT_DIR_LTR && (-G_PI_4 < angle && angle <= G_PI_4)) ||
		    (direction == GTK_TEXT_DIR_RTL && !(-3 * G_PI_4 < angle && angle <= 3 * G_PI_4)))

			pps_view_previous_page (view);
		else
			pps_view_next_page (view);
	}
}

static void
add_move_binding_keypad (GtkWidgetClass *widget_class,
                         guint keyval,
                         GdkModifierType modifiers,
                         GtkMovementStep step,
                         gint count)
{
	guint keypad_keyval = keyval - GDK_KEY_Left + GDK_KEY_KP_Left;

	gtk_widget_class_add_binding_signal (widget_class, keyval, modifiers,
	                                     "move-cursor", "(iib)",
	                                     step, count, FALSE);

	gtk_widget_class_add_binding_signal (widget_class, keypad_keyval, modifiers,
	                                     "move-cursor", "(iib)",
	                                     step, count, FALSE);

	/* Selection-extending version */
	gtk_widget_class_add_binding_signal (widget_class, keyval, modifiers | GDK_SHIFT_MASK,
	                                     "move-cursor", "(iib)",
	                                     step, count, TRUE);
	gtk_widget_class_add_binding_signal (widget_class, keypad_keyval, modifiers | GDK_SHIFT_MASK,
	                                     "move-cursor", "(iib)",
	                                     step, count, TRUE);
}

static void
pps_view_set_focus_child (GtkWidget *widget, GtkWidget *focus_child)
{
	PpsView *view = PPS_VIEW (widget);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	guint new_cursor_page;

	if (PPS_IS_VIEW_PAGE (focus_child)) {
		new_cursor_page = pps_view_page_get_page (PPS_VIEW_PAGE (focus_child));

		if (priv->cursor_page != new_cursor_page) {
			priv->cursor_page = new_cursor_page;
			cursor_go_to_page_start (view);
		}
	}

	GTK_WIDGET_CLASS (pps_view_parent_class)->set_focus_child (widget, focus_child);
}

static gboolean
pps_view_focus (GtkWidget *widget,
                GtkDirectionType direction)
{
	PpsView *view = PPS_VIEW (widget);
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GtkWidget *focus_child = gtk_widget_get_focus_child (widget);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	gboolean has_focus;

	if (document == NULL)
		return FALSE;

	if (direction != GTK_DIR_TAB_FORWARD && direction != GTK_DIR_TAB_BACKWARD)
		return FALSE;

	if (focus_child != NULL && gtk_widget_child_focus (focus_child, direction)) {
		GtkWidget *overlay = gtk_widget_get_focus_child (focus_child);
		if (PPS_IS_VIEW_PAGE (focus_child) && PPS_IS_OVERLAY (overlay)) {
			GdkRectangle doc_rect;
			gdouble padding;
			g_autofree PpsRectangle *view_rect = pps_overlay_get_area (PPS_OVERLAY (overlay), &padding);
			gint page = pps_view_page_get_page (PPS_VIEW_PAGE (focus_child));

			_pps_view_transform_doc_rect_to_view_rect (view, page, view_rect, &doc_rect);
			_pps_view_ensure_rectangle_is_visible (view, page, &doc_rect);
		}
		return TRUE;
	}

	has_focus = gtk_widget_is_focus (widget) || focus_child != NULL;

	switch (direction) {
	case GTK_DIR_TAB_FORWARD:
		/* if view has focused page that didn't keep it, tabbing out of that one */
		if (has_focus) {
			if (!cursor_go_to_next_page (view))
				return FALSE;
		}
		break;
	case GTK_DIR_TAB_BACKWARD:
		/* if view has focused page that didn't keep it, tabbing out of that one */
		if (has_focus) {
			if (!cursor_go_to_previous_page (view))
				return FALSE;

			cursor_go_to_page_start (view);
		}
		break;
	default:
		g_assert_not_reached ();
	}

	pps_view_scroll_to_page (view, priv->cursor_page);
	return gtk_widget_grab_focus (widget);
}

static gboolean
pps_view_grab_focus (GtkWidget *widget)
{
	PpsView *view = PPS_VIEW (widget);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	for (guint i = 0; i < priv->page_widgets->len; i++) {
		PpsViewPage *view_page = g_ptr_array_index (priv->page_widgets, i);

		if (pps_view_page_get_page (view_page) == priv->cursor_page) {
			if ((gtk_widget_get_state_flags (GTK_WIDGET (view_page)) & GTK_STATE_FLAG_FOCUS_WITHIN) == 0)
				gtk_widget_grab_focus (GTK_WIDGET (view_page));
			return TRUE;
		}
	}

	return GTK_WIDGET_CLASS (pps_view_parent_class)->grab_focus (widget);
}

static void
notify_scale_factor_cb (PpsView *view,
                        GParamSpec *pspec)
{
	gtk_widget_queue_allocate (GTK_WIDGET (view));
}

static void
zoom_gesture_begin_cb (GtkGesture *gesture,
                       GdkEventSequence *sequence,
                       PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->prev_zoom_gesture_scale = 1;
}

static void
zoom_gesture_scale_changed_cb (GtkGestureZoom *gesture,
                               gdouble scale,
                               PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble factor;

	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

	factor = scale - priv->prev_zoom_gesture_scale + 1;
	priv->prev_zoom_gesture_scale = scale;
	pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FREE);

	gtk_gesture_get_bounding_box_center (GTK_GESTURE (gesture), &priv->zoom_center_x, &priv->zoom_center_y);

	if ((factor < 1.0 && pps_view_can_zoom_out (view)) ||
	    (factor >= 1.0 && pps_view_can_zoom_in (view)))
		pps_view_zoom (view, factor);
}

static void
pps_view_class_init (PpsViewClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

	object_class->get_property = pps_view_get_property;
	object_class->set_property = pps_view_set_property;
	object_class->dispose = pps_view_dispose;
	object_class->finalize = pps_view_finalize;

	widget_class->snapshot = pps_view_snapshot;
	widget_class->measure = pps_view_measure;
	widget_class->size_allocate = pps_view_size_allocate;
	widget_class->query_tooltip = pps_view_query_tooltip;
	widget_class->set_focus_child = pps_view_set_focus_child;
	widget_class->focus = pps_view_focus;
	widget_class->grab_focus = pps_view_grab_focus;

	gtk_widget_class_set_css_name (widget_class, "pps-view");

	class->scroll = pps_view_scroll;
	class->move_cursor = pps_view_move_cursor;
	class->activate = pps_view_activate;

	gtk_widget_class_set_template_from_resource (widget_class,
	                                             "/org/gnome/papers/ui/view.ui");

	gtk_widget_class_bind_template_child_private (widget_class, PpsView,
	                                              middle_clicked_drag_gesture);
	gtk_widget_class_bind_template_child_private (widget_class, PpsView,
	                                              middle_clicked_drag_swipe_gesture);
	gtk_widget_class_bind_template_child_private (widget_class, PpsView,
	                                              signing_drag_gesture);

	gtk_widget_class_bind_template_callback (widget_class, pps_view_button_press_event);
	gtk_widget_class_bind_template_callback (widget_class, pps_view_button_release_event);
	gtk_widget_class_bind_template_callback (widget_class, pps_view_motion_notify_event);
	gtk_widget_class_bind_template_callback (widget_class, zoom_gesture_begin_cb);
	gtk_widget_class_bind_template_callback (widget_class, zoom_gesture_scale_changed_cb);
	gtk_widget_class_bind_template_callback (widget_class, notify_scale_factor_cb);
	gtk_widget_class_bind_template_callback (widget_class, pps_view_focus_in);
	gtk_widget_class_bind_template_callback (widget_class, pps_view_focus_out);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         selection_begin_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         selection_end_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         selection_update_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         signing_begin_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         signing_end_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         signing_update_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         middle_clicked_drag_begin_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         middle_clicked_drag_update_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         middle_clicked_end_swipe_cb);
	gtk_widget_class_bind_template_callback (widget_class, scroll_to_zoom_cb);
	gtk_widget_class_bind_template_callback (widget_class, page_swipe_cb);
	gtk_widget_class_bind_template_callback (widget_class,
	                                         context_longpress_gesture_pressed_cb);

	/**
	 * PpsView:can-zoom-in:
	 */
	g_object_class_install_property (object_class,
	                                 PROP_CAN_ZOOM_IN,
	                                 g_param_spec_boolean ("can-zoom-in",
	                                                       "Can Zoom In",
	                                                       "Whether the view can be zoomed in further",
	                                                       TRUE,
	                                                       G_PARAM_READABLE |
	                                                           G_PARAM_STATIC_STRINGS));
	/**
	 * PpsView:can-zoom-out:
	 */
	g_object_class_install_property (object_class,
	                                 PROP_CAN_ZOOM_OUT,
	                                 g_param_spec_boolean ("can-zoom-out",
	                                                       "Can Zoom Out",
	                                                       "Whether the view can be zoomed out further",
	                                                       TRUE,
	                                                       G_PARAM_READABLE |
	                                                           G_PARAM_STATIC_STRINGS));

	/**
	 * PpsView:annotations-context:
	 */
	g_object_class_install_property (object_class,
	                                 PROP_ANNOTATIONS_CONTEXT,
	                                 g_param_spec_object ("annotations-context",
	                                                      "Annotations Context",
	                                                      "The view's current annotation context",
	                                                      PPS_TYPE_ANNOTATIONS_CONTEXT,
	                                                      G_PARAM_READWRITE |
	                                                          G_PARAM_STATIC_STRINGS));

	/**
	 * PpsView:model:
	 */
	g_object_class_install_property (object_class,
	                                 PROP_MODEL,
	                                 g_param_spec_object ("model",
	                                                      "View model",
	                                                      "The view's current model",
	                                                      PPS_TYPE_DOCUMENT_MODEL,
	                                                      G_PARAM_READWRITE |
	                                                          G_PARAM_STATIC_STRINGS));

	/**
	 * PpsView:search-context:
	 */
	g_object_class_install_property (object_class,
	                                 PROP_SEARCH_CONTEXT,
	                                 g_param_spec_object ("search-context",
	                                                      "View search context",
	                                                      "The view's current search context",
	                                                      PPS_TYPE_SEARCH_CONTEXT,
	                                                      G_PARAM_READWRITE |
	                                                          G_PARAM_STATIC_STRINGS));

	/* Scrollable interface */
	g_object_class_override_property (object_class, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property (object_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (object_class, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property (object_class, PROP_VSCROLL_POLICY, "vscroll-policy");

	signals[SIGNAL_SCROLL] = g_signal_new ("scroll",
	                                       G_TYPE_FROM_CLASS (object_class),
	                                       G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                                       G_STRUCT_OFFSET (PpsViewClass, scroll),
	                                       NULL, NULL,
	                                       pps_view_marshal_BOOLEAN__ENUM_ENUM,
	                                       G_TYPE_BOOLEAN, 2,
	                                       GTK_TYPE_SCROLL_TYPE,
	                                       GTK_TYPE_ORIENTATION);
	signals[SIGNAL_HANDLE_LINK] = g_signal_new ("handle-link",
	                                            G_TYPE_FROM_CLASS (object_class),
	                                            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                                            G_STRUCT_OFFSET (PpsViewClass, handle_link),
	                                            NULL, NULL,
	                                            NULL,
	                                            G_TYPE_NONE, 2,
	                                            G_TYPE_OBJECT, G_TYPE_OBJECT);
	signals[SIGNAL_EXTERNAL_LINK] = g_signal_new ("external-link",
	                                              G_TYPE_FROM_CLASS (object_class),
	                                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                                              G_STRUCT_OFFSET (PpsViewClass, external_link),
	                                              NULL, NULL,
	                                              g_cclosure_marshal_VOID__OBJECT,
	                                              G_TYPE_NONE, 1,
	                                              PPS_TYPE_LINK_ACTION);
	signals[SIGNAL_POPUP_MENU] = g_signal_new ("popup",
	                                           G_TYPE_FROM_CLASS (object_class),
	                                           G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                                           G_STRUCT_OFFSET (PpsViewClass, popup_menu),
	                                           NULL, NULL,
	                                           pps_view_marshal_VOID__POINTER_DOUBLE_DOUBLE,
	                                           G_TYPE_NONE, 3,
	                                           G_TYPE_POINTER,
	                                           G_TYPE_DOUBLE,
	                                           G_TYPE_DOUBLE);
	signals[SIGNAL_SELECTION_CHANGED] = g_signal_new ("selection-changed",
	                                                  G_TYPE_FROM_CLASS (object_class),
	                                                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                                                  G_STRUCT_OFFSET (PpsViewClass, selection_changed),
	                                                  NULL, NULL,
	                                                  g_cclosure_marshal_VOID__VOID,
	                                                  G_TYPE_NONE, 0,
	                                                  G_TYPE_NONE);
	signals[SIGNAL_LAYERS_CHANGED] = g_signal_new ("layers-changed",
	                                               G_TYPE_FROM_CLASS (object_class),
	                                               G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                                               G_STRUCT_OFFSET (PpsViewClass, layers_changed),
	                                               NULL, NULL,
	                                               g_cclosure_marshal_VOID__VOID,
	                                               G_TYPE_NONE, 0,
	                                               G_TYPE_NONE);
	signals[SIGNAL_MOVE_CURSOR] = g_signal_new ("move-cursor",
	                                            G_TYPE_FROM_CLASS (object_class),
	                                            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                                            G_STRUCT_OFFSET (PpsViewClass, move_cursor),
	                                            NULL, NULL,
	                                            pps_view_marshal_BOOLEAN__ENUM_INT_BOOLEAN,
	                                            G_TYPE_BOOLEAN, 3,
	                                            GTK_TYPE_MOVEMENT_STEP,
	                                            G_TYPE_INT,
	                                            G_TYPE_BOOLEAN);
	signals[SIGNAL_CURSOR_MOVED] = g_signal_new ("cursor-moved",
	                                             G_TYPE_FROM_CLASS (object_class),
	                                             G_SIGNAL_RUN_LAST,
	                                             0,
	                                             NULL, NULL,
	                                             pps_view_marshal_VOID__INT_INT,
	                                             G_TYPE_NONE, 2,
	                                             G_TYPE_INT,
	                                             G_TYPE_INT);
	signals[SIGNAL_ACTIVATE] = g_signal_new ("activate",
	                                         G_OBJECT_CLASS_TYPE (object_class),
	                                         G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
	                                         G_STRUCT_OFFSET (PpsViewClass, activate),
	                                         NULL, NULL,
	                                         g_cclosure_marshal_VOID__VOID,
	                                         G_TYPE_NONE, 0,
	                                         G_TYPE_NONE);
	signals[SIGNAL_SIGNATURE_RECT] = g_signal_new ("signature-rect",
	                                               G_TYPE_FROM_CLASS (object_class),
	                                               G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
	                                               G_STRUCT_OFFSET (PpsViewClass, signature_rect),
	                                               NULL, NULL,
	                                               g_cclosure_marshal_VOID__UINT_POINTER,
	                                               G_TYPE_NONE, 2,
	                                               G_TYPE_UINT,
	                                               PPS_TYPE_RECTANGLE);

	gtk_widget_class_set_activate_signal (widget_class, signals[SIGNAL_ACTIVATE]);

	add_move_binding_keypad (widget_class, GDK_KEY_Left, 0, GTK_MOVEMENT_VISUAL_POSITIONS, -1);
	add_move_binding_keypad (widget_class, GDK_KEY_Right, 0, GTK_MOVEMENT_VISUAL_POSITIONS, 1);
	add_move_binding_keypad (widget_class, GDK_KEY_Left, GDK_CONTROL_MASK, GTK_MOVEMENT_WORDS, -1);
	add_move_binding_keypad (widget_class, GDK_KEY_Right, GDK_CONTROL_MASK, GTK_MOVEMENT_WORDS, 1);
	add_move_binding_keypad (widget_class, GDK_KEY_Up, 0, GTK_MOVEMENT_DISPLAY_LINES, -1);
	add_move_binding_keypad (widget_class, GDK_KEY_Down, 0, GTK_MOVEMENT_DISPLAY_LINES, 1);
	add_move_binding_keypad (widget_class, GDK_KEY_Home, 0, GTK_MOVEMENT_DISPLAY_LINE_ENDS, -1);
	add_move_binding_keypad (widget_class, GDK_KEY_End, 0, GTK_MOVEMENT_DISPLAY_LINE_ENDS, 1);
	add_move_binding_keypad (widget_class, GDK_KEY_Home, GDK_CONTROL_MASK, GTK_MOVEMENT_BUFFER_ENDS, -1);
	add_move_binding_keypad (widget_class, GDK_KEY_End, GDK_CONTROL_MASK, GTK_MOVEMENT_BUFFER_ENDS, 1);

	add_scroll_binding_keypad (widget_class, GDK_KEY_Left, 0, GTK_SCROLL_STEP_BACKWARD, GTK_ORIENTATION_HORIZONTAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Right, 0, GTK_SCROLL_STEP_FORWARD, GTK_ORIENTATION_HORIZONTAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Left, GDK_ALT_MASK, GTK_SCROLL_STEP_DOWN, GTK_ORIENTATION_HORIZONTAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Right, GDK_ALT_MASK, GTK_SCROLL_STEP_UP, GTK_ORIENTATION_HORIZONTAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Up, 0, GTK_SCROLL_STEP_BACKWARD, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Down, 0, GTK_SCROLL_STEP_FORWARD, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Up, GDK_ALT_MASK, GTK_SCROLL_STEP_DOWN, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Down, GDK_ALT_MASK, GTK_SCROLL_STEP_UP, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Page_Up, 0, GTK_SCROLL_PAGE_BACKWARD, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Page_Down, 0, GTK_SCROLL_PAGE_FORWARD, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_Home, GDK_CONTROL_MASK, GTK_SCROLL_START, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_End, GDK_CONTROL_MASK, GTK_SCROLL_END, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_BackSpace, 0, GTK_SCROLL_PAGE_BACKWARD, GTK_ORIENTATION_VERTICAL);
	add_scroll_binding_keypad (widget_class, GDK_KEY_space, 0, GTK_SCROLL_PAGE_FORWARD, GTK_ORIENTATION_VERTICAL);

	/* We can't use the bindings defined in GtkWindow for Space and Return,
	 * because we also have those bindings for scrolling.
	 */
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_space, 0,
	                                     "activate", NULL);
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_Space, 0,
	                                     "activate", NULL);
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Return, 0,
	                                     "activate", NULL);
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_ISO_Enter, 0,
	                                     "activate", NULL);
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_KP_Enter, 0,
	                                     "activate", NULL);

	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Return, 0, "scroll",
	                                     "(ii)", GTK_SCROLL_PAGE_FORWARD, GTK_ORIENTATION_VERTICAL);
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Return, GDK_SHIFT_MASK, "scroll",
	                                     "(ii)", GTK_SCROLL_PAGE_BACKWARD, GTK_ORIENTATION_VERTICAL);
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_space, 0, "scroll",
	                                     "(ii)", GTK_SCROLL_PAGE_FORWARD, GTK_ORIENTATION_VERTICAL);
	gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_space, GDK_SHIFT_MASK, "scroll",
	                                     "(ii)", GTK_SCROLL_PAGE_BACKWARD, GTK_ORIENTATION_VERTICAL);
	gtk_widget_class_add_binding (widget_class,
	                              GDK_KEY_a, GDK_CONTROL_MASK,
	                              (GtkShortcutFunc) pps_view_select_all,
	                              NULL);

	gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_DOCUMENT);
}

static void
pps_view_init (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	gtk_widget_init_template (GTK_WIDGET (view));

	priv->start_page = -1;
	priv->end_page = -1;
	priv->spacing = 12;
	priv->current_page = -1;
	priv->cursor = PPS_VIEW_CURSOR_NORMAL;
	priv->selection_info.selections = NULL;
	priv->pending_scroll = SCROLL_TO_KEEP_POSITION;
	priv->pixbuf_cache_size = DEFAULT_PIXBUF_CACHE_SIZE;
	priv->caret_enabled = FALSE;
	priv->cursor_page = 0;
	priv->allow_links_change_zoom = FALSE;
	priv->zoom_center_x = -1;
	priv->zoom_center_y = -1;
	priv->scroll_animation_vertical = adw_timed_animation_new (GTK_WIDGET (view), 0, 0, 200, adw_callback_animation_target_new ((AdwAnimationTargetFunc) pps_scroll_vertical_animation_cb, view, NULL));
	priv->scroll_animation_horizontal = adw_timed_animation_new (GTK_WIDGET (view), 0, 0, 200, adw_callback_animation_target_new ((AdwAnimationTargetFunc) pps_scroll_horizontal_animation_cb, view, NULL));

	adw_animation_pause (priv->scroll_animation_vertical);
	adw_animation_pause (priv->scroll_animation_horizontal);

	priv->widget_factories[FORM_FACTORY] = pps_form_widget_factory_new ();
	priv->widget_factories[ANNOT_FACTORY] = pps_annotation_widget_factory_new ();

	priv->page_widgets = g_ptr_array_new_full (PAGE_WIDGET_POOL_SIZE, (GDestroyNotify) gtk_widget_unparent);

	for (guint i = 0; i < PAGE_WIDGET_POOL_SIZE; i++) {
		PpsViewPage *page = pps_view_page_new ();

		gtk_widget_set_parent (GTK_WIDGET (page), GTK_WIDGET (view));
		g_ptr_array_add (priv->page_widgets, page);
	}

	gtk_gesture_group (priv->middle_clicked_drag_gesture,
	                   priv->middle_clicked_drag_swipe_gesture);

	g_signal_connect_object (adw_style_manager_get_default (),
	                         "notify::accent-color",
	                         G_CALLBACK (accent_changed_cb),
	                         view,
	                         G_CONNECT_SWAPPED);

#ifdef HAVE_TRANSPARENT_SELECTION
	g_signal_connect_object (view,
	                         "state-flags-changed",
	                         G_CALLBACK (state_flags_changed_cb),
	                         view,
	                         0);
#endif
}

/*** Callbacks ***/

static void
pps_view_scroll_to_page (PpsView *view, gint page)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->current_page = page;
	pps_view_queue_rescroll_to_current_page (view);
}

static void
pps_view_page_changed_cb (PpsDocumentModel *model,
                          gint old_page,
                          gint new_page,
                          PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	if (!pps_document_model_get_document (priv->model))
		return;

	if (priv->current_page != new_page) {
		pps_view_scroll_to_page (view, new_page);
	}
}

PpsView *
pps_view_new (void)
{
	return g_object_new (PPS_TYPE_VIEW, NULL);
}

static void
setup_caches (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	priv->height_to_page_cache = pps_view_get_height_to_page_cache (view);
	priv->pixbuf_cache = pps_pixbuf_cache_new (GTK_WIDGET (view), priv->model, priv->pixbuf_cache_size);
	priv->page_cache = pps_page_cache_new (pps_document_model_get_document (priv->model));

	pps_page_cache_set_flags (priv->page_cache,
	                          pps_page_cache_get_flags (priv->page_cache) |
	                              PPS_PAGE_DATA_INCLUDE_TEXT_LAYOUT |
	                              PPS_PAGE_DATA_INCLUDE_TEXT |
	                              PPS_PAGE_DATA_INCLUDE_TEXT_ATTRS |
	                              PPS_PAGE_DATA_INCLUDE_TEXT_LOG_ATTRS);

	for (int i = 0; i < WIDGET_FACTORY_COUNT; i++) {
		pps_element_widget_factory_setup (priv->widget_factories[i],
		                                  priv->model,
		                                  priv->annots_context,
		                                  priv->pixbuf_cache,
		                                  priv->page_widgets,
		                                  priv->page_cache);
	}
}

static void
clear_caches (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	g_clear_object (&priv->pixbuf_cache);
	g_clear_object (&priv->page_cache);
}

/**
 * pps_view_set_page_cache_size:
 * @view: #PpsView instance
 * @cache_size: size in bytes
 *
 * Sets the maximum size in bytes that will be used to cache
 * rendered pages. Use 0 to disable caching rendered pages.
 *
 * Note that this limit doesn't affect the current visible page range,
 * which will always be rendered. In order to limit the total memory used
 * you have to use pps_document_model_set_max_scale() too.
 *
 */
void
pps_view_set_page_cache_size (PpsView *view,
                              gsize cache_size)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	if (priv->pixbuf_cache_size == cache_size)
		return;

	priv->pixbuf_cache_size = cache_size;
	if (priv->pixbuf_cache)
		pps_pixbuf_cache_set_max_size (priv->pixbuf_cache, cache_size);

	if (priv->model)
		view_update_scale_limits (view);
}

static void
pps_view_document_changed_cb (PpsDocumentModel *model,
                              GParamSpec *pspec,
                              PpsView *view)
{
	PpsDocument *document = pps_document_model_get_document (model);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	pps_view_remove_all (view);
	clear_caches (view);

	if (document == NULL)
		return;

	if (pps_document_get_n_pages (document) <= 0 ||
	    !pps_document_check_dimensions (document))
		return;

	setup_caches (view);

	if (pps_view_has_selection (view))
		clear_selection (view);

	if (priv->caret_enabled)
		preload_pages_for_caret_navigation (view);

	priv->start_page = -1;
	priv->end_page = -1;

	for (guint i = 0; i < priv->page_widgets->len; i++) {
		PpsViewPage *page = g_ptr_array_index (priv->page_widgets, i);

		pps_view_page_setup (page, priv->model, priv->annots_context,
		                     priv->search_context, priv->page_cache,
		                     priv->pixbuf_cache);
	}

	priv->keep_scroll_of_current_page = TRUE;
	pps_view_scroll_to_page (view, pps_document_model_get_page (model));

	view_update_scale_limits (view);

	/* in case the number/size of pages changed */
	gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
pps_view_rotation_changed_cb (PpsDocumentModel *model,
                              GParamSpec *pspec,
                              PpsView *view)
{
	gint rotation = pps_document_model_get_rotation (model);
	PpsDocument *document = pps_document_model_get_document (model);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!document)
		return;

	pps_pixbuf_cache_clear (priv->pixbuf_cache);
	if (!pps_document_is_page_size_uniform (document))
		pps_view_queue_rescroll_to_current_page (view);
	gtk_widget_queue_resize (GTK_WIDGET (view));

	pps_view_remove_all (view);
	view_update_scale_limits (view);

	if (rotation != 0)
		clear_selection (view);
}

static void
pps_view_sizing_mode_changed_cb (PpsDocumentModel *model,
                                 GParamSpec *pspec,
                                 PpsView *view)
{
	gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
update_can_zoom (PpsView *view)
{
	gdouble min_scale;
	gdouble max_scale;
	gdouble scale;
	gboolean can_zoom_in;
	gboolean can_zoom_out;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	min_scale = pps_document_model_get_min_scale (priv->model);
	max_scale = pps_document_model_get_max_scale (priv->model);
	scale = pps_document_model_get_scale (priv->model);

	can_zoom_in = scale < max_scale;
	can_zoom_out = scale > min_scale;

	if (can_zoom_in != priv->can_zoom_in) {
		priv->can_zoom_in = can_zoom_in;
		g_object_notify (G_OBJECT (view), "can-zoom-in");
	}

	if (can_zoom_out != priv->can_zoom_out) {
		priv->can_zoom_out = can_zoom_out;
		g_object_notify (G_OBJECT (view), "can-zoom-out");
	}
}

static void
pps_view_page_layout_changed_cb (PpsDocumentModel *model,
                                 GParamSpec *pspec,
                                 PpsView *view)
{
	pps_view_queue_rescroll_to_current_page (view);
	gtk_widget_queue_resize (GTK_WIDGET (view));

	/* FIXME: if we're keeping the pixbuf cache around, we should extend the
	 * preload_cache_size to be 2 if dual_page is set.
	 */
}

static void
pps_view_scale_changed_cb (PpsDocumentModel *model,
                           GParamSpec *pspec,
                           PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (pps_document_model_get_sizing_mode (priv->model) == PPS_SIZING_FREE)
		gtk_widget_queue_resize (GTK_WIDGET (view));

	update_can_zoom (view);
}

static void
pps_view_min_scale_changed_cb (PpsDocumentModel *model,
                               GParamSpec *pspec,
                               PpsView *view)
{
	update_can_zoom (view);
}

static void
pps_view_max_scale_changed_cb (PpsDocumentModel *model,
                               GParamSpec *pspec,
                               PpsView *view)
{
	update_can_zoom (view);
}

static void
pps_view_continuous_changed_cb (PpsDocumentModel *model,
                                GParamSpec *pspec,
                                PpsView *view)
{
	pps_view_queue_rescroll_to_current_page (view);
	gtk_widget_queue_resize (GTK_WIDGET (view));
}

static void
pps_view_dual_odd_left_changed_cb (PpsDocumentModel *model,
                                   GParamSpec *pspec,
                                   PpsView *view)
{
	if (pps_document_model_get_page_layout (model) == PPS_PAGE_LAYOUT_DUAL) {
		/* odd_left may be set when not in dual mode,
		   queue_resize is not needed in that case */
		pps_view_queue_rescroll_to_current_page (view);
		gtk_widget_queue_resize (GTK_WIDGET (view));
	}
}

static void
pps_view_direction_changed_cb (PpsDocumentModel *model,
                               GParamSpec *pspec,
                               PpsView *view)
{
	gboolean rtl = pps_document_model_get_rtl (model);
	gtk_widget_set_direction (GTK_WIDGET (view), rtl ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
	pps_view_queue_rescroll_to_current_page (view);
}

void
pps_view_set_model (PpsView *view,
                    PpsDocumentModel *model)
{
	g_return_if_fail (PPS_IS_VIEW (view));
	g_return_if_fail (PPS_IS_DOCUMENT_MODEL (model));
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (model == priv->model)
		return;

	if (priv->model) {
		g_signal_handlers_disconnect_by_data (priv->model, view);
	}

	g_set_object (&priv->model, model);

	/* Initialize view from model */
	gtk_widget_set_direction (GTK_WIDGET (view), pps_document_model_get_rtl (priv->model));

	g_signal_connect (priv->model, "notify::document",
	                  G_CALLBACK (pps_view_document_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::rotation",
	                  G_CALLBACK (pps_view_rotation_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::sizing-mode",
	                  G_CALLBACK (pps_view_sizing_mode_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::page-layout",
	                  G_CALLBACK (pps_view_page_layout_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::scale",
	                  G_CALLBACK (pps_view_scale_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::min-scale",
	                  G_CALLBACK (pps_view_min_scale_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::max-scale",
	                  G_CALLBACK (pps_view_max_scale_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::continuous",
	                  G_CALLBACK (pps_view_continuous_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::dual-odd-left",
	                  G_CALLBACK (pps_view_dual_odd_left_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::rtl",
	                  G_CALLBACK (pps_view_direction_changed_cb),
	                  view);
	g_signal_connect (priv->model, "page-changed",
	                  G_CALLBACK (pps_view_page_changed_cb),
	                  view);
	g_signal_connect (priv->model, "notify::inverted-colors",
	                  G_CALLBACK (pps_view_inverted_changed_cb),
	                  view);

	if (pps_document_model_get_inverted_colors (priv->model))
		gtk_widget_add_css_class (GTK_WIDGET (view), PPS_STYLE_CLASS_INVERTED);
	g_signal_connect (priv->model, "notify::annotation-editing-state",
	                  G_CALLBACK (pps_view_editing_state_changed),
	                  view);
}

static void
pps_view_reload_page (PpsView *view,
                      gint page,
                      cairo_region_t *region)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	pps_pixbuf_cache_reload_page (priv->pixbuf_cache,
	                              region,
	                              page,
	                              pps_document_model_get_rotation (priv->model),
	                              pps_document_model_get_scale (priv->model));
}

void
pps_view_reload (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	pps_pixbuf_cache_clear (priv->pixbuf_cache);
	gtk_widget_queue_allocate (GTK_WIDGET (view));
}

/*** Zoom and sizing mode ***/

static gboolean
pps_view_can_zoom (PpsView *view, gdouble factor)
{
	if (factor == 1.0)
		return TRUE;

	else if (factor < 1.0) {
		return pps_view_can_zoom_out (view);
	} else {
		return pps_view_can_zoom_in (view);
	}
}

gboolean
pps_view_can_zoom_in (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	return priv->can_zoom_in;
}

gboolean
pps_view_can_zoom_out (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	return priv->can_zoom_out;
}

static void
pps_view_zoom (PpsView *view, gdouble factor)
{
	gdouble scale;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_return_if_fail (pps_document_model_get_sizing_mode (priv->model) == PPS_SIZING_FREE);

	pps_view_cancel_kinetic_scrolling_deceleration (view);

	priv->pending_scroll = SCROLL_TO_CENTER;
	scale = pps_document_model_get_scale (priv->model) * factor;
	pps_document_model_set_scale (priv->model, scale);
}

void
pps_view_zoom_in (PpsView *view)
{
	if (!pps_view_can_zoom_in (view))
		return;
	pps_view_zoom (view, ZOOM_IN_FACTOR);
}

void
pps_view_zoom_out (PpsView *view)
{
	if (!pps_view_can_zoom_out (view))
		return;
	pps_view_zoom (view, ZOOM_OUT_FACTOR);
}

static double
zoom_for_size_fit_width (gdouble doc_width,
                         gdouble doc_height,
                         int target_width,
                         int target_height)
{
	return (double) target_width / doc_width;
}

static double
zoom_for_size_fit_height (gdouble doc_width,
                          gdouble doc_height,
                          int target_width,
                          int target_height)
{
	return (double) target_height / doc_height;
}

static double
zoom_for_size_fit_page (gdouble doc_width,
                        gdouble doc_height,
                        int target_width,
                        int target_height)
{
	double w_scale;
	double h_scale;

	w_scale = (double) target_width / doc_width;
	h_scale = (double) target_height / doc_height;

	return MIN (w_scale, h_scale);
}

static double
zoom_for_size_automatic (GtkWidget *widget,
                         gdouble doc_width,
                         gdouble doc_height,
                         int target_width,
                         int target_height)
{
	double fit_width_scale;
	double scale;

	fit_width_scale = zoom_for_size_fit_width (doc_width, doc_height, target_width, target_height);

	if (doc_height < doc_width) {
		double fit_height_scale;

		fit_height_scale = zoom_for_size_fit_height (doc_width, doc_height, target_width, target_height);
		scale = MIN (fit_width_scale, fit_height_scale);
	} else {
		scale = fit_width_scale;
	}

	return scale;
}

static void
pps_view_zoom_for_size_continuous_and_dual_page (PpsView *view,
                                                 int width,
                                                 int height)
{
	gdouble doc_width, doc_height;
	gdouble scale;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	gint rotation = pps_document_model_get_rotation (priv->model);

	pps_document_get_max_page_size (document, &doc_width, &doc_height);
	if (rotation == 90 || rotation == 270) {
		gdouble tmp;

		tmp = doc_width;
		doc_width = doc_height;
		doc_height = tmp;
	}

	doc_width *= 2;
	width -= 3 * priv->spacing;
	height -= 2 * priv->spacing;

	switch (pps_document_model_get_sizing_mode (priv->model)) {
	case PPS_SIZING_FIT_WIDTH:
		scale = zoom_for_size_fit_width (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_FIT_PAGE:
		scale = zoom_for_size_fit_page (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_AUTOMATIC:
		scale = zoom_for_size_automatic (GTK_WIDGET (view),
		                                 doc_width, doc_height, width, height);
		break;
	default:
		g_assert_not_reached ();
	}

	pps_document_model_set_scale (priv->model, scale);
}

static void
pps_view_zoom_for_size_continuous (PpsView *view,
                                   int width,
                                   int height)
{
	gdouble doc_width, doc_height;
	gdouble scale;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	gint rotation = pps_document_model_get_rotation (priv->model);

	pps_document_get_max_page_size (document, &doc_width, &doc_height);
	if (rotation == 90 || rotation == 270) {
		gdouble tmp;

		tmp = doc_width;
		doc_width = doc_height;
		doc_height = tmp;
	}

	width -= 2 * priv->spacing;
	height -= 2 * priv->spacing;

	switch (pps_document_model_get_sizing_mode (priv->model)) {
	case PPS_SIZING_FIT_WIDTH:
		scale = zoom_for_size_fit_width (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_FIT_PAGE:
		scale = zoom_for_size_fit_page (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_AUTOMATIC:
		scale = zoom_for_size_automatic (GTK_WIDGET (view),
		                                 doc_width, doc_height, width, height);
		break;
	default:
		g_assert_not_reached ();
	}

	pps_document_model_set_scale (priv->model, scale);
}

static void
pps_view_zoom_for_size_dual_page (PpsView *view,
                                  int width,
                                  int height)
{
	gdouble doc_width, doc_height;
	gdouble scale;
	gint other_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	/* Find the largest of the two. */
	get_doc_page_size (view, priv->current_page, &doc_width, &doc_height);
	if (get_dual_page_other_page (view, priv->current_page, &other_page, NULL) &&
	    other_page >= 0 && other_page < pps_document_get_n_pages (document)) {
		gdouble width_2, height_2;

		get_doc_page_size (view, other_page, &width_2, &height_2);
		if (width_2 > doc_width)
			doc_width = width_2;
		if (height_2 > doc_height)
			doc_height = height_2;
	}

	doc_width = doc_width * 2;
	width -= 3 * priv->spacing;
	height -= 2 * priv->spacing;

	switch (pps_document_model_get_sizing_mode (priv->model)) {
	case PPS_SIZING_FIT_WIDTH:
		scale = zoom_for_size_fit_width (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_FIT_PAGE:
		scale = zoom_for_size_fit_page (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_AUTOMATIC:
		scale = zoom_for_size_automatic (GTK_WIDGET (view),
		                                 doc_width, doc_height, width, height);
		break;
	default:
		g_assert_not_reached ();
	}

	pps_document_model_set_scale (priv->model, scale);
}

static void
pps_view_zoom_for_size_single_page (PpsView *view,
                                    int width,
                                    int height)
{
	gdouble doc_width, doc_height;
	gdouble scale;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	get_doc_page_size (view, priv->current_page, &doc_width, &doc_height);

	width -= 2 * priv->spacing;
	height -= 2 * priv->spacing;

	switch (pps_document_model_get_sizing_mode (priv->model)) {
	case PPS_SIZING_FIT_WIDTH:
		scale = zoom_for_size_fit_width (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_FIT_PAGE:
		scale = zoom_for_size_fit_page (doc_width, doc_height, width, height);
		break;
	case PPS_SIZING_AUTOMATIC:
		scale = zoom_for_size_automatic (GTK_WIDGET (view),
		                                 doc_width, doc_height, width, height);
		break;
	default:
		g_assert_not_reached ();
	}

	pps_document_model_set_scale (priv->model, scale);
}

static void
pps_view_zoom_for_size (PpsView *view,
                        int width,
                        int height)
{
	gboolean dual_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsSizingMode sizing_mode = pps_document_model_get_sizing_mode (priv->model);
	gboolean continuous = pps_document_model_get_continuous (priv->model);

	g_return_if_fail (PPS_IS_VIEW (view));
	g_return_if_fail (sizing_mode == PPS_SIZING_FIT_WIDTH ||
	                  sizing_mode == PPS_SIZING_FIT_PAGE ||
	                  sizing_mode == PPS_SIZING_AUTOMATIC);
	g_return_if_fail (width >= 0);
	g_return_if_fail (height >= 0);

	if (priv->model == NULL || pps_document_model_get_document (priv->model) == NULL)
		return;

	dual_page = is_dual_page (view, NULL);
	if (continuous && dual_page)
		pps_view_zoom_for_size_continuous_and_dual_page (view, width, height);
	else if (continuous)
		pps_view_zoom_for_size_continuous (view, width, height);
	else if (dual_page)
		pps_view_zoom_for_size_dual_page (view, width, height);
	else
		pps_view_zoom_for_size_single_page (view, width, height);
}

static gboolean
pps_view_page_fits (PpsView *view,
                    GtkOrientation orientation)
{
	GtkRequisition requisition;
	double size;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	int widget_width = gtk_widget_get_width (GTK_WIDGET (view));
	int widget_height = gtk_widget_get_height (GTK_WIDGET (view));
	PpsSizingMode sizing_mode = pps_document_model_get_sizing_mode (priv->model);

	if (sizing_mode == PPS_SIZING_FIT_PAGE)
		return TRUE;

	if (orientation == GTK_ORIENTATION_HORIZONTAL &&
	    (sizing_mode == PPS_SIZING_FIT_WIDTH ||
	     sizing_mode == PPS_SIZING_AUTOMATIC))
		return TRUE;

	pps_view_size_request (GTK_WIDGET (view), &requisition);

	if (orientation == GTK_ORIENTATION_HORIZONTAL) {
		if (requisition.width == 1) {
			size = 1.0;
		} else {
			if (widget_width > 0.0)
				size = (double) requisition.width / widget_width;
			else
				size = 1.0;
		}
	} else {
		if (requisition.height == 1) {
			size = 1.0;
		} else {
			if (widget_height > 0.0)
				size = (double) requisition.height / widget_height;
			else
				size = 1.0;
		}
	}

	return size <= 1.0;
}

/*** Find ***/
static void
jump_to_find_result (PpsView *view, guint page, GList *rect_list)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsRectangle rect;
	PpsFindRectangle *find_rect, *rect_next = NULL;
	GdkRectangle view_rect;

	find_rect = (PpsFindRectangle *) rect_list->data;
	rect.x1 = find_rect->x1;
	rect.y1 = find_rect->y1;
	rect.x2 = find_rect->x2;
	rect.y2 = find_rect->y2;
	while (rect_list->next) {
		rect_list = rect_list->next;
		rect_next = (PpsFindRectangle *) rect_list->data;
		/* For an across-lines match, make sure both rectangles are visible */
		rect.x1 = MIN (rect.x1, rect_next->x1);
		rect.y1 = MIN (rect.y1, rect_next->y1);
		rect.x2 = MAX (rect.x2, rect_next->x2);
		rect.y2 = MAX (rect.y2, rect_next->y2);
	}
	_pps_view_transform_doc_rect_to_view_rect (view, page,
	                                           &rect, &view_rect);
	_pps_view_ensure_rectangle_is_visible (view, page, &view_rect);
	if (priv->caret_enabled && pps_document_model_get_rotation (priv->model) == 0)
		position_caret_cursor_at_doc_point (view, page,
		                                    find_rect->x1, find_rect->y1);
}

static void
pps_view_search_result_changed_cb (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsSearchResult *result = gtk_single_selection_get_selected_item (pps_search_context_get_result_model (priv->search_context));
	guint page;

	if (result == NULL)
		return;

	page = pps_search_result_get_page (result);

	jump_to_find_result (view, page,
	                     pps_search_result_get_rectangle_list (result));
}

void
pps_view_set_search_context (PpsView *view,
                             PpsSearchContext *context)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_return_if_fail (PPS_IS_SEARCH_CONTEXT (context));

	if (priv->search_context != NULL) {
		g_signal_handlers_disconnect_by_func (pps_search_context_get_result_model (priv->search_context), pps_view_search_result_changed_cb, view);
	}

	g_set_object (&priv->search_context, context);

	g_signal_connect_object (pps_search_context_get_result_model (priv->search_context),
	                         "notify::selected-item",
	                         G_CALLBACK (pps_view_search_result_changed_cb),
	                         view, G_CONNECT_SWAPPED);
}

/*** Selections ***/
G_DEFINE_BOXED_TYPE (PpsViewSelection, pps_view_selection, pps_view_selection_copy, pps_view_selection_free)

PpsViewSelection *
pps_view_selection_copy (PpsViewSelection *selection)
{
	PpsViewSelection *new_selection;

	g_return_val_if_fail (selection != NULL, NULL);

	new_selection = g_new0 (PpsViewSelection, 1);
	*new_selection = *selection;
	if (new_selection->covered_region)
		new_selection->covered_region =
		    cairo_region_reference (new_selection->covered_region);

	return new_selection;
}

void
pps_view_selection_free (PpsViewSelection *selection)
{
	g_clear_pointer (&selection->covered_region, cairo_region_destroy);
	g_free (selection);
}

static gboolean
gdk_rectangle_point_in (GdkRectangle *rectangle,
                        gdouble x,
                        gdouble y)
{
	return rectangle->x <= x &&
	       rectangle->y <= y &&
	       x < rectangle->x + rectangle->width &&
	       y < rectangle->y + rectangle->height;
}

static gboolean
get_selection_page_range (PpsView *view,
                          graphene_point_t *start,
                          graphene_point_t *stop,
                          gint *first_page,
                          gint *last_page)
{
	gint start_page, end_page;
	gint first, last;
	gint i, n_pages;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	n_pages = pps_document_get_n_pages (document);

	if (graphene_point_equal (start, stop)) {
		start_page = priv->start_page;
		end_page = priv->end_page;
	} else if (pps_document_model_get_continuous (priv->model)) {
		start_page = 0;
		end_page = n_pages - 1;
	} else if (is_dual_page (view, NULL)) {
		start_page = priv->start_page;
		end_page = priv->end_page;
	} else {
		start_page = priv->current_page;
		end_page = priv->current_page;
	}

	first = -1;
	last = -1;
	for (i = start_page; i <= end_page; i++) {
		GdkRectangle page_area;

		pps_view_get_page_extents (view, i, &page_area);
		if (gdk_rectangle_point_in (&page_area, start->x, start->y) ||
		    gdk_rectangle_point_in (&page_area, stop->x, stop->y)) {
			if (first == -1)
				first = i;
			last = i;
		}
	}

	if (first != -1 && last != -1) {
		*first_page = first;
		*last_page = last;

		return TRUE;
	}

	return FALSE;
}

static GList *
compute_new_selection (PpsView *view,
                       PpsSelectionStyle style,
                       graphene_point_t *start,
                       graphene_point_t *stop)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble scale = pps_document_model_get_scale (priv->model);
	int i, first, last;
	GList *list = NULL;

	/* First figure out the range of pages the selection affects. */
	if (!get_selection_page_range (view, start, stop, &first, &last))
		return list;

	/* If everything is equal, then there's nothing to select */
	if (first == last && graphene_point_equal (start, stop) && style == PPS_SELECTION_STYLE_GLYPH)
		return list;

	/* Now create a list of PpsViewSelection's for the affected
	 * pages. This could be an empty list, a list of just one
	 * page or a number of pages.*/
	for (i = first; i <= last; i++) {
		PpsViewSelection *selection;
		GdkRectangle page_area;
		gdouble width, height;
		gdouble point_x, point_y;

		get_doc_page_size (view, i, &width, &height);

		selection = g_new0 (PpsViewSelection, 1);
		selection->page = i;
		selection->style = style;
		selection->rect.x1 = selection->rect.y1 = 0;
		selection->rect.x2 = width;
		selection->rect.y2 = height;

		pps_view_get_page_extents (view, i, &page_area);
		if (gdk_rectangle_point_in (&page_area, start->x, start->y)) {
			point_x = start->x;
			point_y = start->y;
		} else {
			point_x = stop->x;
			point_y = stop->y;
		}

		if (i == first) {
			selection->rect.x1 = MAX ((double) (point_x - page_area.x) / scale, 0);
			selection->rect.y1 = MAX ((double) (point_y - page_area.y) / scale, 0);
		}

		/* If the selection is contained within just one page,
		 * make sure we don't write 'start' into both points
		 * in selection->rect. */
		if (first == last) {
			point_x = stop->x;
			point_y = stop->y;
		}

		if (i == last) {
			selection->rect.x2 = MAX ((double) (point_x - page_area.x) / scale, 0);
			selection->rect.y2 = MAX ((double) (point_y - page_area.y) / scale, 0);
		}

		list = g_list_prepend (list, selection);
	}

	return g_list_reverse (list);
}

/* This function takes the newly calculated list, and figures out which regions
 * have changed.  It then queues a redraw appropriately.
 */
static void
merge_selection_region (PpsView *view,
                        GList *new_list)
{
	GList *old_list;
	GList *new_list_ptr, *old_list_ptr;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	/* Update the selection */
	old_list = pps_pixbuf_cache_get_selection_list (priv->pixbuf_cache);
	g_clear_list (&priv->selection_info.selections,
	              (GDestroyNotify) pps_view_selection_free);
	priv->selection_info.selections = new_list;
	pps_pixbuf_cache_set_selection_list (priv->pixbuf_cache, new_list);
	g_signal_emit (view, signals[SIGNAL_SELECTION_CHANGED], 0, NULL);

	new_list_ptr = new_list;
	old_list_ptr = old_list;

	while (new_list_ptr || old_list_ptr) {
		PpsViewSelection *old_sel, *new_sel;
		int cur_page;

		new_sel = (new_list_ptr) ? (new_list_ptr->data) : NULL;
		old_sel = (old_list_ptr) ? (old_list_ptr->data) : NULL;

		/* Assume that the lists are in order, and we run through them
		 * comparing them, one page at a time.  We come out with the
		 * first page we see. */
		if (new_sel && old_sel) {
			if (new_sel->page < old_sel->page) {
				new_list_ptr = new_list_ptr->next;
				old_sel = NULL;
			} else if (new_sel->page > old_sel->page) {
				old_list_ptr = old_list_ptr->next;
				new_sel = NULL;
			} else {
				new_list_ptr = new_list_ptr->next;
				old_list_ptr = old_list_ptr->next;
			}
		} else if (new_sel) {
			new_list_ptr = new_list_ptr->next;
		} else if (old_sel) {
			old_list_ptr = old_list_ptr->next;
		}

		g_assert (new_sel || old_sel);

		/* is the page we're looking at on the screen?*/
		cur_page = new_sel ? new_sel->page : old_sel->page;
		if (cur_page < priv->start_page || cur_page > priv->end_page)
			continue;

		/* seed the cache with a new page.  We are going to need the new
		 * region too. */
		if (new_sel) {
			cairo_region_t *tmp_region;

			tmp_region = pps_pixbuf_cache_get_selection_region (priv->pixbuf_cache,
			                                                    cur_page,
			                                                    pps_document_model_get_scale (priv->model));

			g_clear_pointer (&new_sel->covered_region, cairo_region_destroy);

			if (tmp_region)
				new_sel->covered_region = cairo_region_reference (tmp_region);
		}
	}

	pps_view_check_cursor_blink (view);

	/* Free the old list, now that we're done with it. */
	g_clear_list (&old_list, (GDestroyNotify) pps_view_selection_free);
}

static void
compute_selections (PpsView *view,
                    PpsSelectionStyle style,
                    gdouble start_x,
                    gdouble start_y,
                    gdouble stop_x,
                    gdouble stop_y)
{
	graphene_point_t start = GRAPHENE_POINT_INIT (start_x, start_y);
	graphene_point_t stop = GRAPHENE_POINT_INIT (stop_x, stop_y);

	merge_selection_region (view, compute_new_selection (view, style, &start, &stop));
}

static void
clear_selection (PpsView *view)
{
	merge_selection_region (view, NULL);
}

void
pps_view_select_all (PpsView *view)
{
	GList *selections = NULL;
	int n_pages, i;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	/* Disable selection on rotated pages for the 0.4.0 series */
	if (pps_document_model_get_rotation (priv->model) != 0)
		return;

	n_pages = pps_document_get_n_pages (document);
	for (i = 0; i < n_pages; i++) {
		gdouble width, height;
		PpsViewSelection *selection;

		get_doc_page_size (view, i, &width, &height);

		selection = g_new0 (PpsViewSelection, 1);
		selection->page = i;
		selection->style = PPS_SELECTION_STYLE_GLYPH;
		selection->rect.x1 = selection->rect.y1 = 0;
		selection->rect.x2 = width;
		selection->rect.y2 = height;

		selections = g_list_prepend (selections, selection);
	}

	merge_selection_region (view, g_list_reverse (selections));
}

gboolean
pps_view_has_selection (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	g_return_val_if_fail (PPS_IS_VIEW (view), FALSE);

	return priv->selection_info.selections != NULL;
}

void
_pps_view_clear_selection (PpsView *view)
{
	clear_selection (view);
}

void
_pps_view_set_selection (PpsView *view,
                         gdouble start_x,
                         gdouble start_y,
                         gdouble stop_x,
                         gdouble stop_y)
{
	compute_selections (view, PPS_SELECTION_STYLE_GLYPH, start_x, start_y, stop_x, stop_y);
}

/**
 * pps_view_get_selected_text:
 * @view: #PpsView instance
 *
 * Returns a pointer to a constant string containing the selected
 * text in the view.
 *
 * The value returned may be NULL if there is no selected text.
 *
 * Returns: The string representing selected text.
 */
char *
pps_view_get_selected_text (PpsView *view)
{
	GString *text;
	GList *l;
	gchar *normalized_text;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);

	text = g_string_new (NULL);

	for (l = priv->selection_info.selections; l != NULL; l = l->next) {
		PpsViewSelection *selection = (PpsViewSelection *) l->data;
		PpsPage *page;
		gchar *tmp;

		page = pps_document_get_page (document, selection->page);
		tmp = pps_selection_get_selected_text (PPS_SELECTION (document),
		                                       page, selection->style,
		                                       &(selection->rect));
		g_object_unref (page);
		g_string_append (text, tmp);
		g_free (tmp);
	}

	/* For copying text from the document to the clipboard, we want a normalization
	 * that preserves 'canonical equivalence' i.e. that text after normalization
	 * is not visually different than the original text. Issue #1085 */
	normalized_text = g_utf8_normalize (text->str, text->len, G_NORMALIZE_NFC);
	g_string_free (text, TRUE);
	return normalized_text;
}

static gboolean
pps_view_get_page_points_from_selection_region (PpsView *view,
                                                gint page,
                                                PpsPoint *begin,
                                                PpsPoint *end)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	gdouble scale = pps_document_model_get_scale (priv->model);
	cairo_rectangle_int_t first, last;
	gdouble start_x, start_y, stop_x, stop_y;
	cairo_region_t *region = NULL;

	if (!priv->pixbuf_cache)
		return FALSE;

	region = pps_pixbuf_cache_get_selection_region (priv->pixbuf_cache, page, scale);

	if (!region)
		return FALSE;

	cairo_region_get_rectangle (region, 0, &first);
	cairo_region_get_rectangle (region, cairo_region_num_rectangles (region) - 1, &last);

	get_page_point_from_offset (view, page, first.x, first.y + (first.height / 2),
	                            &start_x, &start_y);

	get_page_point_from_offset (view, page, last.x + last.width, last.y + (last.height / 2),
	                            &stop_x, &stop_y);

	begin->x = start_x;
	begin->y = start_y;
	end->x = stop_x;
	end->y = stop_y;

	return TRUE;
}

/**
 * pps_view_get_selections:
 * @view: #PpsView instance
 *
 * Returns: (element-type PpsViewSelection) (transfer container): a list with the
 * current selections.
 *
 * Since: 48.0
 */
GList *
pps_view_get_selections (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);
	GList *selections = NULL;

	if (!pps_view_has_selection (view))
		return NULL;

	for (GList *l = priv->selection_info.selections; l != NULL; l = l->next) {
		PpsViewSelection *selection = (PpsViewSelection *) l->data;

		/* Check if selection is of double/triple click type
		 * (STYLE_WORD and STYLE_LINE). In that
		 * case get the start/end points from the selection region of
		 * pixbuf cache. The reason is that when we create those selections,
		 * we create them based on a single point. See Issue evince#1119 for
		 * background. In the future, we should probably do this when
		 * creating the selections, instead of at get time */
		if (selection->style == PPS_SELECTION_STYLE_WORD ||
		    selection->style == PPS_SELECTION_STYLE_LINE) {
			PpsPoint page_point_start;
			PpsPoint page_point_end;
			if (!pps_view_get_page_points_from_selection_region (view, selection->page,
			                                                     &page_point_start, &page_point_end))
				continue;
			selection->rect.x1 = page_point_start.x;
			selection->rect.y1 = page_point_start.y;
			selection->rect.x2 = page_point_end.x;
			selection->rect.y2 = page_point_end.y;
		}
		selections = g_list_prepend (selections, selection);
	}

	return g_list_reverse (selections);
}

static void
pps_view_clipboard_copy (PpsView *view,
                         const gchar *text)
{
	GdkClipboard *clipboard;

	clipboard = gtk_widget_get_clipboard (GTK_WIDGET (view));
	gdk_clipboard_set_text (clipboard, text);
}

static void
pps_view_update_primary_selection (PpsView *view)
{
	char *text = NULL;
	GdkClipboard *clipboard;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!PPS_IS_SELECTION (pps_document_model_get_document (priv->model)))
		return;

	if (priv->link_selected) {
		text = g_strdup (pps_link_action_get_uri (priv->link_selected));
	} else if (priv->selection_info.selections) {
		text = pps_view_get_selected_text (view);
	}

	if (text) {
		clipboard = gtk_widget_get_primary_clipboard (GTK_WIDGET (view));
		gdk_clipboard_set_text (clipboard, text);
		g_free (text);
	}
}

void
pps_view_copy (PpsView *view)
{
	char *text;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (!PPS_IS_SELECTION (pps_document_model_get_document (priv->model)))
		return;

	text = pps_view_get_selected_text (view);
	pps_view_clipboard_copy (view, text);
	g_free (text);
}

void
pps_view_copy_link_address (PpsView *view,
                            PpsLinkAction *action)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_set_object (&priv->link_selected, action);

	pps_view_clipboard_copy (view, pps_link_action_get_uri (action));
	pps_view_update_primary_selection (view);
}

/*** Cursor operations ***/
static void
pps_view_set_cursor (PpsView *view, PpsViewCursor new_cursor)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	if (priv->cursor == new_cursor) {
		return;
	}

	priv->cursor = new_cursor;

	gtk_widget_set_cursor_from_name (GTK_WIDGET (view),
	                                 pps_view_cursor_name (new_cursor));
}

gboolean
pps_view_next_page (PpsView *view)
{
	gint next_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_return_val_if_fail (PPS_IS_VIEW (view), FALSE);

	next_page = go_to_next_page (view, priv->current_page);
	if (next_page == -1)
		return FALSE;

	pps_document_model_set_page (priv->model, next_page);

	return TRUE;
}

gboolean
pps_view_previous_page (PpsView *view)
{
	gint prev_page;
	PpsViewPrivate *priv = GET_PRIVATE (view);

	g_return_val_if_fail (PPS_IS_VIEW (view), FALSE);

	prev_page = go_to_previous_page (view, priv->current_page);
	if (prev_page == -1)
		return FALSE;

	pps_document_model_set_page (priv->model, prev_page);

	return TRUE;
}

void
pps_view_set_allow_links_change_zoom (PpsView *view, gboolean allowed)
{
	g_return_if_fail (PPS_IS_VIEW (view));
	PpsViewPrivate *priv = GET_PRIVATE (view);

	priv->allow_links_change_zoom = allowed;
}

gboolean
pps_view_get_allow_links_change_zoom (PpsView *view)
{
	g_return_val_if_fail (PPS_IS_VIEW (view), FALSE);
	PpsViewPrivate *priv = GET_PRIVATE (view);

	return priv->allow_links_change_zoom;
}

void
pps_view_start_signature_rect (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	pps_view_set_cursor (view, PPS_VIEW_CURSOR_ADD);
	gtk_event_controller_set_propagation_phase (priv->signing_drag_gesture,
	                                            GTK_PHASE_CAPTURE);
}

void
pps_view_cancel_signature_rect (PpsView *view)
{
	PpsViewPrivate *priv = GET_PRIVATE (view);

	gtk_event_controller_set_propagation_phase (priv->signing_drag_gesture,
	                                            GTK_PHASE_NONE);
	gtk_gesture_set_state (GTK_GESTURE (priv->signing_drag_gesture),
	                       GTK_EVENT_SEQUENCE_DENIED);
}

static void
pps_view_stop_signature_rect (PpsView *view)
{
	PpsRectangle rect = { 0 };
	PpsRectangle r;
	PpsViewPrivate *priv = GET_PRIVATE (view);
	g_autofree PpsDocumentPoint *start;
	g_autofree PpsDocumentPoint *end;
	gint selection_page = -1;

	r.x1 = MIN (priv->signing_info.start_x, priv->signing_info.stop_x);
	r.y1 = MIN (priv->signing_info.start_y, priv->signing_info.stop_y);
	r.x2 = MAX (priv->signing_info.start_x, priv->signing_info.stop_x);
	r.y2 = MAX (priv->signing_info.start_y, priv->signing_info.stop_y);

	start = pps_view_get_document_point_for_view_point (view, r.x1, r.y1);
	end = pps_view_get_document_point_for_view_point (view, r.x2, r.y2);

	if (!start || !end) {
		/* if start or end are outside the page extents, let's try to clamp them */
		gint page = -1;

		if (start)
			page = start->page_index;
		if (end)
			page = end->page_index;

		if (page == -1) {
			/* If both start and end are outside the document area, let's try to see
			 * if at least the center of the selection is hitting a page...
			 */
			find_page_at_location (view,
			                       (r.x1 + r.x2) / 2,
			                       (r.y1 + r.y2) / 2,
			                       &page, NULL, NULL);
		}

		if (page != -1) {
			GdkRectangle page_area;

			/* Now that we have a page let's clamp the signing area to its extents */
			pps_view_get_page_extents (view, page, &page_area);

			if (!start) {
				start = pps_view_get_document_point_for_view_point (view,
				                                                    MAX (page_area.x, r.x1),
				                                                    MAX (page_area.y, r.y1));
			}

			if (!end) {
				end = pps_view_get_document_point_for_view_point (view,
				                                                  MIN (page_area.x + page_area.width - 1, r.x2),
				                                                  MIN (page_area.y + page_area.height - 1, r.y2));
			}
		}
	}

	if (start && end) {
		selection_page = start->page_index;
		rect.x1 = start->point_on_page.x;
		rect.y1 = start->point_on_page.y;
		rect.x2 = end->point_on_page.x;
		rect.y2 = end->point_on_page.y;
	}

	gtk_event_controller_set_propagation_phase (priv->signing_drag_gesture,
	                                            GTK_PHASE_NONE);

	g_signal_emit (view, signals[SIGNAL_SIGNATURE_RECT], 0, selection_page, &rect);
	gtk_widget_queue_draw (GTK_WIDGET (view));
}
