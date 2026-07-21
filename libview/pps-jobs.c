// SPDX-License-Identifier: GPL-2.0-or-later
/* this file is part of papers, a gnome document viewer
 *
 *  Copyright (C) 2008 Carlos Garcia Campos <carlosgc@gnome.org>
 *  Copyright (C) 2005 Red Hat, Inc
 */

#include <config.h>

#include "cairo.h"
#include "pps-debug.h"
#include "pps-document-annotations.h"
#include "pps-document-attachments.h"
#include "pps-document-factory.h"
#include "pps-document-find.h"
#include "pps-document-fonts.h"
#include "pps-document-forms.h"
#include "pps-document-images.h"
#include "pps-document-layers.h"
#include "pps-document-links.h"
#include "pps-document-media.h"
#include "pps-document-misc.h"
#include "pps-document-print.h"
#include "pps-document-security.h"
#include "pps-document-signatures.h"
#include "pps-document-text.h"
#include "pps-file-exporter.h"
#include "pps-file-helpers.h"
#include "pps-jobs.h"
#include "pps-outlines.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "PpsJobs"

enum {
	FIND_UPDATED,
	FIND_LAST_SIGNAL
};

static guint job_find_signals[FIND_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PpsJobRenderTexture, pps_job_render_texture, PPS_TYPE_JOB)
G_DEFINE_TYPE (PpsJobPageData, pps_job_page_data, PPS_TYPE_JOB)
G_DEFINE_TYPE (PpsJobFind, pps_job_find, PPS_TYPE_JOB)

/* PpsJobLinks */
typedef struct _PpsJobLinksPrivate {
	GListModel *model;
} PpsJobLinksPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobLinks, pps_job_links, PPS_TYPE_JOB)

#define JOB_LINKS_GET_PRIVATE(o) pps_job_links_get_instance_private (o)

static void
pps_job_links_init (PpsJobLinks *job)
{
}

static void
pps_job_links_dispose (GObject *object)
{
	PpsJobLinksPrivate *priv = JOB_LINKS_GET_PRIVATE (PPS_JOB_LINKS (object));

	g_clear_object (&priv->model);

	G_OBJECT_CLASS (pps_job_links_parent_class)->dispose (object);
}

static void
fill_page_labels (GListModel *model, PpsJob *job)
{
	PpsDocumentLinks *document_links;
	GListModel *children;
	guint items = g_list_model_get_n_items (model);

	for (int i = 0; i < items; i++) {
		g_autoptr (PpsLink) link = NULL;
		g_autofree gchar *page_label = NULL;
		g_autoptr (PpsOutlines) outlines = g_list_model_get_item (model, i);

		g_object_get (outlines, "link", &link, "children", &children, NULL);

		if (!link)
			continue;

		document_links = PPS_DOCUMENT_LINKS (pps_job_get_document (job));
		page_label = pps_document_links_get_link_page_label (document_links, link);
		if (!page_label)
			continue;

		g_object_set (outlines, "label", page_label, NULL);

		if (children) {
			g_assert (G_IS_LIST_MODEL (children));
			fill_page_labels (children, job);
		}
	}
}

static void
pps_job_links_run (PpsJob *job)
{
	PpsJobLinksPrivate *priv = JOB_LINKS_GET_PRIVATE (PPS_JOB_LINKS (job));

	g_debug ("running links job");

	priv->model = pps_document_links_get_links_model (PPS_DOCUMENT_LINKS (pps_job_get_document (job)));

	fill_page_labels (priv->model, job);

	pps_job_succeeded (job);
}

static void
pps_job_links_class_init (PpsJobLinksClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_links_dispose;
	job_class->run = pps_job_links_run;
}

PpsJob *
pps_job_links_new (PpsDocument *document)
{
	PpsJob *job;

	g_debug ("new links job");

	job = g_object_new (PPS_TYPE_JOB_LINKS,
	                    "document", document,
	                    NULL);

	return job;
}

/**
 * pps_job_links_get_model:
 * @job: #PpsJobLinks
 *
 * Get a #GListModel loaded with the links
 *
 * Returns: (nullable) (transfer none): a #GListModel of #PpsOutlines objects
 */
GListModel *
pps_job_links_get_model (PpsJobLinks *self)
{
	PpsJobLinksPrivate *priv = JOB_LINKS_GET_PRIVATE (self);

	return priv->model;
}

/* PpsJobAttachments */
typedef struct _PpsJobAttachmentsPrivate {
	GList *attachments;
} PpsJobAttachmentsPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobAttachments, pps_job_attachments, PPS_TYPE_JOB)

#define JOB_ATTACHMENTS_GET_PRIVATE(o) pps_job_attachments_get_instance_private (o)

static void
pps_job_attachments_init (PpsJobAttachments *job)
{
}

static void
pps_job_attachments_dispose (GObject *object)
{
	PpsJobAttachmentsPrivate *priv = JOB_ATTACHMENTS_GET_PRIVATE (PPS_JOB_ATTACHMENTS (object));

	g_clear_list (&priv->attachments, g_object_unref);

	G_OBJECT_CLASS (pps_job_attachments_parent_class)->dispose (object);
}

static void
pps_job_attachments_run (PpsJob *job)
{
	PpsJobAttachmentsPrivate *priv = JOB_ATTACHMENTS_GET_PRIVATE (PPS_JOB_ATTACHMENTS (job));

	g_debug ("running attachments job");

	priv->attachments =
	    pps_document_attachments_get_attachments (PPS_DOCUMENT_ATTACHMENTS (pps_job_get_document (job)));

	pps_job_succeeded (job);
}

/**
 * pps_job_attachments_get_attachments:
 * @job_attachments: a #PpsJobAttachments
 *
 * Returns: (nullable) (transfer none) (element-type PpsAttachment): a list of #PpsAttachment objects
 */
GList *
pps_job_attachments_get_attachments (PpsJobAttachments *self)
{
	PpsJobAttachmentsPrivate *priv = JOB_ATTACHMENTS_GET_PRIVATE (self);

	return priv->attachments;
}

static void
pps_job_attachments_class_init (PpsJobAttachmentsClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_attachments_dispose;
	job_class->run = pps_job_attachments_run;
}

