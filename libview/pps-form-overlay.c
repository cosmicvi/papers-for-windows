// SPDX-License-Identifier: GPL-2.0-or-later
/* pps-form-overlay.c
 * this file is part of papers, a gnome document viewer
 *
 * Copyright (C) 2025 Lucas Baudin <lbaudin@gnome.org>
 */

#include "pps-form-overlay.h"

#include "pps-form-field-private.h"
#include "pps-overlay.h"
#include "pps-view.h"

typedef struct {
	PpsFormField *field;
	PpsPageCache *page_cache;
	PpsDocumentModel *model;
	PpsPixbufCache *pixbuf_cache;
} PpsOverlayFormPrivate;

static void
pps_overlay_form_iface_init (PpsOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PpsOverlayForm, pps_overlay_form, GTK_TYPE_BOX, G_ADD_PRIVATE (PpsOverlayForm) G_IMPLEMENT_INTERFACE (PPS_TYPE_OVERLAY, pps_overlay_form_iface_init))

#define GET_PRIVATE(o) pps_overlay_form_get_instance_private (PPS_OVERLAY_FORM (o))

enum {
	PROP_0,
	PROP_DOCUMENT_MODEL,
	PROP_FIELD,
	PROP_PAGE_CACHE,
	PROP_PIXBUF_CACHE,
	NUM_PROPERTIES
};

static GParamSpec *props[NUM_PROPERTIES] = {
	NULL,
};

/*** Forms ***/
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static void
pps_overlay_form_field_destroy (GtkWidget *widget,
                                PpsOverlayForm *view)
{
	gtk_widget_grab_focus (gtk_widget_get_parent (GTK_WIDGET (view)));
}

static void
pps_overlay_form_field_button_toggle (PpsOverlayForm *view,
                                      PpsFormField *field)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (view);
	gboolean state;
	PpsFormFieldButton *field_button = PPS_FORM_FIELD_BUTTON (field);

	if (field_button->type == PPS_FORM_FIELD_BUTTON_PUSH)
		return;

	state = pps_document_forms_form_field_button_get_state (PPS_DOCUMENT_FORMS (pps_document_model_get_document (priv->model)),
	                                                        field);

	/* FIXME: it actually depends on NoToggleToOff flags */
	if (field_button->type == PPS_FORM_FIELD_BUTTON_RADIO && state && field_button->state)
		return;

	/* Update state */
	pps_document_forms_form_field_button_set_state (PPS_DOCUMENT_FORMS (pps_document_model_get_document (priv->model)),
	                                                field,
	                                                !state);
	field_button->state = !state;

	pps_pixbuf_cache_reload_page (priv->pixbuf_cache,
	                              NULL,
	                              priv->field->page->index,
	                              pps_document_model_get_rotation (priv->model),
	                              pps_document_model_get_scale (priv->model));
}

static void
on_check_button_toggled (GtkWidget *widget, PpsOverlayForm *form)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (form);

	pps_overlay_form_field_button_toggle (form, priv->field);
}

static void
on_link_clicked (GtkWidget *widget, PpsOverlayForm *form)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (form);

	if (priv->field->activation_link) {
		/* FIXME: this is a hack and should be done correctly elsewhere, probably
		using a FormContext */
		PpsView *view = PPS_VIEW (gtk_widget_get_parent (gtk_widget_get_parent (GTK_WIDGET (form))));
		pps_view_handle_link (view, priv->field->activation_link);
	}
}

static GtkWidget *
pps_overlay_form_field_button_create_widget (PpsOverlayForm *view,
                                             PpsFormField *field)
{
	GtkWidget *button;
	PpsFormFieldButton *field_button = PPS_FORM_FIELD_BUTTON (field);

	if (field_button->type == PPS_FORM_FIELD_BUTTON_PUSH) {
		button = gtk_button_new ();
		g_signal_connect (button, "clicked", G_CALLBACK (on_link_clicked), view);
	} else {
		button = gtk_check_button_new ();

		g_signal_connect (button, "toggled", G_CALLBACK (on_check_button_toggled), view);
	}
	gtk_widget_set_cursor_from_name (button, "pointer");
	return button;
}

