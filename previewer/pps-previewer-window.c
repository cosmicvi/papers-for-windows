// SPDX-FileCopyrightText: 2009 Carlos Garcia Campos <carlosgc@gnome.org>
// SPDX-FileCopyrightText: 2018 Germán Poo-Caamaño <gpoo@gnome.org>
// SPDX-FileCopyrightText: 2018, 2021 Christian Persch
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include <config.h>

#include <fcntl.h>

#if GTKUNIXPRINT_ENABLED
#include <gtk/gtkunixprint.h>
#endif
#include "pps-page-selector.h"
#include <adwaita.h>
#include <glib/gi18n.h>
#include <papers-view.h>

#include "pps-previewer-window.h"

typedef struct _PpsPreviewerWindowPrivate {
	PpsJob *job;
	PpsDocumentModel *model;
	PpsDocument *document;

	PpsView *view;
	GtkWidget *page_selector;
	AdwAlertDialog *dialog;
	GSettings *settings;

	/* Printing */
	GtkPrintSettings *print_settings;
	GtkPageSetup *print_page_setup;
#if GTKUNIXPRINT_ENABLED
	GtkPrinter *printer;
#endif
	gchar *print_job_title;
	gchar *source_file;
	int source_fd;
} PpsPreviewerWindowPrivate;

#define MIN_SCALE 0.05409
#define MAX_SCALE 4.0

G_DEFINE_TYPE_WITH_PRIVATE (PpsPreviewerWindow, pps_previewer_window, ADW_TYPE_APPLICATION_WINDOW)

#define GET_PRIVATE(o) pps_previewer_window_get_instance_private (o)

static void
pps_previewer_window_error_dialog_run (PpsPreviewerWindow *window,
                                       GError *error)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	if (error)
		adw_alert_dialog_set_body (priv->dialog, error->message);

	adw_dialog_present (ADW_DIALOG (priv->dialog), GTK_WIDGET (window));
}

static void
pps_previewer_window_close (GSimpleAction *action,
                            GVariant *parameter,
                            gpointer user_data)
{
	gtk_window_destroy (GTK_WINDOW (user_data));
}

static void
pps_previewer_window_previous_page (GSimpleAction *action,
                                    GVariant *parameter,
                                    gpointer user_data)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (user_data);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	pps_view_previous_page (priv->view);
}

static void
pps_previewer_window_next_page (GSimpleAction *action,
                                GVariant *parameter,
                                gpointer user_data)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (user_data);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	pps_view_next_page (priv->view);
}

static void
pps_previewer_window_zoom_in (GSimpleAction *action,
                              GVariant *parameter,
                              gpointer user_data)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (user_data);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FREE);
	pps_view_zoom_in (priv->view);
}

static void
pps_previewer_window_zoom_out (GSimpleAction *action,
                               GVariant *parameter,
                               gpointer user_data)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (user_data);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_FREE);
	pps_view_zoom_out (priv->view);
}

static void
pps_previewer_window_zoom_default (GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer user_data)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (user_data);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	pps_document_model_set_sizing_mode (priv->model,
	                                    PPS_SIZING_AUTOMATIC);
}

static void
pps_previewer_window_action_page_activated (GObject *object,
                                            PpsLink *link,
                                            PpsPreviewerWindow *window)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	pps_view_handle_link (priv->view, link);
	gtk_widget_grab_focus (GTK_WIDGET (priv->view));
}

static void
pps_previewer_window_focus_page_selector (GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer user_data)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (user_data);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	gtk_widget_grab_focus (priv->page_selector);
}

#if GTKUNIXPRINT_ENABLED

static void
pps_previewer_window_print_finished (GtkPrintJob *print_job,
                                     PpsPreviewerWindow *window,
                                     GError *error)
{
	if (error) {
		pps_previewer_window_error_dialog_run (window, error);
	}

	g_object_unref (print_job);
	gtk_window_destroy (GTK_WINDOW (window));
}

static void
pps_previewer_window_do_print (PpsPreviewerWindow *window)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);
	GtkPrintJob *job;
	gboolean rv = FALSE;
	GError *error = NULL;

	job = gtk_print_job_new (priv->print_job_title ? priv->print_job_title : (priv->source_file ? priv->source_file : _ ("Papers Document Viewer")),
	                         priv->printer,
	                         priv->print_settings,
	                         priv->print_page_setup);

	if (priv->source_fd != -1)
		rv = gtk_print_job_set_source_fd (job, priv->source_fd, &error);
	else if (priv->source_file != NULL)
		rv = gtk_print_job_set_source_file (job, priv->source_file, &error);
	else
		g_set_error_literal (&error, GTK_PRINT_ERROR, GTK_PRINT_ERROR_GENERAL,
		                     "Neither file nor FD to print.");

	if (rv) {
		gtk_print_job_send (job,
		                    (GtkPrintJobCompleteFunc) pps_previewer_window_print_finished,
		                    window, NULL);
	} else {
		pps_previewer_window_error_dialog_run (window, error);
		g_error_free (error);
	}

	gtk_widget_set_visible (GTK_WIDGET (window), FALSE);
}