PpsJob *
pps_job_attachments_new (PpsDocument *document)
{
	PpsJob *job;

	g_debug ("new attachments job");

	job = g_object_new (PPS_TYPE_JOB_ATTACHMENTS,
	                    "document", document,
	                    NULL);

	return job;
}

/* PpsJobAnnots */
typedef struct _PpsJobAnnotsPrivate {
	GList *annots;
} PpsJobAnnotsPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobAnnots, pps_job_annots, PPS_TYPE_JOB)

#define JOB_ANNOTS_GET_PRIVATE(o) pps_job_annots_get_instance_private (o)

static void
pps_job_annots_init (PpsJobAnnots *job)
{
}

static void
pps_job_annots_dispose (GObject *object)
{
	PpsJobAnnotsPrivate *priv = JOB_ANNOTS_GET_PRIVATE (PPS_JOB_ANNOTS (object));

	g_list_free_full (g_steal_pointer (&priv->annots), g_object_unref);

	G_OBJECT_CLASS (pps_job_annots_parent_class)->dispose (object);
}

static void
pps_job_annots_run (PpsJob *job)
{
	PpsJobAnnotsPrivate *priv = JOB_ANNOTS_GET_PRIVATE (PPS_JOB_ANNOTS (job));

	g_debug ("running annots job");

	for (gint i = 0; i < pps_document_get_n_pages (pps_job_get_document (job)); i++) {
		GList *annots;
		PpsPage *page;

		page = pps_document_get_page (pps_job_get_document (job), i);
		annots = pps_document_annotations_get_annotations (PPS_DOCUMENT_ANNOTATIONS (pps_job_get_document (job)),
		                                                   page);
		g_object_unref (page);

		if (annots)
			priv->annots = g_list_concat (priv->annots, annots);
	}

	pps_job_succeeded (job);
}

static void
pps_job_annots_class_init (PpsJobAnnotsClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_annots_dispose;
	job_class->run = pps_job_annots_run;
}

PpsJob *
pps_job_annots_new (PpsDocument *document)
{
	PpsJob *job;

	g_debug ("new annots job");

	job = g_object_new (PPS_TYPE_JOB_ANNOTS,
	                    "document", document,
	                    NULL);

	return job;
}

/**
 * pps_job_annots_get_annots:
 * @job: an #PpsJobAnnots
 *
 * Returns: (nullable) (transfer none) (element-type PpsAnnotation):
 */
GList *
pps_job_annots_get_annots (PpsJobAnnots *job)
{
	PpsJobAnnotsPrivate *priv = JOB_ANNOTS_GET_PRIVATE (job);

	return priv->annots;
}

/* PpsJobRenderTexture */
static void
pps_job_render_texture_init (PpsJobRenderTexture *job)
{
}

static void
pps_job_render_texture_dispose (GObject *object)
{
	PpsJobRenderTexture *job = PPS_JOB_RENDER_TEXTURE (object);

	g_debug ("disposing job render: %d (%p)", job->page, job);

	g_clear_object (&job->texture);
	g_clear_object (&job->selection);
	g_clear_pointer (&job->selection_region, cairo_region_destroy);

	(*G_OBJECT_CLASS (pps_job_render_texture_parent_class)->dispose) (object);
}

PpsJob *
pps_job_render_texture_new (PpsDocument *document,
                            gint page,
                            gint rotation,
                            gdouble scale,
                            gint width,
                            gint height,
                            PpsRenderAnnotsFlags annot_flags)
{
	PpsJobRenderTexture *job;

	g_debug ("new render job: page: %d", page);

	job = g_object_new (PPS_TYPE_JOB_RENDER_TEXTURE,
	                    "document", document,
	                    NULL);

	job->page = page;
	job->rotation = rotation;
	job->scale = scale;
	job->target_width = width;
	job->target_height = height;
	job->annot_flags = annot_flags;

	return PPS_JOB (job);
}

static void
pps_job_render_texture_run (PpsJob *job)
{
	PpsJobRenderTexture *job_render = PPS_JOB_RENDER_TEXTURE (job);
	PpsPage *pps_page;
	PpsRenderContext *rc;
	cairo_surface_t *surface, *selection = NULL;

	g_debug ("running render job: page: %d (%p)", job_render->page, job);

	PPS_PROFILER_START (PPS_GET_TYPE_NAME (job), g_strdup_printf ("page: %d", job_render->page));

	pps_page = pps_document_get_page (pps_job_get_document (job), job_render->page);
	rc = pps_render_context_new (pps_page, job_render->rotation, job_render->scale, job_render->annot_flags);
	pps_render_context_set_target_size (rc,
	                                    job_render->target_width, job_render->target_height);
	g_object_unref (pps_page);

	surface = pps_document_render (pps_job_get_document (job), rc);

	if (surface == NULL ||
	    cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
		g_object_unref (rc);

		if (surface != NULL) {
			cairo_status_t status = cairo_surface_status (surface);
			pps_job_failed (job,
			                PPS_DOCUMENT_ERROR,
			                PPS_DOCUMENT_ERROR_INVALID,
			                _ ("Failed to render page %d: %s"),
			                job_render->page,
			                cairo_status_to_string (status));
		} else {
			pps_job_failed (job,
			                PPS_DOCUMENT_ERROR,
			                PPS_DOCUMENT_ERROR_INVALID,
			                _ ("Failed to render page %d"),
			                job_render->page);
		}

		job_render->texture = NULL;
		return;
	}

	job_render->texture = pps_document_misc_texture_from_surface (surface);
	cairo_surface_destroy (surface);

	/* If job was cancelled during the page rendering,
	 * we return now, so that the thread is finished ASAP
	 */
	if (g_cancellable_is_cancelled (pps_job_get_cancellable (job))) {
		PPS_PROFILER_STOP ();
		g_object_unref (rc);
		return;
	}

	if (job_render->include_selection && PPS_IS_SELECTION (pps_job_get_document (job))) {
		pps_selection_render_selection (PPS_SELECTION (pps_job_get_document (job)),
		                                rc,
		                                &selection,
		                                &(job_render->selection_points),
		                                NULL,
		                                job_render->selection_style,
		                                &(job_render->text), &(job_render->base));
		job_render->selection_region =
		    pps_selection_get_selection_region (PPS_SELECTION (pps_job_get_document (job)),
		                                        rc,
		                                        job_render->selection_style,
		                                        &(job_render->selection_points));

		if (selection != NULL) {
			job_render->selection = pps_document_misc_texture_from_surface (selection);
			cairo_surface_destroy (selection);
		}
	}

	g_object_unref (rc);

	PPS_PROFILER_STOP ();

	pps_job_succeeded (job);
}

