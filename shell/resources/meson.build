resources_conf = configuration_data()
resources_conf.set('APP_ID', app_id)

blueprint_sources = [
  'pps-annotation-properties-dialog.blp',
  'pps-document-view.blp',
  'pps-find-sidebar.blp',
  'pps-loader-view.blp',
  'pps-page-selector.blp',
  'pps-password-dialog.blp',
  'pps-password-view.blp',
  'pps-progress-message-area.blp',
  'pps-properties-fonts.blp',
  'pps-properties-license.blp',
  'pps-properties-signatures.blp',
  'pps-properties-window.blp',
  'pps-search-box.blp',
  'pps-shortcuts-dialog.blp',
  'pps-sidebar-annotations-row.blp',
  'pps-sidebar-annotations.blp',
  'pps-sidebar-attachments.blp',
  'pps-sidebar-layers.blp',
  'pps-sidebar-links.blp',
  'pps-sidebar-thumbnails.blp',
  'pps-sidebar.blp',
  'pps-signature-drawing-widget.blp',
  'pps-sign-manually-dialog.blp',
  'pps-visual-signature-chooser.blp',
  'pps-window.blp',
]

blueprint_output = []

foreach source : blueprint_sources
  blueprint_output += source.replace('.blp', '.ui')
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

papers_resources = gnome.compile_resources(
  'pps-resources',
  configure_file(
    input: 'papers.gresource.xml.in',
    output: 'papers.gresource.xml',
    configuration: resources_conf,
  ),
  source_dir: [data_dir, data_build_dir],
  dependencies: [metainfo_file, blueprints],
  gresource_bundle: true,
)[0]
