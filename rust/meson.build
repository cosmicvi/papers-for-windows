rust_binding_root = meson.current_source_dir()

cargo_fmt = find_program('cargo-fmt', required: false)
if gobject_introspection_dep.found() and cargo_fmt.found()
  gir_dir = rust_binding_root / 'gir-files'

  run_target('update-rust-bindings',
    command: [
      rust_binding_root / 'update-bindings.sh',
      libppsview_gir[0].full_path(),
      libppsdocument_gir[0].full_path(),
    ],
    depends: [libppsview_gir[0], libppsdocument_gir[0]]
  )
endif

configure_file(
  input: 'config.rs.in',
  output: 'config.rs',
  configuration: config_h,
)

# Common environment variables for all rust build targets
cargo_common_env = environment()
# it seems that flatpak can't access network during install stage.
cargo_common_env.set('CARGO_HOME', build_root / 'cargo-home')
cargo_common_env.set('CODEGEN_BUILD_DIR', meson.current_build_dir())
# override library lookup, we should not use the system library.
cargo_common_env.set('SYSTEM_DEPS_PAPERS_VIEW_4_0_NO_PKG_CONFIG', '1')
cargo_common_env.set('SYSTEM_DEPS_PAPERS_VIEW_4_0_SEARCH_NATIVE', build_root / 'libview')
cargo_common_env.set('SYSTEM_DEPS_PAPERS_VIEW_4_0_LIB', 'ppsview-4.0')
cargo_common_env.set('SYSTEM_DEPS_PAPERS_DOCUMENT_4_0_NO_PKG_CONFIG', '1')
cargo_common_env.set('SYSTEM_DEPS_PAPERS_DOCUMENT_4_0_SEARCH_NATIVE', build_root / 'libdocument')
cargo_common_env.set('SYSTEM_DEPS_PAPERS_DOCUMENT_4_0_LIB', 'ppsdocument-4.0')

# If the typename is internal, then we have built libadwaita from the subproject and we nee to tell cargo to look into that
if libaw_dep.type_name() == 'internal'
  cargo_common_env.set('SYSTEM_DEPS_LIBADWAITA_1_NO_PKG_CONFIG', '1')
  cargo_common_env.set('SYSTEM_DEPS_LIBADWAITA_1_SEARCH_NATIVE', build_root / 'subprojects/adwaita/src')
  cargo_common_env.set('SYSTEM_DEPS_LIBADWAITA_1_LIB', 'adwaita-1')
endif

if gtk_dep.type_name() == 'internal'
  cargo_common_env.set('SYSTEM_DEPS_GTK4_NO_PKG_CONFIG', '1')
  cargo_common_env.set('SYSTEM_DEPS_GTK4_SEARCH_NATIVE', build_root / 'subprojects/gtk/gtk')
  cargo_common_env.set('SYSTEM_DEPS_GTK4_LIB', 'gtk-4')
endif

# Setup the debuginfo option from the meson build system
cargo_common_env.set('CARGO_PROFILE_RELEASE_DEBUG', get_option('debug').to_string())
cargo_common_env.set('CARGO_PROFILE_DEV_DEBUG', get_option('debug').to_string())

meson.add_devenv(cargo_common_env)

if get_option('optimization') == '0'
  rust_target = 'debug'
else
  rust_target = 'release'
endif
