// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2005, Red Hat, Inc.
 *  Copyright © 2018 Christian Persch
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "pps-backend-info.h"
#include "pps-document-factory.h"
#include "pps-file-helpers.h"

/* Backends manager */

#define BACKEND_DATA_KEY "pps-backend-info"

static GList *pps_backends_list = NULL;
static GHashTable *pps_module_hash = NULL;
static gchar *pps_backends_dir = NULL;

static PpsDocument *pps_document_factory_new_document_for_mime_type (const char *mime_type,
                                                                     GError **error);

static PpsBackendInfo *
get_backend_info_for_mime_type (const gchar *mime_type)
{
	GList *l;
	g_autofree gchar *content_type = g_content_type_from_mime_type (mime_type);

	for (l = pps_backends_list; l; l = l->next) {
		PpsBackendInfo *info = (PpsBackendInfo *) l->data;
		char **mime_types = info->mime_types;
		guint i;

		for (i = 0; mime_types[i] != NULL; ++i)
			if (g_content_type_is_mime_type (content_type, mime_types[i]))
				return info;
	}

	return NULL;
}

static PpsBackendInfo *
get_backend_info_for_document (PpsDocument *document)
{
	PpsBackendInfo *info;

	info = g_object_get_data (G_OBJECT (document), BACKEND_DATA_KEY);

	g_warn_if_fail (info != NULL);
	return info;
}

static PpsDocument *
pps_document_factory_new_document_for_mime_type (const gchar *mime_type,
                                                 GError **error)
{
	PpsDocument *document;
	PpsBackendInfo *info;
	GModule *module = NULL;
	GType backend_type;
	GType (*query_type_function) (void) = NULL;

	g_return_val_if_fail (mime_type != NULL, NULL);

	info = get_backend_info_for_mime_type (mime_type);
	if (info == NULL) {
		char *content_type, *mime_desc = NULL;

		content_type = g_content_type_from_mime_type (mime_type);
		if (content_type)
			mime_desc = g_content_type_get_description (content_type);

		g_set_error (error,
		             PPS_DOCUMENT_ERROR,
		             PPS_DOCUMENT_ERROR_INVALID,
		             _ ("File type %s (%s) is not supported"),
		             mime_desc ? mime_desc : "(unknown)", mime_type);
		g_free (mime_desc);
		g_free (content_type);

		return NULL;
	}

	if (pps_module_hash != NULL) {
		module = g_hash_table_lookup (pps_module_hash, info->module_name);
	}
	if (module == NULL) {
		g_autofree gchar *path = NULL;

		path = g_strconcat (pps_backends_dir, G_DIR_SEPARATOR_S,
		                    info->module_name, NULL);

		module = g_module_open (path, 0);

		if (!g_module_symbol (module, "pps_backend_query_type",
		                      (void *) &query_type_function)) {
			const char *err = g_module_error ();
			g_set_error (error, PPS_DOCUMENT_ERROR, PPS_DOCUMENT_ERROR_INVALID,
			             "Failed to load backend for '%s': %s",
			             mime_type, err ? err : "unknown error");
			g_module_close (module);
			return NULL;
		}

		/* Make the module resident so it can’t be unloaded: without using a
		 * full #GTypePlugin implementation for the modules, it’s not safe to
		 * re-load a module and re-register its types with GObject, as that will
		 * confuse the GType system. */
		g_module_make_resident (module);

		if (pps_module_hash == NULL) {
			pps_module_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
			                                         g_free,
			                                         NULL /* leaked on purpose */);
		}
		g_hash_table_insert (pps_module_hash, g_strdup (info->module_name), module);
	}

	if (!query_type_function && !g_module_symbol (module, "pps_backend_query_type",
	                                              (void *) &query_type_function)) {
		const char *err = g_module_error ();
		g_set_error (error, PPS_DOCUMENT_ERROR, PPS_DOCUMENT_ERROR_INVALID,
		             "Failed to load backend for '%s': %s",
		             mime_type, err ? err : "unknown error");
		return NULL;
	}

	backend_type = query_type_function ();
	g_assert (g_type_is_a (backend_type, PPS_TYPE_DOCUMENT));

	document = g_object_new (backend_type, NULL);

	g_object_set_data (G_OBJECT (document), BACKEND_DATA_KEY, info);

	return document;
}

