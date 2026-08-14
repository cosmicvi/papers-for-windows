// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright © 2009 Christian Persch
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "pps-file-helpers.h"

static gchar *tmp_dir = NULL;

/*
 * pps_dir_ensure_exists:
 * @dir: the directory name
 * @mode: permissions to use when creating the directory
 * @error: a location to store a #GError
 *
 * Create @dir recursively with permissions @mode.
 *
 * Returns: %TRUE on success, or %FALSE on error with @error filled in
 */
static gboolean
_pps_dir_ensure_exists (const gchar *dir,
                        int mode,
                        GError **error)
{
	int errsv;
	g_autofree char *display_name = NULL;

	g_return_val_if_fail (dir != NULL, FALSE);

	errno = 0;
	if (g_mkdir_with_parents (dir, mode) == 0)
		return TRUE;

	errsv = errno;
	if (errsv == EEXIST && g_file_test (dir, G_FILE_TEST_IS_DIR))
		return TRUE;

	display_name = g_filename_display_name (dir);
	g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errsv),
	             "Failed to create directory '%s': %s",
	             display_name, g_strerror (errsv));

	return FALSE;
}

/*
 * _pps_tmp_dir:
 * @error: a location to store a #GError
 *
 * Returns the tmp directory.
 *
 * Returns: the tmp directory, or %NULL with @error filled in if the
 *   directory could not be created
 */
static const char *
_pps_tmp_dir (GError **error)
{

	if (tmp_dir == NULL) {
		g_autofree gchar *dirname = NULL;
		const gchar *prgname;

		prgname = g_get_prgname ();
		dirname = g_strdup_printf ("%s-%u", prgname ? prgname : "unknown", getpid ());
		tmp_dir = g_build_filename (g_get_tmp_dir (), dirname, NULL);
	}

	if (!_pps_dir_ensure_exists (tmp_dir, 0700, error))
		return NULL;

	return tmp_dir;
}

void
_pps_file_helpers_init (void)
{
}

void
_pps_file_helpers_shutdown (void)
{
	if (tmp_dir != NULL)
		g_rmdir (tmp_dir);

	g_clear_pointer (&tmp_dir, g_free);
}

/**
 * pps_mkstemp:
 * @tmpl: a template string; must contain 'XXXXXX', but not necessarily as a suffix
 * @file_name: (out) (type filename): a location to store the filename of the temp file
 * @error: a location to store a #GError
 *
 * Creates a temp file in the papers temp directory.
 *
 * Returns: a file descriptor to the newly created temp file name, or %-1
 *   on error with @error filled in
 */
int
pps_mkstemp (const char *tmpl,
             char **file_name,
             GError **error)
{
	const char *tmp;
	char *name;
	int fd;

	if ((tmp = _pps_tmp_dir (error)) == NULL)
		return -1;

	name = g_build_filename (tmp, tmpl, NULL);
	fd = g_mkstemp_full (name, O_RDWR | O_CLOEXEC, 0600);

	if (fd == -1) {
		int errsv = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errsv),
		             _ ("Failed to create a temporary file: %s"),
		             g_strerror (errsv));

		g_free (name);
		return -1;
	}

	if (file_name)
		*file_name = name;

	return fd;
}

static void
close_fd_cb (gpointer fdptr)
{
	int fd = GPOINTER_TO_INT (fdptr);

	close (fd);
}

/**
 * pps_mkstemp_file:
 * @tmpl: a template string; must contain 'XXXXXX', but not necessarily as a suffix
 * @error: a location to store a #GError
 *
 * Creates a temp #GFile in the papers temp directory. See pps_mkstemp() for more information.
 *
 * Returns: (transfer full): a newly allocated #GFile for the newly created temp file name, or %NULL
 *   on error with @error filled in
 */
GFile *
pps_mkstemp_file (const char *tmpl,
                  GError **error)
{
	g_autofree char *file_name = NULL;
	int fd;
	GFile *file;

	fd = pps_mkstemp (tmpl, &file_name, error);
	if (fd == -1)
		return NULL;

	file = g_file_new_for_path (file_name);

	g_object_set_data_full (G_OBJECT (file), "pps-mkstemp-fd",
	                        GINT_TO_POINTER (fd), (GDestroyNotify) close_fd_cb);

	return file;
}

