// SPDX-License-Identifier: LGPL-2.1-or-later
/* this file is part of papers, a gnome document viewer
 *
 * Copyright © 2009 Christian Persch
 */

#include <config.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#ifdef HAVE_EXEMPI
#include <exempi/xmp.h>
#endif

#include "pps-document-factory.h"
#include "pps-file-helpers.h"
#include "pps-init.h"

static int pps_init_count;

/**
 * pps_init:
 *
 * Initializes the papers document library, and binds the papers
 * gettext domain.
 *
 * You must call this before calling any other function in the papers
 * document library.
 *
 * Returns: %TRUE if any backends were found; %FALSE otherwise
 */
gboolean
pps_init (void)
{
	static gboolean have_backends;

	if (pps_init_count++ > 0)
		return have_backends;

	/* set up translation catalog */
	bindtextdomain (GETTEXT_PACKAGE, PPS_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

#ifdef HAVE_EXEMPI
	xmp_init ();
#endif
	gdk_pixbuf_init_modules (EXTRA_GDK_PIXBUF_LOADERS_DIR, NULL);
	_pps_file_helpers_init ();
	have_backends = _pps_document_factory_init ();

	return have_backends;
}

/**
 * pps_shutdown:
 *
 * Shuts the papers document library down.
 */
void
pps_shutdown (void)
{
	g_assert (_pps_is_initialized ());

	if (--pps_init_count > 0)
		return;

#ifdef HAVE_EXEMPI
	xmp_terminate ();
#endif
	_pps_document_factory_shutdown ();
	_pps_file_helpers_shutdown ();
}

/*
 * _pps_is_initialized:
 *
 * Returns: %TRUE if the papers document library has been initialized
 */
gboolean
_pps_is_initialized (void)
{
	return pps_init_count > 0;
}
