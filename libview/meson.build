libview_inc = include_directories('.')

# For MacOS build
context_inc = include_directories('context')
factory_inc = include_directories('factory')

include_subdir = join_paths(pps_include_subdir, 'libview')

context_headers = files(
  'context/pps-annotations-context.h',
  'context/pps-attachment-context.h',
  'context/pps-annotation-model.h',
  'context/pps-document-model.h',
  'context/pps-history.h',
  'context/pps-metadata.h',
  'context/pps-search-context.h',
  'context/pps-search-result.h',
  'context/pps-undo-context.h',
  'context/pps-undo-handler.h'
)

factory_headers = files (
  'factory/pps-element-widget-factory.h',
  'factory/pps-form-widget-factory.h',
  'factory/pps-annotation-widget-factory.h'
)

headers = files(
  'pps-css-utils.h',
  'pps-job-scheduler.h',
  'pps-job.h',
  'pps-jobs.h',
  'pps-print-operation.h',
  'pps-view-presentation.h',
  'pps-view.h',
)

install_headers(
  headers,
  subdir: include_subdir,
)

include_context_subdir = join_paths (include_subdir, 'context')

install_headers(
  context_headers,
  subdir: include_context_subdir,
)

headers += context_headers

sources = files(
  'context/pps-annotations-context.c',
  'context/pps-attachment-context.c',
  'context/pps-annotation-model.c',
  'context/pps-document-model.c',
  'context/pps-history.c',
  'context/pps-metadata.c',
  'context/pps-search-context.c',
  'context/pps-search-result.c',
  'context/pps-undo-context.c',
  'context/pps-undo-handler.c',
  'factory/pps-annotation-widget-factory.c',
  'factory/pps-element-widget-factory.c',
  'factory/pps-form-widget-factory.c',
  'pps-annotation-window.c',
  'pps-colors.c',
  'pps-debug.c',
  'pps-job.c',
  'pps-jobs.c',
  'pps-job-scheduler.c',
  'pps-overlay.c',
  'pps-form-overlay.c',
  'pps-page-cache.c',
  'pps-pixbuf-cache.c',
  'pps-print-operation.c',
  'pps-view.c',
  'pps-view-cursor.c',
  'pps-view-page.c',
  'pps-view-presentation.c',
  'pps-css-utils.c',
  'pps-annotation-overlay.c',
  'pps-annotation-layer.c',
  'pps-annotation-layer-objects.c',
  'pps-annotation-layer-ink.c'
)

marshal = 'pps-view-marshal'

marshal_sources = gnome.genmarshal(
  marshal,
  sources: marshal + '.list',
  prefix: 'pps_view_marshal',
  internal: true,
  extra_args: '--prototypes',
)

enum_types = 'pps-view-type-builtins'
enum_header_prefix = '''
#if !defined (__PPS_PAPERS_VIEW_H_INSIDE__) && !defined (PAPERS_COMPILATION)
#error "Only <papers-view.h> can be included directly."
#endif

#include <papers-document.h>
'''

enum_sources = gnome.mkenums_simple(
  enum_types,
  sources: headers,
  header_prefix: enum_header_prefix,
  decorator: 'PPS_PUBLIC',
  install_header: true,
  install_dir: join_paths(pps_includedir, include_subdir),
)

libview_deps = [
  gthread_dep,
  libppsdocument_dep,
  libaw_dep,
  libspelling_dep,
  m_dep,
]

cflags = [
  '-DG_LOG_DOMAIN="PapersView"',
  '-DPAPERS_COMPILATION'
]

blueprint_sources = [
  'pps-view-presentation.blp',
  'pps-view.blp',
]

blueprint_output = []

foreach source : blueprint_sources
  blueprint_output = source.replace('.blp', '.ui')
endforeach

blueprints = custom_target(
  'blueprints',
  input: blueprint_sources,
  output: blueprint_output,
  command: [
    blueprint_compiler,
    'batch-compile',
    '@OUTDIR@',
    '@CURRENT_SOURCE_DIR@',
    '@INPUT@',
  ],
)

sources += gnome.compile_resources(
  'pps-view-resources',
  'libview.gresource.xml',
  source_dir: data_dir,
  c_name: 'pps_view',
  dependencies: blueprints,
)


libppsview = shared_library(
  'ppsview-' + pps_api_version,
  version: pps_view_version,
  sources: sources + marshal_sources + enum_sources,
  include_directories: [top_inc, context_inc, factory_inc],
  dependencies: libview_deps,
  c_args: cflags,
  install: true,
  gnu_symbol_visibility: 'hidden',
)

libppsview_dep = declare_dependency(
  sources: headers + [enum_sources[1]],
  include_directories: libview_inc,
  dependencies: libview_deps,
  link_with: libppsview,
)

pkg.generate(
  libppsview,
  filebase: 'papers-view-' + pps_api_version,
  name: 'Papers View',
  description: 'Papers view library',
  subdirs: pps_include_subdir,
  requires: [
    'papers-document-@0@ = @1@'.format(pps_api_version, pps_version),
    gthread_dep,
  ],
  requires_private: [
    '@0@ @1@'.format(libaw_dep.name(), libaw_req_version),
  ],
  variables: 'exec_prefix=${prefix}',
)

install_headers(
  files('papers-view.h'),
  subdir: pps_include_subdir,
)

# GObject Introspection
if gobject_introspection_dep.found()
  incs = [
    'Gdk-4.0',
    'GdkPixbuf-2.0',
    'Gio-2.0',
    'GLib-2.0',
    'GObject-2.0',
    'Gtk-4.0',
    libppsdocument_gir[0],
  ]

  libppsview_gir = gnome.generate_gir(
    [libppsview, libppsdocument],
    sources: sources + headers + [enum_sources[1]],
    doc_format: 'gi-docgen',
    includes: incs,
    nsversion: pps_api_version,
    namespace: 'PapersView',
    identifier_prefix: pps_code_prefix,
    symbol_prefix: pps_code_prefix.to_lower(),
    export_packages: 'papers-view-' + pps_api_version,
    extra_args: '-DPAPERS_COMPILATION',
    install: true,
  )
endif