static void
pps_overlay_form_field_text_save (PpsOverlayForm *view,
                                  GtkWidget *widget)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	PpsFormField *field;

	if (!document)
		return;

	field = priv->field;

	if (field->changed) {
		PpsFormFieldText *field_text = PPS_FORM_FIELD_TEXT (field);

		pps_document_forms_form_field_text_set_text (PPS_DOCUMENT_FORMS (document),
		                                             field, field_text->text);
		field->changed = FALSE;

		pps_pixbuf_cache_reload_page (priv->pixbuf_cache,
		                              NULL,
		                              priv->field->page->index,
		                              pps_document_model_get_rotation (priv->model),
		                              pps_document_model_get_scale (priv->model));
	}
}

static void
pps_overlay_form_field_text_changed (GObject *widget,
                                     PpsFormField *field)
{
	PpsFormFieldText *field_text = PPS_FORM_FIELD_TEXT (field);
	gchar *text = NULL;

	if (GTK_IS_ENTRY (widget)) {
		text = g_strdup (gtk_editable_get_text (GTK_EDITABLE (widget)));
	} else if (GTK_IS_TEXT_BUFFER (widget)) {
		GtkTextIter start, end;

		gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (widget), &start, &end);
		text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (widget),
		                                 &start, &end, FALSE);
	}

	if (!field_text->text ||
	    (field_text->text && g_ascii_strcasecmp (field_text->text, text) != 0)) {
		g_free (field_text->text);
		field_text->text = text;
		field->changed = TRUE;
	}
}

static void
pps_overlay_form_field_text_focus_out (GtkEventControllerFocus *self,
                                       PpsOverlayForm *view)
{
	GtkWidget *widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (self));
	pps_overlay_form_field_text_save (view, widget);
}

static void
pps_overlay_form_field_text_button_pressed (GtkGestureClick *self,
                                            gint n_press,
                                            gdouble x,
                                            gdouble y,
                                            gpointer user_data)
{
	gtk_gesture_set_state (GTK_GESTURE (self), GTK_EVENT_SEQUENCE_CLAIMED);
}

static GtkWidget *
pps_overlay_form_field_text_create_widget (PpsOverlayForm *view,
                                           PpsFormField *field)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (view);
	PpsFormFieldText *field_text = PPS_FORM_FIELD_TEXT (field);
	GtkWidget *text = NULL;
	GtkTextBuffer *buffer = NULL;
	gchar *txt;
	GtkEventController *controller;
#if HAVE_LIBSPELLING
	g_autoptr (SpellingTextBufferAdapter) adapter = NULL;
#endif

	txt = pps_document_forms_form_field_text_get_text (PPS_DOCUMENT_FORMS (pps_document_model_get_document (priv->model)),
	                                                   field);

	switch (field_text->type) {
	case PPS_FORM_FIELD_TEXT_FILE_SELECT:
		/* TODO */
		return NULL;
	case PPS_FORM_FIELD_TEXT_NORMAL:
		text = gtk_entry_new ();
		gtk_entry_set_has_frame (GTK_ENTRY (text), FALSE);
		/* Remove '.flat' style added by previous call
		 * gtk_entry_set_has_frame(FALSE) which caused bug #687 */
		gtk_widget_remove_css_class (text, "flat");
		gtk_entry_set_max_length (GTK_ENTRY (text), field_text->max_len);
		gtk_entry_set_visibility (GTK_ENTRY (text), !field_text->is_password);

		if (txt)
			gtk_editable_set_text (GTK_EDITABLE (text), txt);

		g_signal_connect_after (text, "activate",
		                        G_CALLBACK (pps_overlay_form_field_destroy),
		                        view);
		g_signal_connect (text, "changed",
		                  G_CALLBACK (pps_overlay_form_field_text_changed),
		                  field);
		break;
	case PPS_FORM_FIELD_TEXT_MULTILINE:
#if HAVE_LIBSPELLING
		if (priv->enable_spellchecking && field_text->do_spell_check) {
			text = gtk_source_view_new ();
			adapter = spelling_text_buffer_adapter_new (
			    GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text))),
			    spelling_checker_get_default ());

			gtk_text_view_set_extra_menu (GTK_TEXT_VIEW (text),
			                              spelling_text_buffer_adapter_get_menu_model (adapter));
			gtk_widget_insert_action_group (text, "spelling", G_ACTION_GROUP (adapter));
			spelling_text_buffer_adapter_set_enabled (adapter, TRUE);
		} else {
			text = gtk_text_view_new ();
		}