static void
pps_previewer_window_enumerate_finished (PpsPreviewerWindow *window)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	if (priv->printer) {
		pps_previewer_window_do_print (window);
	} else {
		GError *error = NULL;

		g_set_error (&error,
		             GTK_PRINT_ERROR,
		             GTK_PRINT_ERROR_GENERAL,
		             _ ("The selected printer “%s” could not be found"),
		             gtk_print_settings_get_printer (priv->print_settings));

		pps_previewer_window_error_dialog_run (window, error);
		g_error_free (error);
	}
}

static gboolean
pps_previewer_window_enumerate_printers (GtkPrinter *printer,
                                         PpsPreviewerWindow *window)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	const gchar *printer_name;

	printer_name = gtk_print_settings_get_printer (priv->print_settings);
	if ((printer_name && strcmp (printer_name, gtk_printer_get_name (printer)) == 0) ||
	    (!printer_name && gtk_printer_is_default (printer))) {
		g_set_object (&priv->printer, printer);

		return TRUE; /* we're done */
	}

	return FALSE; /* continue the enumeration */
}

static void
pps_previewer_window_print (GSimpleAction *action,
                            GVariant *parameter,
                            gpointer user_data)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (user_data);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	if (!priv->print_settings)
		priv->print_settings = gtk_print_settings_new ();
	if (!priv->print_page_setup)
		priv->print_page_setup = gtk_page_setup_new ();
	gtk_enumerate_printers ((GtkPrinterFunc) pps_previewer_window_enumerate_printers,
	                        window,
	                        (GDestroyNotify) pps_previewer_window_enumerate_finished,
	                        FALSE);
}

#endif /* GTKUNIXPRINT_ENABLED */

static const GActionEntry actions[] = {
#if GTKUNIXPRINT_ENABLED
	{ "print", pps_previewer_window_print },
#endif
	{ "go-previous-page", pps_previewer_window_previous_page },
	{ "go-next-page", pps_previewer_window_next_page },
	{ "select-page", pps_previewer_window_focus_page_selector },
	{ "zoom-in", pps_previewer_window_zoom_in },
	{ "zoom-out", pps_previewer_window_zoom_out },
	{ "close", pps_previewer_window_close },
	{ "zoom-default", pps_previewer_window_zoom_default },
};

static void
pps_previewer_window_set_action_enabled (PpsPreviewerWindow *window,
                                         const char *name,
                                         gboolean enabled)
{
	GAction *action;

	action = g_action_map_lookup_action (G_ACTION_MAP (window), name);
	g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
model_page_changed (PpsDocumentModel *model,
                    gint old_page,
                    gint new_page,
                    PpsPreviewerWindow *window)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);
	gint n_pages = pps_document_get_n_pages (pps_document_model_get_document (priv->model));

	pps_previewer_window_set_action_enabled (window,
	                                         "go-previous-page",
	                                         new_page > 0);
	pps_previewer_window_set_action_enabled (window,
	                                         "go-next-page",
	                                         new_page < n_pages - 1);
}

static void
view_sizing_mode_changed (PpsDocumentModel *model,
                          GParamSpec *pspec,
                          PpsPreviewerWindow *window)
{
	PpsSizingMode sizing_mode = pps_document_model_get_sizing_mode (model);

	pps_previewer_window_set_action_enabled (window,
	                                         "zoom-default",
	                                         sizing_mode != PPS_SIZING_AUTOMATIC);
}

static void
load_job_finished_cb (PpsJob *job,
                      PpsPreviewerWindow *window)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);
	g_assert (job == priv->job);
	g_autoptr (GError) error = NULL;

	if (!pps_job_is_succeeded (job, &error)) {
		pps_previewer_window_error_dialog_run (window, error);
		g_clear_object (&priv->job);
		return;
	}

	priv->document = pps_job_load_get_loaded_document (PPS_JOB_LOAD (job));

	g_clear_object (&priv->job);

	pps_document_model_set_document (priv->model, priv->document);
	g_signal_connect (priv->model, "notify::sizing-mode",
	                  G_CALLBACK (view_sizing_mode_changed),
	                  window);
}