static PpsCompressionType
get_compression_from_mime_type (const gchar *mime_type)
{
	gchar type[3];
	gchar *p;

	if (!(p = g_strrstr (mime_type, "/")))
		return PPS_COMPRESSION_NONE;

	if (sscanf (++p, "x-%2s%*s", type) == 1) {
		if (g_ascii_strcasecmp (type, "gz") == 0)
			return PPS_COMPRESSION_GZIP;
		else if (g_ascii_strcasecmp (type, "bz") == 0)
			return PPS_COMPRESSION_BZIP2;
		else if (g_ascii_strcasecmp (type, "xz") == 0)
			return PPS_COMPRESSION_LZMA;
	}

	return PPS_COMPRESSION_NONE;
}

/*
 * new_document_for_uri:
 * @uri: the document URI
 * @fast: whether to use fast MIME type detection
 * @compression: a location to store the document's compression type
 * @error: a #GError location to store an error, or %NULL
 *
 * Creates a #PpsDocument instance for the document at @uri, using either
 * fast or slow MIME type detection. If a document could be created,
 * @compression is filled in with the document's compression type.
 * On error, %NULL is returned and @error filled in.
 *
 * Returns: a new #PpsDocument instance, or %NULL on error with @error filled in
 */
static PpsDocument *
new_document_for_uri (const char *uri,
                      gboolean fast,
                      PpsCompressionType *compression,
                      GError **error)
{
	PpsDocument *document = NULL;
	g_autofree gchar *mime_type;

	*compression = PPS_COMPRESSION_NONE;

	mime_type = pps_file_get_mime_type (uri, fast, error);
	if (mime_type == NULL)
		return NULL;

	document = pps_document_factory_new_document_for_mime_type (mime_type, error);
	if (document == NULL)
		return NULL;

	*compression = get_compression_from_mime_type (mime_type);

	return document;
}

static void
free_uncompressed_uri (gchar *uri_unc)
{
	if (!uri_unc)
		return;

	pps_tmp_uri_unlink (uri_unc);
	g_free (uri_unc);
}

/*
 * _pps_document_factory_init:
 *
 * Initializes the papers document factory.
 *
 * Returns: %TRUE if there were any backends found; %FALSE otherwise
 */
gboolean
_pps_document_factory_init (void)
{
	if (pps_backends_list)
		return TRUE;

	if (g_getenv ("PPS_BACKENDS_DIR") != NULL)
		pps_backends_dir = g_strdup (g_getenv ("PPS_BACKENDS_DIR"));

#ifdef G_OS_WIN32
	if (!pps_backends_dir) {
		g_autofree gchar *inst_dir = g_win32_get_package_installation_directory_of_module (NULL);
		if (inst_dir) {
			pps_backends_dir = g_build_filename (inst_dir, "lib", "papers", "6", "backends", NULL);
		}
	}
#endif

	if (!pps_backends_dir) {
		pps_backends_dir = g_strdup (PPS_BACKENDSDIR);
	}

	pps_backends_list = _pps_backend_info_load_from_dir (pps_backends_dir);

	return pps_backends_list != NULL;
}

/*
 * _pps_document_factory_shutdown:
 *
 * Shuts the papers document factory down.
 */
void
_pps_document_factory_shutdown (void)
{
	g_clear_list (&pps_backends_list, (GDestroyNotify) _pps_backend_info_free);

	g_clear_pointer (&pps_module_hash, g_hash_table_unref);
	g_clear_pointer (&pps_backends_dir, g_free);
}

/**
 * pps_document_factory_get_document:
 * @uri: an URI
 * @error: a #GError location to store an error, or %NULL
 *
 * Creates a #PpsDocument for the document at @uri; or, if no backend handling
 * the document's type is found, or an error occurred on opening the document,
 * returns %NULL and fills in @error.
 * If the document is encrypted, it is returned but also @error is set to
 * %PPS_DOCUMENT_ERROR_ENCRYPTED.
 *
 * Returns: (transfer full): a new #PpsDocument, or %NULL
 */
