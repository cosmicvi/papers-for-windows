// SPDX-FileCopyrightText: 2009 Carlos Garcia Campos <carlosgc@gnome.org>
// SPDX-FileCopyrightText: 2012, 2018, 2021, 2022 Christian Persch
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <papers-document.h>
#include <papers-view.h>

#include "pps-previewer-window.h"

static gboolean unlink_temp_file = FALSE;
static int input_fd = -1;
static char *input_file = NULL;
static char *input_mime_type = NULL;
static int print_settings_fd = -1;
static gchar *print_settings_file = NULL;
static PpsPreviewerWindow *window = NULL;

static const GOptionEntry goption_options[] = {
	{ "unlink-tempfile", 'u', 0, G_OPTION_ARG_NONE, &unlink_temp_file,
	  N_ ("Delete the temporary file"), NULL },
	{ "print-settings", 'p', 0, G_OPTION_ARG_FILENAME, &print_settings_file,
	  N_ ("File specifying print settings"), N_ ("FILE") },
	{ "fd", 0, 0, G_OPTION_ARG_INT, &input_fd,
	  N_ ("File descriptor of input file"), N_ ("FD") },
	{ "mime-type", 0, 0, G_OPTION_ARG_STRING, &input_mime_type,
	  N_ ("MIME type of input file"), N_ ("TYPE") },
	{ "print-settings-fd", 0, 0, G_OPTION_ARG_INT, &print_settings_fd,
	  N_ ("File descriptor of print settings file"), N_ ("FD") },
	{ NULL }
};

static void
pps_previewer_unlink_tempfile (const gchar *filename)
{
	GFile *file, *tempdir;

	file = g_file_new_for_path (filename);
	tempdir = g_file_new_for_path (g_get_tmp_dir ());

	if (g_file_has_prefix (file, tempdir)) {
		g_file_delete (file, NULL, NULL);
	}

	g_object_unref (file);
	g_object_unref (tempdir);
}

static void
activate_cb (GApplication *application,
             gpointer user_data)
{
	if (window) {
		gtk_window_present (GTK_WINDOW (window));
	}
}

static void
startup_cb (GApplication *application,
            gpointer data)
{
	PpsJobLoad *job;
	GError *error = NULL;
	gboolean ps_ok = TRUE;

	g_assert (input_fd != -1 || input_file != NULL);

	window = pps_previewer_window_new ();

	if (print_settings_fd != -1) {
		ps_ok = pps_previewer_window_set_print_settings_fd (PPS_PREVIEWER_WINDOW (window), print_settings_fd, &error);
		print_settings_fd = -1;
	} else if (print_settings_file != NULL) {
		ps_ok = pps_previewer_window_set_print_settings (PPS_PREVIEWER_WINDOW (window), print_settings_file, &error);
		g_clear_pointer (&print_settings_file, g_free);
	}
	if (!ps_ok) {
		g_printerr ("Failed to load print settings: %s\n", error->message);
		g_clear_error (&error);
	}

	job = PPS_JOB_LOAD (pps_job_load_new ());
	if (input_fd != -1) {
		if (!pps_previewer_window_set_source_fd (PPS_PREVIEWER_WINDOW (window), input_fd, &error)) {
			g_printerr ("Failed to set source FD: %s\n", error->message);
			g_clear_error (&error);

			g_application_quit (application);
			return;
		}

		pps_job_load_take_fd (job, input_fd, input_mime_type);

		input_fd = -1;
	} else {
		GFile *file;
		char *uri;
		char *path;

		file = g_file_new_for_commandline_arg (input_file);
		uri = g_file_get_uri (file);
		path = g_file_get_path (file);

		pps_previewer_window_set_source_file (PPS_PREVIEWER_WINDOW (window), path);
		pps_job_load_set_uri (job, uri);

		g_free (uri);
		g_free (path);
		g_object_unref (file);

		g_clear_pointer (&input_file, g_free);
	}

	pps_previewer_window_set_job (window, PPS_JOB (job));
	g_object_unref (job);

	/* Window will be presented by 'activate' signal */
}

static gboolean
check_arguments (int argc,
                 char **argv,
                 GError **error)
{
#ifdef G_OS_WIN32
	if (input_fd != -1) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "File descriptor support is not available on Windows");
		return FALSE;
	}
