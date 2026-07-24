// SPDX-License-Identifier: GPL-2.0-or-later
/* pps-annotation-layer-objects.c
 *  this file is part of papers, a gnome document viewer
 *
 * Copyright (C) 2025 Lucas Baudin <lbaudin@gnome.org>
 */

#include "pps-annotation-layer-objects.h"

#include "gtk/gtk.h"
#include "pps-annotation-model.h"

struct _PpsAnnotationLayerObjects {
	PpsAnnotationLayer base_instance;
};

enum { PROP_0 };

G_DEFINE_TYPE (PpsAnnotationLayerObjects, pps_annotation_layer_objects, PPS_TYPE_ANNOTATION_LAYER)

#define GET_ANNOT_MODEL(d) pps_document_model_get_annotation_model (pps_annotation_layer_get_doc_model (PPS_ANNOTATION_LAYER (d)))

#define GET_DOC_MODEL(d) pps_annotation_layer_get_doc_model (PPS_ANNOTATION_LAYER (d))
#define GET_ANNOT_CONTEXT(d) pps_annotation_layer_get_annotations_context (PPS_ANNOTATION_LAYER (d))

static void
pps_annotation_layer_objects_pressed (GtkGestureClick *gesture, int n_pressed, gdouble x, gdouble y, PpsAnnotationLayerObjects *layer)
{
	PpsAnnotationsContext *context = GET_ANNOT_CONTEXT (layer);
	PpsPoint start;
	PpsPoint end;
	GdkRGBA *color = pps_annotation_model_get_text_color (GET_ANNOT_MODEL (layer));
	gdouble scale = pps_document_model_get_scale (GET_DOC_MODEL (layer));

	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

	start.x = x / scale;
	start.y = y / scale;
	end.x = x / scale;
	end.y = y / scale + 11.;

	pps_annotations_context_add_annotation_sync (context,
	                                             pps_annotation_layer_get_page (PPS_ANNOTATION_LAYER (layer)),
	                                             PPS_ANNOTATION_TYPE_FREE_TEXT, &start, &end, color,
	                                             pps_annotation_model_get_font_desc (GET_ANNOT_MODEL (layer)));
}

static void
pps_annotation_layer_objects_init (PpsAnnotationLayerObjects *draw)
{
	gtk_widget_set_cursor_from_name (GTK_WIDGET (draw), "text");

	GtkGesture *gesture = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 1);

	g_signal_connect_after (gesture, "pressed", G_CALLBACK (pps_annotation_layer_objects_pressed), draw);

	gtk_widget_add_controller (GTK_WIDGET (draw), GTK_EVENT_CONTROLLER (gesture));

	gtk_widget_set_size_request (GTK_WIDGET (draw), 100, 100);
}

static void
pps_annotation_layer_objects_class_init (PpsAnnotationLayerObjectsClass *klass)
{
}

GtkWidget *
pps_annotation_layer_objects_new (PpsDocument *document,
                                  PpsDocumentModel *model,
                                  PpsAnnotationsContext *annotations_context)
{
	GtkWidget *draw;

	g_return_val_if_fail (PPS_IS_DOCUMENT (document), NULL);

	draw = g_object_new (PPS_TYPE_ANNOTATION_LAYER_OBJECTS,
	                     "model",
	                     model,
	                     "annotations-context",
	                     annotations_context,
	                     NULL);

	return draw;
}