#else
		text = gtk_text_view_new ();
#endif
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text));

		if (txt) {
			gtk_text_buffer_set_text (buffer, txt, -1);
		}

		g_signal_connect (buffer, "changed",
		                  G_CALLBACK (pps_overlay_form_field_text_changed),
		                  field);

		break;
	default:
		g_assert_not_reached ();
	}

	g_clear_pointer (&txt, g_free);

	controller = GTK_EVENT_CONTROLLER (gtk_event_controller_focus_new ());
	g_signal_connect (controller, "leave",
	                  G_CALLBACK (pps_overlay_form_field_text_focus_out),
	                  view);
	gtk_widget_add_controller (text, controller);

	controller = GTK_EVENT_CONTROLLER (gtk_gesture_click_new ());
	g_signal_connect (controller, "pressed",
	                  G_CALLBACK (pps_overlay_form_field_text_button_pressed), NULL);
	gtk_widget_add_controller (text, controller);

	g_object_weak_ref (G_OBJECT (text),
	                   (GWeakNotify) pps_overlay_form_field_text_save,
	                   view);

	return text;
}

static void
pps_overlay_form_field_choice_save (PpsOverlayForm *view,
                                    GtkWidget *widget)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	PpsFormField *field;

	if (!document)
		return;

	field = priv->field;

	if (field->changed) {
		GList *l;
		PpsFormFieldChoice *field_choice = PPS_FORM_FIELD_CHOICE (field);

		if (field_choice->is_editable) {
			pps_document_forms_form_field_choice_set_text (PPS_DOCUMENT_FORMS (document),
			                                               field, field_choice->text);
		} else {
			pps_document_forms_form_field_choice_unselect_all (PPS_DOCUMENT_FORMS (document), field);
			for (l = field_choice->selected_items; l; l = g_list_next (l)) {
				pps_document_forms_form_field_choice_select_item (PPS_DOCUMENT_FORMS (document),
				                                                  field,
				                                                  GPOINTER_TO_INT (l->data));
			}
		}
		field->changed = FALSE;

		pps_pixbuf_cache_reload_page (priv->pixbuf_cache,
		                              NULL,
		                              priv->field->page->index,
		                              pps_document_model_get_rotation (priv->model),
		                              pps_document_model_get_scale (priv->model));
	}
}