static void
pps_job_render_texture_class_init (PpsJobRenderTextureClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_render_texture_dispose;
	job_class->run = pps_job_render_texture_run;
}

void
pps_job_render_texture_set_selection_info (PpsJobRenderTexture *job,
                                           PpsRectangle *selection_points,
                                           PpsSelectionStyle selection_style,
                                           GdkRGBA *text,
                                           GdkRGBA *base)
{
	job->include_selection = TRUE;

	job->selection_points = *selection_points;
	job->selection_style = selection_style;
	job->text = *text;
	job->base = *base;
}

/* PpsJobPageData */
static void
pps_job_page_data_init (PpsJobPageData *job)
{
}

static void
pps_job_page_data_run (PpsJob *job)
{
	PpsJobPageData *job_pd = PPS_JOB_PAGE_DATA (job);
	PpsPage *pps_page;

	g_debug ("running page data job: page: %d (%p)", job_pd->page, job);

	pps_page = pps_document_get_page (pps_job_get_document (job), job_pd->page);

	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_TEXT_MAPPING) && PPS_IS_DOCUMENT_TEXT (pps_job_get_document (job)))
		job_pd->text_mapping =
		    pps_document_text_get_text_mapping (PPS_DOCUMENT_TEXT (pps_job_get_document (job)), pps_page);
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_TEXT) && PPS_IS_DOCUMENT_TEXT (pps_job_get_document (job)))
		job_pd->text =
		    pps_document_text_get_text (PPS_DOCUMENT_TEXT (pps_job_get_document (job)), pps_page);
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_TEXT_LAYOUT) && PPS_IS_DOCUMENT_TEXT (pps_job_get_document (job)))
		pps_document_text_get_text_layout (PPS_DOCUMENT_TEXT (pps_job_get_document (job)),
		                                   pps_page,
		                                   &(job_pd->text_layout),
		                                   &(job_pd->text_layout_length));
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_TEXT_ATTRS) && PPS_IS_DOCUMENT_TEXT (pps_job_get_document (job)))
		job_pd->text_attrs =
		    pps_document_text_get_text_attrs (PPS_DOCUMENT_TEXT (pps_job_get_document (job)),
		                                      pps_page);
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_TEXT_LOG_ATTRS) && job_pd->text) {
		job_pd->text_log_attrs_length = g_utf8_strlen (job_pd->text, -1);
		job_pd->text_log_attrs = g_new0 (PangoLogAttr, job_pd->text_log_attrs_length + 1);

		/* FIXME: We need API to get the language of the document */
		pango_get_log_attrs (job_pd->text, -1, -1, NULL, job_pd->text_log_attrs, job_pd->text_log_attrs_length + 1);
	}
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_LINKS) && PPS_IS_DOCUMENT_LINKS (pps_job_get_document (job)))
		job_pd->link_mapping =
		    pps_document_links_get_links (PPS_DOCUMENT_LINKS (pps_job_get_document (job)), pps_page);
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_FORMS) && PPS_IS_DOCUMENT_FORMS (pps_job_get_document (job)))
		job_pd->form_field_mapping =
		    pps_document_forms_get_form_fields (PPS_DOCUMENT_FORMS (pps_job_get_document (job)),
		                                        pps_page);
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_IMAGES) && PPS_IS_DOCUMENT_IMAGES (pps_job_get_document (job)))
		job_pd->image_mapping =
		    pps_document_images_get_image_mapping (PPS_DOCUMENT_IMAGES (pps_job_get_document (job)),
		                                           pps_page);
	if ((job_pd->flags & PPS_PAGE_DATA_INCLUDE_MEDIA) && PPS_IS_DOCUMENT_MEDIA (pps_job_get_document (job)))
		job_pd->media_mapping =
		    pps_document_media_get_media_mapping (PPS_DOCUMENT_MEDIA (pps_job_get_document (job)),
		                                          pps_page);
	g_object_unref (pps_page);

	pps_job_succeeded (job);
}

static void
pps_job_page_data_class_init (PpsJobPageDataClass *class)
{
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	job_class->run = pps_job_page_data_run;
}

PpsJob *
pps_job_page_data_new (PpsDocument *document,
                       gint page,
                       PpsJobPageDataFlags flags)
{
	PpsJobPageData *job;

	g_debug ("new page data job: page: %d", page);

	job = g_object_new (PPS_TYPE_JOB_PAGE_DATA,
	                    "document", document,
	                    NULL);

	job->page = page;
	job->flags = flags;

	return PPS_JOB (job);
}

/* PpsJobThumbnailTexture */
typedef struct {
	gint page;
	gint rotation;
	gdouble scale;
	gint target_width;
	gint target_height;

	GdkTexture *thumbnail_texture;
} PpsJobThumbnailTexturePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobThumbnailTexture, pps_job_thumbnail_texture, PPS_TYPE_JOB)

#define JOB_THUMBNAIL_TEXTURE_GET_PRIVATE(o) pps_job_thumbnail_texture_get_instance_private (o)

static void
pps_job_thumbnail_texture_init (PpsJobThumbnailTexture *job)
{
}

static void
pps_job_thumbnail_texture_dispose (GObject *object)
{
	PpsJobThumbnailTexturePrivate *priv =
	    JOB_THUMBNAIL_TEXTURE_GET_PRIVATE (PPS_JOB_THUMBNAIL_TEXTURE (object));

	g_debug ("disposing job thumbnail: page: %d", priv->page);

	g_clear_object (&priv->thumbnail_texture);

	G_OBJECT_CLASS (pps_job_thumbnail_texture_parent_class)->dispose (object);
}