PpsDocument *
pps_document_factory_get_document (const char *uri,
                                   GError **error)
{
	g_autoptr (PpsDocument) document = NULL;
	PpsCompressionType compression;
	gchar *uri_unc = NULL;
	GError *err = NULL;
	gboolean fast = TRUE;

	g_return_val_if_fail (uri != NULL, NULL);

	// Run twice, once with fast=TRUE, one with fast=FALSE
	for (int i = 0; i < 2; i++, fast = !fast) {
		document = new_document_for_uri (uri, fast, &compression, &err);
		g_assert (document != NULL || err != NULL);

		if (err != NULL) {
			if (i == 0)
				g_clear_error (&err);

			continue;
		}

		uri_unc = pps_file_uncompress (uri, compression, &err);
		if (err != NULL) {
			/* Error uncompressing file */
			g_propagate_error (error, err);
			return NULL;
		}
		if (uri_unc)
			g_object_set_data_full (G_OBJECT (document),
			                        "uri-uncompressed",
			                        uri_unc,
			                        (GDestroyNotify) free_uncompressed_uri);

		return g_steal_pointer (&document);
	}

	g_assert (err != NULL);
	g_propagate_error (error, err);
	return NULL;
}

/**
 * pps_document_factory_get_document_for_fd:
 * @fd: a file descriptor
 * @mime_type: the mime type
 * @error: (allow-none): a #GError location to store an error, or %NULL
 *
 * Synchronously creates a #PpsDocument for the document from @fd using the backend
 * for loading documents of type @mime_type; or, if the backend does not support
 * loading from file descriptors, or an error occurred on opening the document,
 * returns %NULL and fills in @error.
 * If the document is encrypted, it is returned but also @error is set to
 * %PPS_DOCUMENT_ERROR_ENCRYPTED.
 *
 * If the mime type cannot be inferred from the file descriptor, and @mime_type is %NULL,
 * an error is returned.
 *
 * Note that this function takes ownership of @fd; you must not ever
 * operate on it again. It will be closed automatically if the document
 * is destroyed, or if this function returns %NULL.
 *
 * Returns: (transfer full): a new #PpsDocument, or %NULL
 *
 * Since: 42.0
 */
PpsDocument *
pps_document_factory_get_document_for_fd (int fd,
                                          const char *mime_type,
                                          GError **error)
{
	g_return_val_if_fail (fd != -1, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (mime_type == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "Cannot query mime type from file descriptor");
		return NULL;
	}

	return pps_document_factory_new_document_for_mime_type (mime_type, error);
}

static void
file_filter_add_mime_types (PpsBackendInfo *info, GtkFileFilter *filter)
{
	char **mime_types;
	guint i;

	mime_types = info->mime_types;
	if (mime_types == NULL)
		return;

	for (i = 0; mime_types[i] != NULL; ++i)
		gtk_file_filter_add_mime_type (filter, mime_types[i]);
}

/**
 * pps_document_factory_add_filters:
 * @dialog: a #GtkFileDialog
 * @document: (nullable): a #PpsDocument, or %NULL
 *
 * Adds some file filters to @dialog.

 * Always add a "All documents" format.
 *
 * If @document is not %NULL, adds a #GtkFileFilter for @document's MIME type.
 *
 * If @document is %NULL, adds a #GtkFileFilter for each document type that papers
 * can handle.
 */
void
pps_document_factory_add_filters (GtkFileDialog *dialog, PpsDocument *document)
{
	GtkFileFilter *all_documents_filter, *default_filter, *all_files_filter;
	GListStore *filters = g_list_store_new (GTK_TYPE_FILTER);

	g_return_if_fail (GTK_IS_FILE_DIALOG (dialog));
	g_return_if_fail (document == NULL || PPS_IS_DOCUMENT (document));

	default_filter = all_documents_filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (all_documents_filter, _ ("All Documents"));
	g_list_foreach (pps_backends_list, (GFunc) file_filter_add_mime_types,
	                all_documents_filter);
	g_list_store_append (filters, all_documents_filter);

	if (document) {
		PpsBackendInfo *info;

		info = get_backend_info_for_document (document);
		g_assert (info != NULL);
		default_filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (default_filter, info->type_desc);
		file_filter_add_mime_types (info, default_filter);
		g_list_store_append (filters, default_filter);
	} else {
		GList *l;

		for (l = pps_backends_list; l; l = l->next) {
			GtkFileFilter *filter = gtk_file_filter_new ();
			PpsBackendInfo *info = (PpsBackendInfo *) l->data;

			gtk_file_filter_set_name (filter, info->type_desc);
			file_filter_add_mime_types (info, filter);
			g_list_store_append (filters, filter);
		}
	}

	all_files_filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (all_files_filter, _ ("All Files"));
	gtk_file_filter_add_pattern (all_files_filter, "*");
	g_list_store_append (filters, all_files_filter);

	gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
	gtk_file_dialog_set_default_filter (dialog, default_filter);
}