static void
pps_overlay_form_field_choice_changed (GtkWidget *widget,
                                       PpsOverlayForm *form)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (form);
	PpsFormField *field = priv->field;
	PpsFormFieldChoice *field_choice = PPS_FORM_FIELD_CHOICE (field);

	if (GTK_IS_COMBO_BOX (widget)) {
		gint item;

		item = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
		if (item != -1 && (!field_choice->selected_items ||
		                   GPOINTER_TO_INT (field_choice->selected_items->data) != item)) {
			g_clear_pointer (&field_choice->selected_items, g_list_free);
			field_choice->selected_items = g_list_prepend (field_choice->selected_items,
			                                               GINT_TO_POINTER (item));
			field->changed = TRUE;
		}

		if (gtk_combo_box_get_has_entry (GTK_COMBO_BOX (widget))) {
			const gchar *text;

			text = gtk_editable_get_text (GTK_EDITABLE (gtk_combo_box_get_child (GTK_COMBO_BOX (widget))));
			if (!field_choice->text ||
			    (field_choice->text && g_ascii_strcasecmp (field_choice->text, text) != 0)) {
				g_free (field_choice->text);
				field_choice->text = g_strdup (text);
				field->changed = TRUE;
			}
		}
	} else if (GTK_IS_TREE_SELECTION (widget)) {
		GtkTreeSelection *selection = GTK_TREE_SELECTION (widget);
		GtkTreeModel *model;
		GList *items, *l;

		items = gtk_tree_selection_get_selected_rows (selection, &model);
		g_clear_pointer (&field_choice->selected_items, g_list_free);

		for (l = items; l && l->data; l = g_list_next (l)) {
			GtkTreeIter iter;
			GtkTreePath *path = (GtkTreePath *) l->data;
			gint item;

			gtk_tree_model_get_iter (model, &iter, path);
			gtk_tree_model_get (model, &iter, 1, &item, -1);

			field_choice->selected_items = g_list_prepend (field_choice->selected_items,
			                                               GINT_TO_POINTER (item));

			gtk_tree_path_free (path);
		}

		g_list_free (items);

		field->changed = TRUE;
	}
	pps_overlay_form_field_choice_save (form, widget);
}

typedef struct _PopupShownData {
	GtkWidget *choice;
	PpsFormField *field;
	PpsOverlayForm *view;
} PopupShownData;

static void
pps_overlay_form_field_choice_popup_shown_real (PopupShownData *data)
{
	pps_overlay_form_field_choice_changed (data->choice, data->view);
	pps_overlay_form_field_destroy (data->choice, data->view);

	g_object_unref (data->choice);
	g_object_unref (data->field);
	g_free (data);
}

static void
pps_overlay_form_field_choice_popup_shown_cb (GObject *self,
                                              GParamSpec *pspec,
                                              PpsOverlayForm *view)
{
	PpsFormField *field;
	GtkWidget *choice;
	gboolean shown;
	PopupShownData *data;

	g_object_get (self, "popup-shown", &shown, NULL);
	if (shown)
		return; /* popup is already opened */

	/* Popup has been closed */
	field = g_object_get_data (self, "form-field");
	choice = GTK_WIDGET (self);

	data = g_new0 (PopupShownData, 1);
	data->choice = g_object_ref (choice);
	data->field = g_object_ref (field);
	data->view = view;
	/* We need to use an idle here because combobox "active" item is not updated yet */
	g_idle_add_once ((GSourceOnceFunc) pps_overlay_form_field_choice_popup_shown_real,
	                 (gpointer) data);
}