static void
pps_job_thumbnail_texture_run (PpsJob *job)
{
	PpsJobThumbnailTexturePrivate *priv =
	    JOB_THUMBNAIL_TEXTURE_GET_PRIVATE (PPS_JOB_THUMBNAIL_TEXTURE (job));
	PpsRenderContext *rc;
	PpsPage *page;
	cairo_surface_t *surface;

	g_debug ("running thumbnail job: page: %d (%p)", priv->page, job);

	PPS_PROFILER_START (PPS_GET_TYPE_NAME (job), g_strdup_printf ("page: %d", priv->page));
	page = pps_document_get_page (pps_job_get_document (job), priv->page);
	rc = pps_render_context_new (page, priv->rotation, priv->scale, PPS_RENDER_ANNOTS_ALL);
	pps_render_context_set_target_size (rc,
	                                    priv->target_width, priv->target_height);
	g_object_unref (page);

	surface = pps_document_get_thumbnail_surface (pps_job_get_document (job), rc);

	priv->thumbnail_texture = pps_document_misc_texture_from_surface (surface);
	cairo_surface_destroy (surface);
	g_object_unref (rc);
	PPS_PROFILER_STOP ();

	if (priv->thumbnail_texture == NULL) {
		pps_job_failed (job,
		                PPS_DOCUMENT_ERROR,
		                PPS_DOCUMENT_ERROR_INVALID,
		                _ ("Failed to create thumbnail for page %d"),
		                priv->page);
	} else {
		pps_job_succeeded (job);
	}
}

static void
pps_job_thumbnail_texture_class_init (PpsJobThumbnailTextureClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_thumbnail_texture_dispose;
	job_class->run = pps_job_thumbnail_texture_run;
}

PpsJob *
pps_job_thumbnail_texture_new (PpsDocument *document,
                               gint page,
                               gint rotation,
                               gdouble scale)
{
	PpsJobThumbnailTexture *job;
	PpsJobThumbnailTexturePrivate *priv;

	g_debug ("new thumbnail job: page: %d", page);

	job = g_object_new (PPS_TYPE_JOB_THUMBNAIL_TEXTURE,
	                    "document", document,
	                    NULL);

	priv = JOB_THUMBNAIL_TEXTURE_GET_PRIVATE (job);
	priv->page = page;
	priv->rotation = rotation;
	priv->scale = scale;
	priv->target_width = -1;
	priv->target_height = -1;

	return PPS_JOB (job);
}

PpsJob *
pps_job_thumbnail_texture_new_with_target_size (PpsDocument *document,
                                                gint page,
                                                gint rotation,
                                                gint target_width,
                                                gint target_height)
{
	PpsJob *job = pps_job_thumbnail_texture_new (document, page, rotation, 1.);
	PpsJobThumbnailTexturePrivate *priv =
	    JOB_THUMBNAIL_TEXTURE_GET_PRIVATE (PPS_JOB_THUMBNAIL_TEXTURE (job));

	priv->target_width = target_width;
	priv->target_height = target_height;

	return job;
}

/**
 * pps_job_thumbnail_texture_get_texture:
 * @job: an #PpsJobThumbnailTexture job
 *
 * This is similar to pps_job_find_get_n_results() but it takes
 * care to treat any multi-line matches as being only one result.
 *
 * Returns: (nullable) (transfer none): total number of match results in @page
 */
GdkTexture *
pps_job_thumbnail_texture_get_texture (PpsJobThumbnailTexture *job)
{
	PpsJobThumbnailTexturePrivate *priv = JOB_THUMBNAIL_TEXTURE_GET_PRIVATE (job);

	g_return_val_if_fail (PPS_IS_JOB_THUMBNAIL_TEXTURE (job), NULL);

	return priv->thumbnail_texture;
}

/* PpsJobFonts */
G_DEFINE_TYPE (PpsJobFonts, pps_job_fonts, PPS_TYPE_JOB)

static void
pps_job_fonts_init (PpsJobFonts *job)
{
}

static void
pps_job_fonts_run (PpsJob *job)
{
	PpsDocument *document = pps_job_get_document (job);

	g_debug ("running fonts job");

	pps_document_fonts_scan (PPS_DOCUMENT_FONTS (document));

	pps_job_succeeded (job);
}

static void
pps_job_fonts_class_init (PpsJobFontsClass *class)
{
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	job_class->run = pps_job_fonts_run;
}

PpsJob *
pps_job_fonts_new (PpsDocument *document)
{
	PpsJobFonts *job;

	g_debug ("new fonts job");

	job = g_object_new (PPS_TYPE_JOB_FONTS,
	                    "document", document,
	                    NULL);

	return PPS_JOB (job);
}

/**
 * PpsJobLoad:
 *
 * A job class to load an #PpsDocument
 *
 * Supports loading in different forms depending on the options
 * being set.
 *
 */

typedef struct _PpsJobLoadPrivate {
	gchar *uri;
	int fd;
	char *mime_type;
	gchar *password;
	GPasswordSave password_save;
	PpsDocument *loaded_document;
	PpsDocument *document;
} PpsJobLoadPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobLoad, pps_job_load, PPS_TYPE_JOB)

#define JOB_LOAD_GET_PRIVATE(o) pps_job_load_get_instance_private (o)

static void
pps_job_load_init (PpsJobLoad *job)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);

	priv->fd = -1;
}

static void
pps_job_load_dispose (GObject *object)
{
	PpsJobLoad *job = PPS_JOB_LOAD (object);
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);

	if (priv->fd != -1) {
		close (priv->fd);
		priv->fd = -1;
	}

	g_clear_pointer (&priv->mime_type, g_free);
	g_clear_pointer (&priv->uri, g_free);
	g_clear_pointer (&priv->password, g_free);
	g_clear_object (&priv->loaded_document);
	g_clear_object (&priv->document);

	G_OBJECT_CLASS (pps_job_load_parent_class)->dispose (object);
}

static int
pps_dupfd (int fd,
           GError **error)
{
#ifdef G_OS_WIN32
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	                     "File descriptor support is not available on Windows");
	return -1;
#else
	int new_fd;

	new_fd = fcntl (fd, F_DUPFD_CLOEXEC, 3);
	if (new_fd == -1) {
		int errsv = errno;
		g_set_error_literal (error, G_FILE_ERROR, g_file_error_from_errno (errsv),
		                     g_strerror (errsv));
	}

	return new_fd;
#endif
}

