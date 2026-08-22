// SPDX-License-Identifier: GPL-2.0-or-later
/* this file is part of papers, a gnome document viewer
 *
 *  Copyright (C) 2004 Red Hat, Inc
 */

#pragma once

#if !defined(PAPERS_COMPILATION)
#error "This is a private header."
#endif

#include "adwaita.h"
#include "factory/pps-element-widget-factory.h"
#include "pps-form-field.h"
#include "pps-image.h"
#include "pps-jobs.h"
#include "pps-page-cache.h"
#include "pps-pixbuf-cache.h"
#include "pps-selection.h"
#include "pps-view-cursor.h"
#include "pps-view.h"

/* Information for middle clicking and moving around the doc */
typedef struct
{
	gdouble last_offset_x;
	gdouble last_offset_y;
	guint release_timeout_id;
	gdouble momentum_x;
	gdouble momentum_y;
	gboolean in_scroll_notify;
} DragInfo;

/* Information for handling selection */
typedef struct
{
	gdouble start_x;
	gdouble start_y;
	GList *selections;
	PpsSelectionStyle style;
	gdouble motion_x;
	gdouble motion_y;
} SelectionInfo;

typedef enum {
	SCROLL_TO_KEEP_POSITION,
	SCROLL_TO_CENTER,
} PendingScroll;

typedef struct _PpsHeightToPageCache {
	gint rotation;
	gboolean dual_even_left;
	gdouble *height_to_page;
	gdouble *dual_height_to_page;
} PpsHeightToPageCache;

/* Information for handling link preview thumbnails */
typedef struct
{
	PpsJob *job;
	gdouble left;
	gdouble top;
	GtkWidget *popover;
	PpsLink *link;
	gint source_page;
	guint delay_timeout_id;
} PpsLinkPreview;

typedef struct
{
	gdouble start_x;
	gdouble start_y;
	gdouble stop_x;
	gdouble stop_y;
} SigningInfo;

enum {
	FORM_FACTORY,
	ANNOT_FACTORY,
	WIDGET_FACTORY_COUNT
};

typedef struct _PpsViewPrivate {
	/* Find */
	PpsSearchContext *search_context;

	PpsDocumentModel *model;
	PpsPixbufCache *pixbuf_cache;
	gsize pixbuf_cache_size;
	PpsPageCache *page_cache;
	PpsHeightToPageCache *height_to_page_cache;
	PpsViewCursor cursor;
	GPtrArray *page_widgets;

	GtkRequisition requisition;

	/* Scrolling */
	GtkAdjustment *hadjustment;
	GtkAdjustment *vadjustment;
	/* GtkScrollablePolicy needs to be checked when
	 * driving the scrollable adjustment values */
	guint hscroll_policy : 1;
	guint vscroll_policy : 1;

	guint update_cursor_idle_id;

	PendingScroll pending_scroll;
	gboolean needs_scrolling_to_current_page;
	gboolean keep_scroll_of_current_page;

	/* Animation for scrolling with keys */
	AdwAnimation *scroll_animation_vertical;
	AdwAnimation *scroll_animation_horizontal;
	gboolean pending_scroll_animation;

	/* Current geometry */

	gint start_page;
	gint end_page;
	gint current_page;

	gint spacing;

	gboolean can_zoom_in;
	gboolean can_zoom_out;
	gboolean allow_links_change_zoom;

	/* Key bindings propagation */
	gboolean key_binding_handled;

	/* Information for middle clicking and dragging around. */
	DragInfo drag_info;

	/* Selection */
	gdouble motion_x;
	gdouble motion_y;
	guint selection_update_id;
	guint selection_scroll_id;

	SelectionInfo selection_info;

	/* Copy link address selection */
	PpsLinkAction *link_selected;

	/* Image DND */
	PpsImage *dnd_image;

	/* Annotations */
	PpsAnnotationsContext *annots_context;
	gboolean enable_spellchecking;

	/* Focus & Caret navigation */
	PpsMapping *focused_element;
	guint focused_element_page;
	guint child_focus_idle_id;

	gboolean caret_enabled;
	gint cursor_offset;
	gint cursor_page;
	gdouble cursor_line_offset;
	gboolean cursor_visible;
	guint cursor_blink_timeout_id;
	guint cursor_blink_time;

	/* Gestures */
	GtkGesture *middle_clicked_drag_gesture;
	GtkGesture *middle_clicked_drag_swipe_gesture;
	gdouble prev_zoom_gesture_scale;

	/* Current zoom center */
	gdouble zoom_center_x;
	gdouble zoom_center_y;

	/* Link preview */
	PpsLinkPreview link_preview;

	/* Signing Info */
	SigningInfo signing_info;
	GtkEventController *signing_drag_gesture;

	/* Widgets Factory */
	PpsElementWidgetFactory *widget_factories[WIDGET_FACTORY_COUNT];
} PpsViewPrivate;

struct _PpsViewClass {
	GtkWidgetClass parent_class;

	gboolean (*scroll) (PpsView *view,
	                    GtkScrollType scroll,
	                    GtkOrientation orientation);
	void (*handle_link) (PpsView *view,
	                     gint old_page,
	                     PpsLink *link);
	void (*external_link) (PpsView *view,
	                       PpsLinkAction *action);
	void (*popup_menu) (PpsView *view,
	                    GList *items);
	void (*selection_changed) (PpsView *view);
	void (*annot_removed) (PpsView *view,
	                       PpsAnnotation *annot);
	void (*layers_changed) (PpsView *view);
	gboolean (*move_cursor) (PpsView *view,
	                         GtkMovementStep step,
	                         gint count,
	                         gboolean extend_selection);
	void (*activate) (PpsView *view);
	void (*signature_rect) (PpsView *view,
	                        guint page,
	                        PpsRectangle *rectangle);
};

void pps_view_get_page_extents (PpsView *view,
                                gint page,
                                GdkRectangle *page_area);
PpsPoint pps_view_get_point_on_page (PpsView *view,
                                     gint page_index,
                                     gdouble view_point_x,
                                     gdouble view_point_y);
void _get_page_size_for_scale_and_rotation (PpsDocument *document,
                                            gint page,
                                            gdouble scale,
                                            gint rotation,
                                            gint *page_width,
                                            gint *page_height);
void _pps_view_transform_doc_rect_to_view_rect (PpsView *view,
                                                int page,
                                                const PpsRectangle *doc_rect,
                                                GdkRectangle *view_rect);
gint _pps_view_get_caret_cursor_offset_at_doc_point (PpsView *view,
                                                     gint page,
                                                     gdouble doc_x,
                                                     gdouble doc_y);
void _pps_view_clear_selection (PpsView *view);
void _pps_view_set_selection (PpsView *view,
                              gdouble start_x,
                              gdouble start_y,
                              gdouble stop_x,
                              gdouble stop_y);

void _pps_view_set_focused_element (PpsView *view,
                                    PpsMapping *element_mapping,
                                    gint page);
void _pps_view_focus_form_field (PpsView *view,
                                 PpsFormField *field);

void _pps_view_ensure_rectangle_is_visible (PpsView *view,
                                            gint page,
                                            GdkRectangle *rect);