static GtkWidget *
pps_overlay_form_field_choice_create_widget (PpsOverlayForm *view,
                                             PpsFormField *field)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (view);
	PpsDocument *document = pps_document_model_get_document (priv->model);
	PpsFormFieldChoice *field_choice = PPS_FORM_FIELD_CHOICE (field);
	GtkWidget *choice;
	GtkTreeModel *model;
	gint n_items, i;
	gint selected_item = -1;

	n_items = pps_document_forms_form_field_choice_get_n_items (PPS_DOCUMENT_FORMS (document),
	                                                            field);
	model = GTK_TREE_MODEL (gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT));
	for (i = 0; i < n_items; i++) {
		GtkTreeIter iter;
		gchar *item;

		item = pps_document_forms_form_field_choice_get_item (PPS_DOCUMENT_FORMS (document),
		                                                      field, i);
		if (pps_document_forms_form_field_choice_is_item_selected (
			PPS_DOCUMENT_FORMS (document), field, i)) {
			selected_item = i;
			/* FIXME: we need a get_selected_items function in poppler */
			field_choice->selected_items = g_list_prepend (field_choice->selected_items,
			                                               GINT_TO_POINTER (i));
		}

		if (item) {
			gtk_list_store_append (GTK_LIST_STORE (model), &iter);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			                    0, item,
			                    1, i,
			                    -1);
			g_free (item);
		}
	}

	if (field_choice->type == PPS_FORM_FIELD_CHOICE_LIST) {
		GtkCellRenderer *renderer;
		GtkWidget *tree_view;
		GtkTreeSelection *selection;

		tree_view = gtk_tree_view_new_with_model (model);
		gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree_view), FALSE);

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
		if (field_choice->multi_select) {
			gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
		}

		/* TODO: set selected items */

		renderer = gtk_cell_renderer_text_new ();
		gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tree_view),
		                                             0,
		                                             "choix", renderer,
		                                             "text", 0,
		                                             NULL);

		choice = gtk_scrolled_window_new ();
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (choice), tree_view);

		g_signal_connect (selection, "changed",
		                  G_CALLBACK (pps_overlay_form_field_choice_changed),
		                  view);
		g_signal_connect_after (selection, "changed",
		                        G_CALLBACK (pps_overlay_form_field_destroy),
		                        view);
	} else if (field_choice->is_editable) { /* ComboBoxEntry */
		GtkEntry *combo_entry;
		gchar *text;

		choice = gtk_combo_box_new_with_model_and_entry (model);
		combo_entry = GTK_ENTRY (gtk_combo_box_get_child (GTK_COMBO_BOX (choice)));
		/* This sets GtkEntry's minimum-width to be 1 char long, short enough
		 * to workaround gtk issue gtk#1422 . Papers issue #1002 */
		gtk_editable_set_width_chars (GTK_EDITABLE (combo_entry), 1);
		gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (choice), 0);

		text = pps_document_forms_form_field_choice_get_text (PPS_DOCUMENT_FORMS (document), field);
		if (text) {
			gtk_editable_set_text (GTK_EDITABLE (combo_entry), text);
			g_free (text);
		}

		g_signal_connect (choice, "changed",
		                  G_CALLBACK (pps_overlay_form_field_choice_changed),
		                  view);
		g_signal_connect_after (gtk_combo_box_get_child (GTK_COMBO_BOX (choice)),
		                        "activate",
		                        G_CALLBACK (pps_overlay_form_field_destroy),
		                        view);
	} else { /* ComboBoxText */
		GtkCellRenderer *renderer;

		choice = gtk_combo_box_new_with_model (model);
		renderer = gtk_cell_renderer_text_new ();
		gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (choice),
		                            renderer, TRUE);
		gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (choice),
		                                renderer,
		                                "text", 0,
		                                NULL);
		gtk_combo_box_set_active (GTK_COMBO_BOX (choice), selected_item);

		/* See issue #294 for why we use this instead of "changed" signal */
		g_signal_connect (choice, "notify::popup-shown",
		                  G_CALLBACK (pps_overlay_form_field_choice_popup_shown_cb),
		                  view);
	}

	g_object_unref (model);

	return choice;
}

G_GNUC_END_IGNORE_DEPRECATIONS

static void
pps_overlay_form_dispose (GObject *object)
{
	G_OBJECT_CLASS (pps_overlay_form_parent_class)->dispose (object);
}

