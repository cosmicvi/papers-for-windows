libdocument_inc = include_directories('.')

include_subdir = join_paths(pps_include_subdir, 'libdocument')

headers = files(
  'pps-annotation.h',
  'pps-attachment.h',
  'pps-certificate-info.h',
  'pps-document-annotations.h',
  'pps-document-attachments.h',
  'pps-document-factory.h',
  'pps-document-find.h',
  'pps-document-fonts.h',
  'pps-document-forms.h',
  'pps-document-images.h',
  'pps-document-info.h',
  'pps-document-layers.h',
  'pps-document-links.h',
  'pps-document-media.h',
  'pps-document-misc.h',
  'pps-document-print.h',
  'pps-document-security.h',
  'pps-document-signatures.h',
  'pps-document-text.h',
  'pps-document-transition.h',
  'pps-document.h',
  'pps-file-exporter.h',
  'pps-file-helpers.h',
  'pps-font-description.h',
  'pps-form-field.h',
  'pps-image.h',
  'pps-init.h',
  'pps-layer.h',
  'pps-link-action.h',
  'pps-link-dest.h',
  'pps-link.h',
  'pps-macros.h',
  'pps-mapping-list.h',
  'pps-media.h',
  'pps-outlines.h',
  'pps-page.h',
  'pps-render-context.h',
  'pps-selection.h',
  'pps-signature.h',
  'pps-transition-effect.h',
)

install_headers(
  headers,
  subdir: include_subdir,
)

sources = files(
  'pps-annotation.c',
  'pps-attachment.c',
  'pps-backend-info.c',
  'pps-certificate-info.c',
  'pps-document.c',
  'pps-document-annotations.c',
  'pps-document-attachments.c',
  'pps-document-factory.c',
  'pps-document-find.c',
  'pps-document-fonts.c',
  'pps-document-forms.c',
  'pps-document-images.c',
  'pps-document-info.c',
  'pps-document-layers.c',
  'pps-document-links.c',
  'pps-document-media.c',
  'pps-document-misc.c',
  'pps-document-print.c',
  'pps-document-security.c',
  'pps-document-signatures.c',
  'pps-document-text.c',
  'pps-document-transition.c',
  'pps-file-exporter.c',
  'pps-file-helpers.c',
  'pps-font-description.c',
  'pps-form-field.c',
  'pps-image.c',
  'pps-init.c',
  'pps-layer.c',
  'pps-link-action.c',
  'pps-link.c',
  'pps-link-dest.c',
  'pps-mapping-list.c',
  'pps-media.c',
  'pps-outlines.c',
  'pps-page.c',
  'pps-render-context.c',
  'pps-selection.c',
  'pps-signature.c',
  'pps-transition-effect.c',
  'pps-xmp.c',
  'pps-xmp.h',
)

version_conf = configuration_data()
version_conf.set('PPS_MAJOR_VERSION', pps_major_version)
version_conf.set('PPS_MINOR_VERSION', pps_minor_version)

version = 'pps-version.h'

version_header = configure_file(
  input: version + '.in',
  output: version,
  configuration: version_conf,
  install: true,
  install_dir: join_paths(pps_includedir, include_subdir),
)

enum_types = 'pps-document-type-builtins'
enum_header_prefix = '''
#if !defined (__PPS_PAPERS_DOCUMENT_H_INSIDE__) && !defined (PAPERS_COMPILATION)
#error "Only <papers-document.h> can be included directly."
#endif

#include "pps-macros.h"
'''

enum_sources = gnome.mkenums_simple(
  enum_types,
  sources: headers,
  header_prefix: enum_header_prefix,
  decorator: 'PPS_PUBLIC',
  install_header: true,
  install_dir: join_paths(pps_includedir, include_subdir),
)

libdocument_deps = [
  cairo_dep,
  gdk_pixbuf_dep,
  gio_dep,
  glib_dep,
  gtk_dep,
  gmodule_dep,
  m_dep,
  zlib_dep,
  libsysprof_capture_dep,
]

if exempi_dep.found()
  libdocument_deps += exempi_dep
endif

cflags = [
  '-DG_LOG_DOMAIN="PapersDocument"',
  '-DPPS_BACKENDSDIR="@0@"'.format(pps_backendsdir),
  '-DPPS_BACKENDSBINARYVERSION="@0@"'.format(pps_backends_binary_version),
  '-DPAPERS_COMPILATION',
]

libppsdocument = shared_library(
  'ppsdocument-' + pps_api_version,
  version: pps_document_version,
  sources: sources + enum_sources + [version_header],
  include_directories: top_inc,
  dependencies: libdocument_deps,
  c_args: cflags,
  install: true,
  gnu_symbol_visibility: 'hidden',
)

libppsdocument_dep = declare_dependency(
  sources: headers + [enum_sources[1]] + [version_header],
  include_directories: libdocument_inc,
  dependencies: libdocument_deps,
  link_with: libppsdocument,
)

pkg.generate(
  libppsdocument,
  filebase: 'papers-document-' + pps_api_version,
  name: 'Papers Document',
  description: 'Papers document backend library',
  subdirs: pps_include_subdir,
  requires: [
    gio_dep,
    '@0@ @1@'.format(gtk_dep.name(), gtk_req_version),
  ],
  variables: [
    'exec_prefix=${prefix}',
    'backenddir=' + join_paths('${libdir}', pps_backends_subdir),
  ],
)

install_headers(
  files('papers-document.h'),
  subdir: pps_include_subdir,
)

# GObject Introspection
if gobject_introspection_dep.found()
  incs = [
    'Gdk-4.0',
    'GdkPixbuf-2.0',
    'Gio-2.0',
    'GLib-2.0',
    'Gtk-4.0',
    'Pango-1.0',
  ]

  libppsdocument_gir = gnome.generate_gir(
    libppsdocument,
    sources: sources + headers + [enum_sources[1], version_header],
    doc_format: 'gi-docgen',
    includes: incs,
    nsversion: pps_api_version,
    namespace: 'PapersDocument',
    identifier_prefix: pps_code_prefix,
    symbol_prefix: pps_code_prefix.to_lower(),
    export_packages: 'papers-document-' + pps_api_version,
    extra_args: '-DPAPERS_COMPILATION',
    install: true,
  )
endif

subdir('backend')