/* Remove a local temp file created by papers */
void
pps_tmp_filename_unlink (const gchar *filename)
{
	if (!filename)
		return;

	if (!tmp_dir)
		return;

	if (g_str_has_prefix (filename, tmp_dir)) {
		g_unlink (filename);
	}
}

void
pps_tmp_file_unlink (GFile *file)
{
	gboolean res;
	g_autoptr (GError) error = NULL;

	if (!file)
		return;

	res = g_file_delete (file, NULL, &error);
	if (!res) {
		g_autofree char *uri = NULL;

		uri = g_file_get_uri (file);
		g_warning ("Unable to delete temp file %s: %s\n", uri, error->message);
	}
}

void
pps_tmp_uri_unlink (const gchar *uri)
{
	g_autoptr (GFile) file = NULL;

	if (!uri)
		return;

	file = g_file_new_for_uri (uri);
	if (!g_file_is_native (file)) {
		g_warning ("Attempting to delete non native uri: %s\n", uri);
		return;
	}

	pps_tmp_file_unlink (file);
}

gboolean
pps_file_is_temp (GFile *file)
{
	g_autofree gchar *path = NULL;

	if (!g_file_is_native (file))
		return FALSE;

	path = g_file_get_path (file);
	if (!path)
		return FALSE;

	return g_str_has_prefix (path, g_get_tmp_dir ());
}

/**
 * pps_xfer_uri_simple:
 * @from: the source URI
 * @to: the target URI
 * @error: a #GError location to store an error, or %NULL
 *
 * Performs a g_file_copy() from @from to @to.
 *
 * Returns: %TRUE on success, or %FALSE on error with @error filled in
 */
gboolean
pps_xfer_uri_simple (const char *from,
                     const char *to,
                     GError **error)
{
	g_autoptr (GFile) source_file = NULL;
	g_autoptr (GFile) target_file = NULL;
	gboolean result;

	if (!from)
		return TRUE;

	g_return_val_if_fail (to != NULL, TRUE);

	source_file = g_file_new_for_uri (from);
	target_file = g_file_new_for_uri (to);

	result = g_file_copy (source_file, target_file,
	                      G_FILE_COPY_TARGET_DEFAULT_PERMS |
	                          G_FILE_COPY_OVERWRITE,
	                      NULL, NULL, NULL, error);

	return result;
}

/**
 * pps_file_copy_metadata:
 * @from: the source URI
 * @to: the target URI
 * @error: a #GError location to store an error, or %NULL
 *
 * Performs a g_file_copy_attributes() with %G_FILE_COPY_ALL_METADATA
 * from @from to @to.
 *
 * Returns: %TRUE if the attributes were copied successfully, %FALSE otherwise.
 */
gboolean
pps_file_copy_metadata (const char *from,
                        const char *to,
                        GError **error)
{
	g_autoptr (GFile) source_file = NULL;
	g_autoptr (GFile) target_file = NULL;
	gboolean result;

	g_return_val_if_fail (from != NULL, FALSE);
	g_return_val_if_fail (to != NULL, FALSE);

	source_file = g_file_new_for_uri (from);
	target_file = g_file_new_for_uri (to);

	result = g_file_copy_attributes (source_file, target_file,
	                                 G_FILE_COPY_ALL_METADATA |
	                                     G_FILE_COPY_TARGET_DEFAULT_PERMS,
	                                 NULL, error);

	return result;
}

static gchar *
get_mime_type_from_uri (const gchar *uri, GError **error)
{
	g_autoptr (GFile) file = NULL;
	g_autoptr (GFileInfo) file_info = NULL;
	const gchar *content_type;
	gchar *mime_type = NULL;

	file = g_file_new_for_uri (uri);
	file_info = g_file_query_info (file,
	                               G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
	                               0, NULL, error);
	if (file_info == NULL)
		return NULL;

	content_type = g_file_info_get_content_type (file_info);
	if (content_type != NULL) {
		mime_type = g_content_type_get_mime_type (content_type);
	}
	if (mime_type == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     _ ("Unknown MIME Type"));
	}

	return mime_type;
}