static void
pps_previewer_window_dispose (GObject *object)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (object);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	g_clear_object (&priv->job);
	g_clear_object (&priv->document);
	g_clear_object (&priv->print_settings);
	g_clear_object (&priv->print_page_setup);
	g_clear_object (&priv->settings);
#if GTKUNIXPRINT_ENABLED
	g_clear_object (&priv->printer);
#endif
	g_clear_pointer (&priv->print_job_title, g_free);
	g_clear_pointer (&priv->source_file, g_free);

	if (priv->source_fd != -1) {
		close (priv->source_fd);
		priv->source_fd = -1;
	}

	G_OBJECT_CLASS (pps_previewer_window_parent_class)->dispose (object);
}

static void
pps_previewer_window_init (PpsPreviewerWindow *window)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	priv->source_fd = -1;

	gtk_widget_init_template (GTK_WIDGET (window));

	g_action_map_add_action_entries (G_ACTION_MAP (window),
	                                 actions, G_N_ELEMENTS (actions),
	                                 window);
}

static void
pps_previewer_window_constructed (GObject *object)
{
	PpsPreviewerWindow *window = PPS_PREVIEWER_WINDOW (object);
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);
	gdouble dpi;

	G_OBJECT_CLASS (pps_previewer_window_parent_class)->constructed (object);

	pps_view_set_model (priv->view, priv->model);
	pps_page_selector_set_model (PPS_PAGE_SELECTOR (priv->page_selector),
	                             priv->model);

	dpi = pps_document_misc_get_widget_dpi (GTK_WIDGET (window));
	pps_document_model_set_min_scale (priv->model, MIN_SCALE * dpi / 72.0);
	pps_document_model_set_max_scale (priv->model, MAX_SCALE * dpi / 72.0);
	pps_document_model_set_sizing_mode (priv->model, PPS_SIZING_AUTOMATIC);

	view_sizing_mode_changed (priv->model, NULL, window);
	priv->settings = g_settings_new ("org.gnome.Papers.Previewer");

	g_settings_bind (priv->settings,
	                 "window-width",
	                 window, "default-width",
	                 G_SETTINGS_BIND_GET | G_SETTINGS_BIND_GET_NO_CHANGES | G_SETTINGS_BIND_SET);

	g_settings_bind (priv->settings,
	                 "window-height",
	                 window, "default-height",
	                 G_SETTINGS_BIND_GET | G_SETTINGS_BIND_GET_NO_CHANGES | G_SETTINGS_BIND_SET);

	g_settings_bind (priv->settings,
	                 "window-maximized",
	                 window, "maximized",
	                 G_SETTINGS_BIND_GET | G_SETTINGS_BIND_GET_NO_CHANGES | G_SETTINGS_BIND_SET);

	g_signal_connect (priv->page_selector,
	                  "activate-link",
	                  G_CALLBACK (pps_previewer_window_action_page_activated),
	                  window);

	g_signal_connect_object (priv->model, "page-changed",
	                         G_CALLBACK (model_page_changed),
	                         window, 0);
}

static void
pps_previewer_window_class_init (PpsPreviewerWindowClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gobject_class->constructed = pps_previewer_window_constructed;
	gobject_class->dispose = pps_previewer_window_dispose;

	g_type_ensure (PPS_TYPE_PAGE_SELECTOR);
	gtk_widget_class_set_template_from_resource (widget_class,
	                                             "/org/gnome/papers/previewer/ui/previewer-window.ui");

	gtk_widget_class_bind_template_child_private (widget_class, PpsPreviewerWindow, view);
	gtk_widget_class_bind_template_child_private (widget_class, PpsPreviewerWindow, page_selector);
	gtk_widget_class_bind_template_child_private (widget_class, PpsPreviewerWindow, model);
	gtk_widget_class_bind_template_child_private (widget_class, PpsPreviewerWindow, dialog);
}

/* Public methods */
PpsPreviewerWindow *
pps_previewer_window_new (void)
{
	return g_object_new (PPS_TYPE_PREVIEWER_WINDOW,
	                     "application", g_application_get_default (),
	                     NULL);
}

void
pps_previewer_window_set_job (PpsPreviewerWindow *window,
                              PpsJob *job)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	g_return_if_fail (PPS_IS_PREVIEWER_WINDOW (window));
	g_return_if_fail (PPS_IS_JOB (job));

	g_set_object (&priv->job, job);

	g_signal_connect_object (priv->job, "finished",
	                         G_CALLBACK (load_job_finished_cb),
	                         window, 0);
	pps_job_scheduler_push_job (priv->job, PPS_JOB_PRIORITY_NONE);
}