static void
pps_job_load_run (PpsJob *job)
{
	PpsJobLoad *job_load = PPS_JOB_LOAD (job);
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job_load);
	GError *error = NULL;

	g_debug ("running load job");

	if (priv->uri == NULL && priv->fd == -1) {
		g_set_error_literal (&error, G_FILE_ERROR, G_FILE_ERROR_BADF,
		                     "Either the URI or the FD must be set!");
		pps_job_failed_from_error (job, error);
		g_error_free (error);
		return;
	}

	/* This job may already have a document even if the job didn't complete
	   because, e.g., a password is required - if so, just reload rather than
	   creating a new instance */
	if (priv->document) {
		if (priv->password) {
			pps_document_security_set_password (PPS_DOCUMENT_SECURITY (priv->document),
			                                    priv->password);
		}

		pps_job_reset (job);

		if (priv->uri) {
			if (pps_document_load (priv->document, priv->uri, &error))
				g_set_object (&priv->loaded_document, priv->document);
		} else {
			/* We need to dup the FD since we may need to pass it again
			 * if the document is reloaded, as pps_document calls
			 * consume it.
			 */
			int fd = pps_dupfd (priv->fd, &error);
			if (fd != -1 && pps_document_load_fd (priv->document, fd, &error))
				g_set_object (&priv->loaded_document, priv->document);
		}
	} else {
		if (priv->uri) {
			priv->document = pps_document_factory_get_document (priv->uri, &error);
			if (priv->document != NULL &&
			    pps_document_load (priv->document, priv->uri, &error))
				priv->loaded_document = g_object_ref (priv->document);

		} else {
			priv->document = pps_document_factory_get_document_for_fd (priv->fd,
			                                                           priv->mime_type,
			                                                           &error);
			if (priv->document != NULL) {
				/* We need to dup the FD since we may need to pass it again
				 * if the document is reloaded, as pps_document_load calls
				 * consume it.
				 */
				int fd = pps_dupfd (priv->fd, &error);
				if (fd != -1 && pps_document_load_fd (priv->document,
				                                      fd,
				                                      &error))
					priv->loaded_document = g_object_ref (priv->document);
			}
		}
	}
	if (priv->document && priv->loaded_document)
		pps_document_setup_cache (priv->document);

	if (error) {
		pps_job_failed_from_error (job, error);
		g_error_free (error);
	} else {
		pps_job_succeeded (job);
	}
}

static void
pps_job_load_class_init (PpsJobLoadClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_load_dispose;
	job_class->run = pps_job_load_run;
}

PpsJob *
pps_job_load_new (void)
{
	PpsJobLoad *job;

	g_debug ("new load job");

	job = g_object_new (PPS_TYPE_JOB_LOAD, NULL);

	return PPS_JOB (job);
}

/**
 * pps_job_load_set_uri:
 * @job: an #PpsJobLoad
 * @uri: an uri representing a file
 *
 * It is an error to call this function if the file descriptor
 * is set for the job.
 *
 */
void
pps_job_load_set_uri (PpsJobLoad *job,
                      const gchar *uri)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);

	g_return_if_fail (PPS_IS_JOB_LOAD (job));
	g_return_if_fail (uri != NULL);
	g_return_if_fail (priv->fd == -1);

	g_debug ("load job set uri: %s", uri);

	g_free (priv->uri);
	priv->uri = g_strdup (uri);
}

/**
 * pps_job_load_set_fd:
 * @job: an #PpsJobLoad
 * @fd: a file descriptor
 * @mime_type: the mime type of the file descriptor
 * @error: (nullable): a location to store a #GError, or %NULL
 *
 * Sets @fd as the file descriptor in @job. If duplicating @fd fails,
 * returns %FALSE with @error filled in.
 *
 * It is an error to call this function if uri is set for the job
 *
 * Returns: %TRUE if the file descriptor could be set
 *
 * Since: 46.0
 */
gboolean
pps_job_load_set_fd (PpsJobLoad *job,
                     int fd,
                     const char *mime_type,
                     GError **error)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);

	g_return_val_if_fail (PPS_IS_JOB_LOAD (job), FALSE);
	g_return_val_if_fail (fd != -1, FALSE);
	g_return_val_if_fail (mime_type != NULL, FALSE);
	g_return_val_if_fail (priv->uri == NULL, FALSE);

	g_debug ("load job set fd: %d, mime: %s", fd, mime_type);

	g_free (priv->mime_type);
	priv->mime_type = g_strdup (mime_type);

	priv->fd = pps_dupfd (fd, error);
	return priv->fd != -1;
}

/**
 * pps_job_load_take_fd:
 * @job: an #PpsJobLoad
 * @fd: a file descriptor
 * @mime_type: the mime type of the file descriptor
 *
 * Sets @fd as the file descriptor in @job.
 * Note that @job takes ownership of @fd; you must not do anything
 * with it afterwards.
 *
 * It is an error to call this function if uri is set for the job
 *
 * Since: 46.0
 */
void
pps_job_load_take_fd (PpsJobLoad *job,
                      int fd,
                      const char *mime_type)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);

	g_return_if_fail (PPS_IS_JOB_LOAD (job));
	g_return_if_fail (fd != -1);
	g_return_if_fail (mime_type != NULL);
	g_return_if_fail (priv->uri == NULL);

	g_debug ("load job take fd: %d %s", fd, mime_type);

	g_free (priv->mime_type);
	priv->mime_type = g_strdup (mime_type);

	priv->fd = fd;
}

/**
 * pps_job_load_set_password:
 * @job: an #PpsJobLoad
 * @password: (nullable): the password
 *
 */
void
pps_job_load_set_password (PpsJobLoad *job, const gchar *password)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);
	g_return_if_fail (PPS_IS_JOB_LOAD (job));

	g_debug ("load job setting password");

	g_free (priv->password);
	priv->password = g_strdup (password);
}

const gchar *
pps_job_load_get_password (PpsJobLoad *job)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);
	g_return_val_if_fail (PPS_IS_JOB_LOAD (job), NULL);

	return priv->password;
}

void
pps_job_load_set_password_save (PpsJobLoad *job, GPasswordSave save)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);
	g_return_if_fail (PPS_IS_JOB_LOAD (job));

	g_debug ("load job setting password save");

	priv->password_save = save;
}