static gchar *
get_mime_type_from_data (const gchar *uri, GError **error)
{
	g_autoptr (GFile) file = NULL;
	g_autoptr (GFileInputStream) input_stream = NULL;
	gssize size_read;
	guchar buffer[1024];
	gboolean retval;
	g_autofree gchar *content_type = NULL;
	gchar *mime_type = NULL;

	file = g_file_new_for_uri (uri);

	input_stream = g_file_read (file, NULL, error);
	if (!input_stream)
		return NULL;

	size_read = g_input_stream_read (G_INPUT_STREAM (input_stream),
	                                 buffer, sizeof (buffer), NULL, error);
	if (size_read == -1)
		return NULL;

	retval = g_input_stream_close (G_INPUT_STREAM (input_stream), NULL, error);
	if (!retval)
		return NULL;

	content_type = g_content_type_guess (NULL, /* no filename */
	                                     buffer, size_read,
	                                     NULL);
	if (content_type == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     _ ("Unknown MIME Type"));
		return NULL;
	}

	mime_type = g_content_type_get_mime_type (content_type);
	if (mime_type == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     _ ("Unknown MIME Type"));
	}

	return mime_type;
}

/**
 * pps_file_get_mime_type:
 * @uri: the URI
 * @fast: whether to use fast MIME type detection
 * @error: a #GError location to store an error, or %NULL
 *
 * Returns: a newly allocated string with the MIME type of the file at
 *   @uri, or %NULL on error or if the MIME type could not be determined
 */
gchar *
pps_file_get_mime_type (const gchar *uri,
                        gboolean fast,
                        GError **error)
{
	return fast ? get_mime_type_from_uri (uri, error) : get_mime_type_from_data (uri, error);
}

/**
 * pps_file_get_mime_type_from_fd:
 * @fd: an file descriptor (must be seekable)
 * @error: a #GError location to store an error, or %NULL
 *
 * Returns: a newly allocated string with the MIME type of the file referred to
 *   by @fd, or %NULL on error or if the MIME type could not be determined
 */
gchar *
pps_file_get_mime_type_from_fd (int fd,
                                GError **error)
{
	guchar buffer[4096];
	ssize_t r;
	off_t pos;
	g_autofree char *content_type = NULL;
	char *mime_type;

	g_return_val_if_fail (fd != -1, NULL);

	pos = lseek (fd, 0, SEEK_CUR);
	if (pos == (off_t) -1) {
		int errsv = errno;
		g_set_error (error, G_IO_ERROR,
		             g_io_error_from_errno (errsv),
		             "Failed to get MIME type: %s",
		             g_strerror (errsv));
		return NULL;
	}

	do {
		r = read (fd, buffer, sizeof (buffer));
	} while (r == -1 && errno == EINTR);

	if (r == -1) {
		int errsv = errno;
		g_set_error (error, G_IO_ERROR,
		             g_io_error_from_errno (errsv),
		             "Failed to get MIME type: %s",
		             g_strerror (errsv));

		(void) lseek (fd, pos, SEEK_SET);
		return NULL;
	}

	if (lseek (fd, pos, SEEK_SET) == (off_t) -1) {
		int errsv = errno;
		g_set_error (error, G_IO_ERROR,
		             g_io_error_from_errno (errsv),
		             "Failed to get MIME type: %s",
		             g_strerror (errsv));
		return NULL;
	}

	content_type = g_content_type_guess (NULL, /* no filename */
	                                     buffer, r,
	                                     NULL);
	if (content_type == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     _ ("Unknown MIME Type"));
		return NULL;
	}

	mime_type = g_content_type_get_mime_type (content_type);
	if (mime_type == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                     _ ("Unknown MIME Type"));
		return NULL;
	}

	return mime_type;
}

/* Compressed files support */

static const char *compressor_cmds[] = {
	NULL,
	"bzip2",
	"gzip",
	"xz"
};

#define N_ARGS 4
#define BUFFER_SIZE 1024

static void
compression_child_setup_cb (gpointer fd_ptr)
{
#ifndef G_OS_WIN32
	int fd = GPOINTER_TO_INT (fd_ptr);
	int flags;

	flags = fcntl (fd, F_GETFD);
	if (flags >= 0 && (flags & FD_CLOEXEC)) {
		flags &= ~FD_CLOEXEC;
		fcntl (fd, F_SETFD, flags);
	}
#endif
}