static void
pps_overlay_form_constructed (GObject *obj)
{
	PpsOverlayForm *self = PPS_OVERLAY_FORM (obj);
	GtkWidget *widget = GTK_WIDGET (self);
	PpsOverlayFormPrivate *priv = GET_PRIVATE (self);
	gchar *alternate_name;
	PpsFormField *field = priv->field;
	GtkWidget *field_widget = NULL;

	G_OBJECT_CLASS (pps_overlay_form_parent_class)->constructed (obj);

	if (field->is_read_only)
		return;

	alternate_name = pps_form_field_get_alternate_name (priv->field);

	if (PPS_IS_FORM_FIELD_BUTTON (field)) {
		field_widget = pps_overlay_form_field_button_create_widget (self, field);
	} else if (PPS_IS_FORM_FIELD_TEXT (field)) {
		field_widget = pps_overlay_form_field_text_create_widget (self, field);
	} else if (PPS_IS_FORM_FIELD_CHOICE (field)) {
		field_widget = pps_overlay_form_field_choice_create_widget (self, field);
	} else if (PPS_IS_FORM_FIELD_SIGNATURE (field)) {
		/* TODO */
	}

	if (field_widget) {
		gtk_widget_add_css_class (field_widget, "view");

		g_object_set_data_full (G_OBJECT (field_widget), "form-field",
		                        g_object_ref (field),
		                        (GDestroyNotify) g_object_unref);

		gtk_widget_set_hexpand (field_widget, TRUE);
		gtk_accessible_update_property (GTK_ACCESSIBLE (field_widget), GTK_ACCESSIBLE_PROPERTY_LABEL, alternate_name, -1);
		gtk_box_append (GTK_BOX (self), field_widget);

		gtk_widget_set_visible (field_widget, TRUE);
	}

	gtk_widget_add_css_class (widget, "overlay-form");
	gtk_widget_set_tooltip_text (widget, alternate_name);
}

static void
pps_overlay_form_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_DOCUMENT_MODEL:
		priv->model = g_value_get_object (value);
		break;
	case PROP_PIXBUF_CACHE:
		priv->pixbuf_cache = g_value_get_object (value);
		break;
	case PROP_PAGE_CACHE:
		priv->page_cache = g_value_get_object (value);
		break;
	case PROP_FIELD:
		priv->field = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
pps_overlay_form_class_init (PpsOverlayFormClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = pps_overlay_form_dispose;
	object_class->constructed = pps_overlay_form_constructed;
	object_class->set_property = pps_overlay_form_set_property;

	props[PROP_DOCUMENT_MODEL] =
	    g_param_spec_object ("document-model",
	                         "DocumentModel",
	                         "The document model",
	                         PPS_TYPE_DOCUMENT_MODEL,
	                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	props[PROP_FIELD] =
	    g_param_spec_object ("field",
	                         "FormField",
	                         "the corresponding form field",
	                         PPS_TYPE_FORM_FIELD,
	                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	props[PROP_PAGE_CACHE] =
	    g_param_spec_object ("page-cache",
	                         "PageCache",
	                         "page cache",
	                         PPS_TYPE_PAGE_CACHE,
	                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	props[PROP_PIXBUF_CACHE] =
	    g_param_spec_object ("pixbuf-cache",
	                         "PixbufCache",
	                         "pixbuf cache",
	                         PPS_TYPE_PIXBUF_CACHE,
	                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, NUM_PROPERTIES, props);
}

static void
pps_overlay_form_init (PpsOverlayForm *self)
{
}

static PpsRectangle *
pps_overlay_form_get_area (PpsOverlay *overlay, gdouble *padding)
{
	PpsOverlayFormPrivate *priv = GET_PRIVATE (overlay);
	PpsMapping *mapping = pps_mapping_list_find (pps_page_cache_get_form_field_mapping (priv->page_cache, priv->field->page->index), priv->field);
	*padding = 0;
	PpsRectangle *rect = g_new (PpsRectangle, 1);
	*rect = mapping->area;
	return rect;
}

void
pps_overlay_form_iface_init (PpsOverlayInterface *iface)
{
	iface->get_area = pps_overlay_form_get_area;
}

GtkWidget *
pps_overlay_form_new (PpsFormField *field,
                      PpsDocumentModel *model,
                      PpsPageCache *page_cache,
                      PpsPixbufCache *pixbuf_cache)
{
	GtkWidget *widget = GTK_WIDGET (g_object_new (PPS_TYPE_OVERLAY_FORM,
	                                              "field", field,
	                                              "document-model", model,
	                                              "page-cache", page_cache,
	                                              "pixbuf-cache", pixbuf_cache, NULL));
	return widget;
}