GPasswordSave
pps_job_load_get_password_save (PpsJobLoad *job)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);
	g_return_val_if_fail (PPS_IS_JOB_LOAD (job), G_PASSWORD_SAVE_NEVER);

	return priv->password_save;
}

/**
 * pps_job_load_get_loaded_document:
 * @job: an #PpsJobLoad
 *
 * Returns: (nullable) (transfer full): The loaded document, if available
 *
 * Since: 46.0
 */
PpsDocument *
pps_job_load_get_loaded_document (PpsJobLoad *job)
{
	PpsJobLoadPrivate *priv = JOB_LOAD_GET_PRIVATE (job);
	g_return_val_if_fail (PPS_IS_JOB_LOAD (job), NULL);

	if (!priv->loaded_document)
		return NULL;

	return g_object_ref (priv->loaded_document);
}

/* PpsJobSave */

typedef struct _PpsJobSavePrivate {
	gchar *uri;
	gchar *document_uri;
} PpsJobSavePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobSave, pps_job_save, PPS_TYPE_JOB)

#define JOB_SAVE_GET_PRIVATE(o) pps_job_save_get_instance_private (o)

static void
pps_job_save_init (PpsJobSave *job)
{
}

static void
pps_job_save_dispose (GObject *object)
{
	PpsJobSave *job_save = PPS_JOB_SAVE (object);
	PpsJobSavePrivate *priv = JOB_SAVE_GET_PRIVATE (job_save);

	g_debug ("disposing job save: uri: %s", priv->uri);

	g_clear_pointer (&priv->uri, g_free);
	g_clear_pointer (&priv->document_uri, g_free);

	G_OBJECT_CLASS (pps_job_save_parent_class)->dispose (object);
}

static void
pps_job_save_run (PpsJob *job)
{
	PpsJobSave *job_save = PPS_JOB_SAVE (job);
	PpsJobSavePrivate *priv = JOB_SAVE_GET_PRIVATE (job_save);
	gint fd;
	g_autofree gchar *tmp_filename = NULL;
	g_autofree gchar *tmp_uri = NULL;
	g_autoptr (GError) error = NULL;

	g_debug ("running save job: uri: %s, document_uri: %s",
	         priv->uri, priv->document_uri);

	fd = pps_mkstemp ("saveacopy.XXXXXX", &tmp_filename, &error);
	if (fd == -1) {
		pps_job_failed_from_error (job, error);
		return;
	}
	close (fd);

	/* Save document to temp filename */
	tmp_uri = g_filename_to_uri (tmp_filename, NULL, &error);
	if (tmp_uri != NULL) {
		pps_document_save (pps_job_get_document (job), tmp_uri, &error);
	}

	if (error) {
		pps_job_failed_from_error (job, error);
		return;
	}

	/* If original document was compressed,
	 * compress it again before saving
	 */
	if (g_object_get_data (G_OBJECT (pps_job_get_document (job)), "uri-uncompressed")) {
		PpsCompressionType ctype = PPS_COMPRESSION_NONE;
		const gchar *ext;
		gchar *uri_comp;

		ext = g_strrstr (priv->document_uri, ".gz");
		if (ext && g_ascii_strcasecmp (ext, ".gz") == 0)
			ctype = PPS_COMPRESSION_GZIP;

		ext = g_strrstr (priv->document_uri, ".bz2");
		if (ext && g_ascii_strcasecmp (ext, ".bz2") == 0)
			ctype = PPS_COMPRESSION_BZIP2;

		uri_comp = pps_file_compress (tmp_uri, ctype, &error);
		g_unlink (tmp_filename);

		if (!uri_comp || error) {
			tmp_uri = NULL;
		} else {
			tmp_uri = uri_comp;
		}
	}

	if (error) {
		pps_job_failed_from_error (job, error);
		return;
	}

	if (!tmp_uri)
		return;

	pps_xfer_uri_simple (tmp_uri, priv->uri, &error);
	pps_tmp_uri_unlink (tmp_uri);

	/* Copy the metadata from the original file */
	if (!error) {
		/* Ignore errors here. Failure to copy metadata is not a hard error */
		pps_file_copy_metadata (priv->document_uri, priv->uri, NULL);
	}

	if (error) {
		pps_job_failed_from_error (job, error);
	} else {
		pps_job_succeeded (job);
	}
}

static void
pps_job_save_class_init (PpsJobSaveClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_save_dispose;
	job_class->run = pps_job_save_run;
}

PpsJob *
pps_job_save_new (PpsDocument *document,
                  const gchar *uri,
                  const gchar *document_uri)
{
	PpsJobSave *job;
	PpsJobSavePrivate *priv;

	g_debug ("new save job: uri: %s, document_uri: %s", uri, document_uri);

	job = g_object_new (PPS_TYPE_JOB_SAVE,
	                    "document", document,
	                    NULL);

	priv = JOB_SAVE_GET_PRIVATE (job);

	priv->uri = g_strdup (uri);
	priv->document_uri = g_strdup (document_uri);

	return PPS_JOB (job);
}

const gchar *
pps_job_save_get_uri (PpsJobSave *job_save)
{
	PpsJobSavePrivate *priv = JOB_SAVE_GET_PRIVATE (job_save);

	return priv->uri;
}

/* PpsJobFind */
static void
pps_job_find_init (PpsJobFind *job)
{
}

static void
pps_job_find_dispose (GObject *object)
{
	PpsJobFind *job = PPS_JOB_FIND (object);

	g_clear_pointer (&job->text, g_free);

	if (job->pages) {
		gint i;

		for (i = 0; i < job->n_pages; i++) {
			g_clear_list (&job->pages[i], (GDestroyNotify) pps_find_rectangle_free);
		}

		g_clear_pointer (&job->pages, g_free);
	}

	(*G_OBJECT_CLASS (pps_job_find_parent_class)->dispose) (object);
}

