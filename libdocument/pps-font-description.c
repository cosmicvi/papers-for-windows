// SPDX-License-Identifier: GPL-2.0-or-later
/* this file is part of papers, a gnome document viewer
 *
 *  Copyright (C) 2024 Pablo Correa Gomez <ablocorrea@hotmail.com>
 */

#include <config.h>

#include "pps-font-description.h"

struct _PpsFontDescriptionPrivate {
	char *name;
	char *details;
};

typedef struct _PpsFontDescriptionPrivate PpsFontDescriptionPrivate;
#define GET_PRIVATE(o) pps_font_description_get_instance_private (o)

G_DEFINE_TYPE_WITH_PRIVATE (PpsFontDescription, pps_font_description, G_TYPE_OBJECT);

enum {
	PROP_0,
	PROP_NAME,
	PROP_DETAILS,
};

static void
pps_font_description_init (PpsFontDescription *self)
{
}

static void
pps_font_description_set_property (GObject *object,
                                   guint prop_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
	PpsFontDescriptionPrivate *priv = GET_PRIVATE (PPS_FONT_DESCRIPTION (object));

	switch (prop_id) {
	case PROP_NAME: {
		const char *name = g_value_get_string (value);

		/* PDF font names are not required to be UTF-8. */
		g_free (priv->name);
		priv->name = name != NULL ? g_utf8_make_valid (name, -1) : NULL;
		break;
	}
	case PROP_DETAILS: {
		const char *details = g_value_get_string (value);

		g_free (priv->details);
		priv->details = details != NULL ? g_utf8_make_valid (details, -1) : NULL;
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pps_font_description_get_property (GObject *object,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
	PpsFontDescriptionPrivate *priv = GET_PRIVATE (PPS_FONT_DESCRIPTION (object));

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_DETAILS:
		g_value_set_string (value, priv->details);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pps_font_description_dispose (GObject *object)
{
	PpsFontDescriptionPrivate *priv = GET_PRIVATE (PPS_FONT_DESCRIPTION (object));

	g_clear_pointer (&priv->name, g_free);
	g_clear_pointer (&priv->details, g_free);

	G_OBJECT_CLASS (pps_font_description_parent_class)->dispose (object);
}

static void
pps_font_description_class_init (PpsFontDescriptionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = pps_font_description_set_property;
	object_class->get_property = pps_font_description_get_property;
	object_class->dispose = pps_font_description_dispose;

	g_object_class_install_property (object_class,
	                                 PROP_NAME,
	                                 g_param_spec_string ("name",
	                                                      "name",
	                                                      "The name of the font",
	                                                      NULL,
	                                                      G_PARAM_READWRITE |
	                                                          G_PARAM_STATIC_STRINGS |
	                                                          G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_DETAILS,
	                                 g_param_spec_string ("details",
	                                                      "details",
	                                                      "The details of the font",
	                                                      NULL,
	                                                      G_PARAM_READWRITE |
	                                                          G_PARAM_STATIC_STRINGS |
	                                                          G_PARAM_CONSTRUCT_ONLY));
}

PpsFontDescription *
pps_font_description_new (void)
{
	return PPS_FONT_DESCRIPTION (g_object_new (PPS_TYPE_FONT_DESCRIPTION, NULL));
}