#else
	if (input_fd != -1) {
		struct stat statbuf;
		int flags;

		if (fstat (input_fd, &statbuf) == -1 ||
		    (flags = fcntl (input_fd, F_GETFL, &flags)) == -1) {
			int errsv = errno;
			g_set_error_literal (error, G_FILE_ERROR,
			                     g_file_error_from_errno (errsv),
			                     g_strerror (errsv));
			return FALSE;
		}

		if (!S_ISREG (statbuf.st_mode)) {
			g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_BADF,
			                     "Not a regular file.");
			return FALSE;
		}

		switch (flags & O_ACCMODE) {
		case O_RDONLY:
		case O_RDWR:
			break;
		case O_WRONLY:
		default:
			g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_BADF,
			                     "Not a readable file descriptor.");
			return FALSE;
		}

		if (argc > 1) {
			g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
			                     "Too many arguments");
			return FALSE;
		}
		if (input_mime_type == NULL) {
			input_mime_type = pps_file_get_mime_type_from_fd (input_fd, error);
			if (input_mime_type == NULL) {
				g_prefix_error (error, "Must specify --mime-type: ");
				return FALSE;
			}
		}
		if (unlink_temp_file) {
			g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
			                     "Must not specify --unlink-tempfile");
			return FALSE;
		}
	} else
#endif
	{
		char *path;

		if (argc != 2) {
			g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
			                     "Need exactly one argument");
			return FALSE;
		}
		if (input_mime_type != NULL) {
			g_set_error_literal (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
			                     "Must not specify --mime-type");
			return FALSE;
		}

		path = g_filename_from_uri (argv[1], NULL, NULL);
		if (!g_file_test (argv[1], G_FILE_TEST_IS_REGULAR) && !g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
			g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
			             "File \"%s\" does not exist or is not a regular file\n", argv[1]);
			g_free (path);
			return FALSE;
		}
		g_free (path);

		input_file = g_strdup (argv[1]);
	}

	return TRUE;
}

gint
main (gint argc, gchar **argv)
{
	AdwApplication *application;
	GOptionContext *context;
	GError *error = NULL;
	int status = 1;

	const gchar *action_accels[] = {
		"win.select-page",
		"<Ctrl>L",
		NULL,
		"win.go-previous-page",
		"p",
		"<Ctrl>Page_Up",
		NULL,
		"win.go-next-page",
		"n",
		"<Ctrl>Page_Down",
		NULL,
		"win.print",
		"<Ctrl>P",
		NULL,
		"win.zoom-in",
		"plus",
		"<Ctrl>plus",
		"KP_Add",
		"<Ctrl>KP_Add",
		"equal",
		"<Ctrl>equal",
		NULL,
		"win.zoom-out",
		"minus",
		"<Ctrl>minus",
		"KP_Subtract",
		"<Ctrl>KP_Subtract",
		NULL,
		"win.zoom-default",
		"a",
		NULL,
		NULL,
	};
	const char **it;

	/* Initialize the i18n stuff */
	bindtextdomain (GETTEXT_PACKAGE, PPS_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_set_prgname ("papers-previewer");

	context = g_option_context_new (_ ("GNOME Document Previewer"));
	g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, goption_options, GETTEXT_PACKAGE);

	if (!g_option_context_parse (context, &argc, &argv, &error) ||
	    !check_arguments (argc, argv, &error)) {
		g_printerr ("Error parsing command line arguments: %s\n", error->message);
		g_error_free (error);
		g_option_context_free (context);
		return 1;
	}
	g_option_context_free (context);

	if (!pps_init ())
		return 1;

	g_set_application_name (_ ("GNOME Document Previewer"));
	gtk_window_set_default_icon_name (APP_ID);

	application = adw_application_new (APP_ID "-previewer", G_APPLICATION_NON_UNIQUE);
	g_application_set_resource_base_path (G_APPLICATION (application),
	                                      "/org/gnome/papers/previewer");
	g_signal_connect (application, "startup", G_CALLBACK (startup_cb), NULL);
	g_signal_connect (application, "activate", G_CALLBACK (activate_cb), NULL);

	for (it = action_accels; it[0]; it += g_strv_length ((gchar **) it) + 1)
		gtk_application_set_accels_for_action (GTK_APPLICATION (application), it[0], &it[1]);

	status = g_application_run (G_APPLICATION (application), 0, NULL);

	if (unlink_temp_file)
		pps_previewer_unlink_tempfile (argv[1]);
	if (print_settings_file)
		pps_previewer_unlink_tempfile (print_settings_file);

	pps_job_scheduler_wait ();
	pps_shutdown ();

	return status;
}