static void
pps_job_find_run (PpsJob *job)
{
	PpsJobFind *job_find = PPS_JOB_FIND (job);
	PpsDocumentFind *find = PPS_DOCUMENT_FIND (pps_job_get_document (job));
	PpsPage *pps_page;
	GList *matches;
	gint n_pages, current_page;

	g_debug ("running find job");

	n_pages = job_find->n_pages;
	current_page = job_find->start_page;

	while (n_pages-- > 0) {
		if (g_cancellable_is_cancelled (pps_job_get_cancellable (job)))
			return;

		pps_page = pps_document_get_page (pps_job_get_document (job), current_page);
		matches = pps_document_find_find_text (find, pps_page, job_find->text,
		                                       job_find->options);
		g_object_unref (pps_page);

		job_find->has_results |= (matches != NULL);

		job_find->pages[current_page] = matches;
		g_signal_emit (job_find, job_find_signals[FIND_UPDATED], 0, current_page);

		current_page = (current_page + 1) % job_find->n_pages;
	}

	pps_job_succeeded (job);
}

static void
pps_job_find_class_init (PpsJobFindClass *class)
{
	PpsJobClass *job_class = PPS_JOB_CLASS (class);
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);

	job_class->run = pps_job_find_run;
	gobject_class->dispose = pps_job_find_dispose;

	job_find_signals[FIND_UPDATED] =
	    g_signal_new ("updated",
	                  PPS_TYPE_JOB_FIND,
	                  G_SIGNAL_RUN_LAST,
	                  G_STRUCT_OFFSET (PpsJobFindClass, updated),
	                  NULL, NULL,
	                  g_cclosure_marshal_VOID__INT,
	                  G_TYPE_NONE,
	                  1, G_TYPE_INT);
}

PpsJob *
pps_job_find_new (PpsDocument *document,
                  gint start_page,
                  gint n_pages,
                  const gchar *text,
                  PpsFindOptions options)
{
	PpsJobFind *job;

	g_debug ("new find job");

	job = g_object_new (PPS_TYPE_JOB_FIND,
	                    "document", document,
	                    NULL);

	job->start_page = start_page;
	job->n_pages = n_pages;
	job->pages = g_new0 (GList *, n_pages);
	job->text = g_strdup (text);
	job->has_results = FALSE;
	job->options = options;

	return PPS_JOB (job);
}

/**
 * pps_job_find_get_options:
 * @job:
 *
 * Returns: the job's find options
 */
PpsFindOptions
pps_job_find_get_options (PpsJobFind *job)
{
	return job->options;
}

/**
 * pps_job_find_get_n_main_results:
 * @job: an #PpsJobFind job
 * @page: number of the page we want to count its match results.
 *
 * This is similar to pps_job_find_get_n_results() but it takes
 * care to treat any multi-line matches as being only one result.
 *
 * Returns: total number of match results in @page
 */
gint
pps_job_find_get_n_main_results (PpsJobFind *job,
                                 gint page)
{
	GList *l;
	int n = 0;

	for (l = job->pages[page]; l; l = l->next) {
		if (!((PpsFindRectangle *) l->data)->next_line)
			n++;
	}

	return n;
}

gboolean
pps_job_find_has_results (PpsJobFind *job)
{
	return job->has_results;
}

/**
 * pps_job_find_get_results: (skip)
 * @job: an #PpsJobFind
 *
 * Returns: a #GList of #GList<!-- -->s containing #PpsFindRectangle<!-- -->s
 */
GList **
pps_job_find_get_results (PpsJobFind *job)
{
	return job->pages;
}

/* PpsJobLayers */
typedef struct {
	GListModel *model;
} PpsJobLayersPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobLayers, pps_job_layers, PPS_TYPE_JOB)

#define JOB_LAYERS_GET_PRIVATE(o) pps_job_layers_get_instance_private (o)

static void
pps_job_layers_init (PpsJobLayers *job)
{
}

static void
pps_job_layers_dispose (GObject *object)
{
	PpsJobLayersPrivate *priv = JOB_LAYERS_GET_PRIVATE (PPS_JOB_LAYERS (object));

	g_clear_object (&priv->model);

	G_OBJECT_CLASS (pps_job_layers_parent_class)->dispose (object);
}

static void
pps_job_layers_run (PpsJob *job)
{
	PpsJobLayersPrivate *priv = JOB_LAYERS_GET_PRIVATE (PPS_JOB_LAYERS (job));

	g_debug ("running layers job");

	priv->model = pps_document_layers_get_layers (PPS_DOCUMENT_LAYERS (pps_job_get_document (job)));

	pps_job_succeeded (job);
}

static void
pps_job_layers_class_init (PpsJobLayersClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_layers_dispose;
	job_class->run = pps_job_layers_run;
}

/**
 * pps_job_layers_get_model:
 * @job_layers: a #PpsJobLayers
 *
 * Returns: (transfer full): A #GListModel contains the result
 */
GListModel *
pps_job_layers_get_model (PpsJobLayers *job)
{
	PpsJobLayersPrivate *priv = JOB_LAYERS_GET_PRIVATE (job);

	return g_steal_pointer (&priv->model);
}

PpsJob *
pps_job_layers_new (PpsDocument *document)
{
	PpsJob *job;

	g_debug ("new layers job");

	job = g_object_new (PPS_TYPE_JOB_LAYERS,
	                    "document", document,
	                    NULL);

	return job;
}

/* PpsJobExport */
typedef struct {
	gint page;
	PpsRenderContext *rc;
} PpsJobExportPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobExport, pps_job_export, PPS_TYPE_JOB)

#define JOB_EXPORT_GET_PRIVATE(o) pps_job_export_get_instance_private (o)

static void
pps_job_export_init (PpsJobExport *job)
{
	PpsJobExportPrivate *priv = JOB_EXPORT_GET_PRIVATE (job);

	priv->page = -1;
}

static void
pps_job_export_dispose (GObject *object)
{
	PpsJobExportPrivate *priv = JOB_EXPORT_GET_PRIVATE (PPS_JOB_EXPORT (object));

	g_clear_object (&priv->rc);

	G_OBJECT_CLASS (pps_job_export_parent_class)->dispose (object);
}