static gboolean
pps_previewer_window_set_print_settings_take_file (PpsPreviewerWindow *window,
                                                   GMappedFile *file,
                                                   GError **error)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);
	GBytes *bytes;
	GKeyFile *key_file;
	GtkPrintSettings *psettings;
	GtkPageSetup *psetup;
	char *job_name;
	gboolean rv;

	g_clear_object (&priv->print_settings);
	g_clear_object (&priv->print_page_setup);
	g_clear_pointer (&priv->print_job_title, g_free);

	bytes = g_mapped_file_get_bytes (file);
	key_file = g_key_file_new ();
	rv = g_key_file_load_from_bytes (key_file, bytes, G_KEY_FILE_NONE, error);
	g_bytes_unref (bytes);
	g_mapped_file_unref (file);

	if (!rv) {
		priv->print_settings = gtk_print_settings_new ();
		priv->print_page_setup = gtk_page_setup_new ();
		priv->print_job_title = g_strdup (_ ("Papers"));
		return FALSE;
	}

	psettings = gtk_print_settings_new_from_key_file (key_file,
	                                                  "Print Settings",
	                                                  NULL);
	priv->print_settings = psettings ? psettings : gtk_print_settings_new ();

	psetup = gtk_page_setup_new_from_key_file (key_file,
	                                           "Page Setup",
	                                           NULL);
	priv->print_page_setup = psetup ? psetup : gtk_page_setup_new ();

	job_name = g_key_file_get_string (key_file,
	                                  "Print Job", "title",
	                                  NULL);
	if (job_name && job_name[0]) {
		priv->print_job_title = job_name;
		gtk_window_set_title (GTK_WINDOW (window), job_name);
	} else {
		g_free (job_name);
		priv->print_job_title = g_strdup (_ ("Papers Document Viewer"));
	}

	g_key_file_free (key_file);

	return TRUE;
}

gboolean
pps_previewer_window_set_print_settings (PpsPreviewerWindow *window,
                                         const gchar *print_settings,
                                         GError **error)
{
	GMappedFile *file;

	g_return_val_if_fail (PPS_IS_PREVIEWER_WINDOW (window), FALSE);
	g_return_val_if_fail (print_settings != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	file = g_mapped_file_new (print_settings, FALSE, error);
	if (file == NULL)
		return FALSE;

	return pps_previewer_window_set_print_settings_take_file (window, file, error);
}

/**
 * pps_previewer_window_set_print_settings_fd:
 * @window:
 * @fd:
 * @error:
 *
 * Sets the print settings from FD.
 *
 * Note that this function takes ownership of @fd.
 */
gboolean
pps_previewer_window_set_print_settings_fd (PpsPreviewerWindow *window,
                                            int fd,
                                            GError **error)
{
	GMappedFile *file;

	g_return_val_if_fail (PPS_IS_PREVIEWER_WINDOW (window), FALSE);
	g_return_val_if_fail (fd != -1, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	file = g_mapped_file_new_from_fd (fd, FALSE, error);
	if (file == NULL)
		return FALSE;

	return pps_previewer_window_set_print_settings_take_file (window, file, error);
}

void
pps_previewer_window_set_source_file (PpsPreviewerWindow *window,
                                      const gchar *source_file)
{
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	g_return_if_fail (PPS_IS_PREVIEWER_WINDOW (window));

	g_free (priv->source_file);
	priv->source_file = g_strdup (source_file);
}

/**
 * pps_previewer_window_set_source_fd:
 * @window:
 * @fd:
 *
 * Sets the source document FD.
 *
 * Note that this function takes ownership of @fd.
 *
 * Returns: %TRUE on success
 */
gboolean
pps_previewer_window_set_source_fd (PpsPreviewerWindow *window,
                                    int fd,
                                    GError **error)
{
#ifdef G_OS_WIN32
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	                     "File descriptor support is not available on Windows");
	return FALSE;
#else
	PpsPreviewerWindowPrivate *priv = GET_PRIVATE (window);

	int nfd;

	g_return_val_if_fail (PPS_IS_PREVIEWER_WINDOW (window), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	nfd = fcntl (fd, F_DUPFD_CLOEXEC, 3);
	if (nfd == -1) {
		int errsv = errno;
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errsv),
		             "Failed to duplicate file descriptor: %s",
		             g_strerror (errsv));
		return FALSE;
	}

	if (priv->source_fd != -1)
		close (priv->source_fd);

	priv->source_fd = fd;

	return TRUE;
#endif
}