static gchar *
compression_run (const gchar *uri,
                 PpsCompressionType type,
                 gboolean compress,
                 GError **error)
{
	gchar *argv[N_ARGS];
	gchar *uri_dst = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *filename_dst = NULL;
	g_autofree gchar *cmd = NULL;
	gint fd, pout;
	GError *err = NULL;

	if (type == PPS_COMPRESSION_NONE)
		return NULL;

	cmd = g_find_program_in_path (compressor_cmds[type]);
	if (!cmd) {
		/* FIXME: better error codes! */
		/* FIXME: i18n later */
		g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		             "Failed to find the \"%s\" command in the search path.",
		             compressor_cmds[type]);
		return NULL;
	}

	filename = g_filename_from_uri (uri, NULL, error);
	if (!filename)
		return NULL;

	fd = pps_mkstemp ("comp.XXXXXX", &filename_dst, error);
	if (fd == -1)
		return NULL;

	argv[0] = cmd;
	argv[1] = compress ? (char *) "-c" : (char *) "-cd";
	argv[2] = filename;
	argv[3] = NULL;

	if (g_spawn_async_with_pipes (NULL, argv, NULL,
	                              G_SPAWN_STDERR_TO_DEV_NULL,
	                              compression_child_setup_cb, GINT_TO_POINTER (fd),
	                              NULL,
	                              NULL, &pout, NULL, &err)) {
		g_autoptr (GIOChannel) in = NULL;
		g_autoptr (GIOChannel) out = NULL;
		gchar buf[BUFFER_SIZE];
		GIOStatus read_st, write_st;
		gsize bytes_read, bytes_written;

		in = g_io_channel_unix_new (pout);
		g_io_channel_set_encoding (in, NULL, NULL);
		out = g_io_channel_unix_new (fd);
		g_io_channel_set_encoding (out, NULL, NULL);

		do {
			read_st = g_io_channel_read_chars (in, buf,
			                                   BUFFER_SIZE,
			                                   &bytes_read,
			                                   error);
			if (read_st == G_IO_STATUS_NORMAL) {
				write_st = g_io_channel_write_chars (out, buf,
				                                     bytes_read,
				                                     &bytes_written,
				                                     error);
				if (write_st == G_IO_STATUS_ERROR)
					break;
			} else if (read_st == G_IO_STATUS_ERROR) {
				break;
			}
		} while (bytes_read > 0);
	}

	close (fd);

	if (err) {
		g_propagate_error (error, err);
	} else {
		uri_dst = g_filename_to_uri (filename_dst, NULL, error);
	}

	return uri_dst;
}

/**
 * pps_file_uncompress:
 * @uri: a file URI
 * @type: the compression type
 * @error: a #GError location to store an error, or %NULL
 *
 * Uncompresses the file at @uri.
 *
 * If @type is %PPS_COMPRESSION_NONE, it does nothing and returns %NULL.
 *
 * Otherwise, it returns the filename of a
 * temporary file containing the decompressed data from the file at @uri.
 * On error it returns %NULL and fills in @error.
 *
 * It is the caller's responsibility to unlink the temp file after use.
 *
 * Returns: a newly allocated string URI, or %NULL on error
 */
gchar *
pps_file_uncompress (const gchar *uri,
                     PpsCompressionType type,
                     GError **error)
{
	g_return_val_if_fail (uri != NULL, NULL);

	return compression_run (uri, type, FALSE, error);
}

/**
 * pps_file_compress:
 * @uri: a file URI
 * @type: the compression type
 * @error: a #GError location to store an error, or %NULL
 *
 * Compresses the file at @uri.

 * If @type is %PPS_COMPRESSION_NONE, it does nothing and returns %NULL.
 *
 * Otherwise, it returns the filename of a
 * temporary file containing the compressed data from the file at @uri.
 *
 * On error it returns %NULL and fills in @error.
 *
 * It is the caller's responsibility to unlink the temp file after use.
 *
 * Returns: a newly allocated string URI, or %NULL on error
 */
gchar *
pps_file_compress (const gchar *uri,
                   PpsCompressionType type,
                   GError **error)
{
	g_return_val_if_fail (uri != NULL, NULL);

	return compression_run (uri, type, TRUE, error);
}