static void
pps_job_export_run (PpsJob *job)
{
	PpsJobExportPrivate *priv = JOB_EXPORT_GET_PRIVATE (PPS_JOB_EXPORT (job));
	PpsPage *pps_page;

	g_assert (priv->page != -1);

	g_debug ("running export job");

	pps_page = pps_document_get_page (pps_job_get_document (job), priv->page);
	if (priv->rc) {
		pps_job_reset (job);

		pps_render_context_set_page (priv->rc, pps_page);
	} else {
		priv->rc = pps_render_context_new (pps_page, 0, 1.0, PPS_RENDER_ANNOTS_ALL);
	}
	g_object_unref (pps_page);

	pps_file_exporter_do_page (PPS_FILE_EXPORTER (pps_job_get_document (job)), priv->rc);

	pps_job_succeeded (job);
}

static void
pps_job_export_class_init (PpsJobExportClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_export_dispose;
	job_class->run = pps_job_export_run;
}

PpsJob *
pps_job_export_new (PpsDocument *document)
{
	PpsJob *job;

	g_debug ("new export job");

	job = g_object_new (PPS_TYPE_JOB_EXPORT,
	                    "document", document,
	                    NULL);

	return job;
}

void
pps_job_export_set_page (PpsJobExport *job,
                         gint page)
{
	PpsJobExportPrivate *priv = JOB_EXPORT_GET_PRIVATE (job);

	priv->page = page;
}

/* PpsJobPrint */
typedef struct _PpsJobPrintPrivate {
	gint page;
	cairo_t *cr;
} PpsJobPrintPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobPrint, pps_job_print, PPS_TYPE_JOB)

#define JOB_PRINT_GET_PRIVATE(o) pps_job_print_get_instance_private (o)

static void
pps_job_print_init (PpsJobPrint *job)
{
	PpsJobPrintPrivate *priv = JOB_PRINT_GET_PRIVATE (job);

	priv->page = -1;
}

static void
pps_job_print_dispose (GObject *object)
{
	PpsJobPrintPrivate *priv = JOB_PRINT_GET_PRIVATE (PPS_JOB_PRINT (object));

	g_clear_pointer (&priv->cr, cairo_destroy);

	G_OBJECT_CLASS (pps_job_print_parent_class)->dispose (object);
}

static void
pps_job_print_run (PpsJob *job)
{
	PpsJobPrintPrivate *priv = JOB_PRINT_GET_PRIVATE (PPS_JOB_PRINT (job));
	PpsPage *pps_page;
	cairo_status_t cr_status;

	g_assert (priv->page != -1);
	g_assert (priv->cr != NULL);

	g_debug ("running print job");

	pps_job_reset (job);

	pps_page = pps_document_get_page (pps_job_get_document (job), priv->page);
	pps_document_print_print_page (PPS_DOCUMENT_PRINT (pps_job_get_document (job)),
	                               pps_page, priv->cr);
	g_object_unref (pps_page);

	if (g_cancellable_is_cancelled (pps_job_get_cancellable (job)))
		return;

	cr_status = cairo_status (priv->cr);
	if (cr_status == CAIRO_STATUS_SUCCESS) {
		pps_job_succeeded (job);
	} else {
		pps_job_failed (job,
		                GTK_PRINT_ERROR,
		                GTK_PRINT_ERROR_GENERAL,
		                _ ("Failed to print page %d: %s"),
		                priv->page,
		                cairo_status_to_string (cr_status));
	}
}

static void
pps_job_print_class_init (PpsJobPrintClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_print_dispose;
	job_class->run = pps_job_print_run;
}

PpsJob *
pps_job_print_new (PpsDocument *document)
{
	PpsJob *job;

	g_debug ("new print job");

	job = g_object_new (PPS_TYPE_JOB_PRINT,
	                    "document", document,
	                    NULL);

	return job;
}

void
pps_job_print_set_page (PpsJobPrint *job,
                        gint page)
{
	PpsJobPrintPrivate *priv = JOB_PRINT_GET_PRIVATE (job);

	priv->page = page;
}

void
pps_job_print_set_cairo (PpsJobPrint *job,
                         cairo_t *cr)
{
	PpsJobPrintPrivate *priv = JOB_PRINT_GET_PRIVATE (job);

	if (priv->cr == cr)
		return;

	if (priv->cr)
		cairo_destroy (priv->cr);
	priv->cr = cr ? cairo_reference (cr) : NULL;
}

/* PpsJobSignatures */
typedef struct {
	GList *signatures;
} PpsJobSignaturesPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PpsJobSignatures, pps_job_signatures, PPS_TYPE_JOB)

#define JOB_SIGNATURES_GET_PRIVATE(o) pps_job_signatures_get_instance_private (o)

static void
pps_job_signatures_init (PpsJobSignatures *job)
{
}

static void
pps_job_signatures_dispose (GObject *object)
{
	PpsJobSignaturesPrivate *priv = JOB_SIGNATURES_GET_PRIVATE (PPS_JOB_SIGNATURES (object));

	g_clear_list (&priv->signatures, g_object_unref);

	G_OBJECT_CLASS (pps_job_signatures_parent_class)->dispose (object);
}

static void
pps_job_signatures_run (PpsJob *job)
{
	PpsJobSignaturesPrivate *priv = JOB_SIGNATURES_GET_PRIVATE (PPS_JOB_SIGNATURES (job));

	g_debug ("running signatures job");

	priv->signatures =
	    pps_document_signatures_get_signatures (PPS_DOCUMENT_SIGNATURES ((pps_job_get_document (job))));

	pps_job_succeeded (job);
}

static void
pps_job_signatures_class_init (PpsJobSignaturesClass *class)
{
	GObjectClass *oclass = G_OBJECT_CLASS (class);
	PpsJobClass *job_class = PPS_JOB_CLASS (class);

	oclass->dispose = pps_job_signatures_dispose;
	job_class->run = pps_job_signatures_run;
}

PpsJob *
pps_job_signatures_new (PpsDocument *document)
{
	PpsJob *job;

	g_debug ("new signatures job");

	job = g_object_new (PPS_TYPE_JOB_SIGNATURES, "document", document, NULL);

	return job;
}

/**
 * pps_job_signatures_get_signatures:
 * @self: a #PpsJobSignatures
 *
 * Get all available signatures
 *
 * Returns: (element-type PpsSignature) (transfer none): all available signatures
 */
GList *
pps_job_signatures_get_signatures (PpsJobSignatures *self)
{
	PpsJobSignaturesPrivate *priv = JOB_SIGNATURES_GET_PRIVATE (self);

	return priv->signatures;
}
