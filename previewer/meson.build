previewer_sources = files(
  'pps-previewer-window.c',
  'pps-previewer-window.h',
  'pps-page-selector.c',
  'pps-page-selector.h',
  'pps-previewer.c',
)

blueprint_sources = [
  'pps-page-selector.blp',
  'pps-previewer-window.blp',
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

previewer_sources += gnome.compile_resources(
  'pps-previewer-resources',
  'previewer.gresource.xml',
  source_dir: data_dir,
  c_name: 'pps_previewer',
  dependencies: blueprints,
)

if host_machine.system() == 'windows'
  win = import('windows')
  previewer_win_res = win.compile_resources(
    'papers-previewer.rc',
    depend_files: files('../shell/resources/papers.ico'),
  )
  previewer_sources += previewer_win_res
endif


previewer_deps = [
  libppsdocument_dep,
  libppsview_dep,
  libaw_dep
]

if enable_gtk_unix_print
  previewer_deps += gtk_unix_print_dep
endif

executable(
  'papers-previewer',
  previewer_sources,
  include_directories: top_inc,
  dependencies: previewer_deps,
  link_args: common_ldflags,
  install: true,
)
