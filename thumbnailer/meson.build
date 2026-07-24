input = 'papers.thumbnailer'

configure_file(
  input: input + '.in',
  output: input,
  configuration: mime_types_conf,
  install: true,
  install_dir: join_paths(pps_datadir, 'thumbnailers'),
)

cargo = find_program('cargo', required: true)

thumbnailer_root = meson.current_source_dir()
cargo_options = ['--manifest-path', thumbnailer_root / 'Cargo.toml']
cargo_options += ['--target-dir', meson.current_build_dir()]

if rust_target == 'release'
  cargo_options += [ '--release' ]
endif

cargo_env = cargo_common_env

cargo_deps = []

if host_machine.system() == 'windows'
  win = import('windows')
  thumb_win_res = win.compile_resources(
    'papers-thumbnailer.rc',
    depend_files: files('../shell/resources/papers.ico'),
  )
  cargo_deps += thumb_win_res
  cargo_env.set('RUSTFLAGS', '-C link-arg=' + thumb_win_res[0].full_path())
endif

cargo_build = custom_target(
  'cargo-build',
  build_by_default: true,
  build_always_stale: true,
  output: 'src',
  console: true,
  depends: cargo_deps,
  env: cargo_env,
  command: [
    cargo,
    'build',
    cargo_options,
  ],
)


if host_machine.system() == 'windows'
  thumb_bin_name = meson.project_name() + '-thumbnailer.exe'
else
  thumb_bin_name = meson.project_name() + '-thumbnailer'
endif

copy = custom_target(
  'cp-binary',
  depends: cargo_build,
  build_by_default: true,
  build_always_stale: true,
  install: true,
  install_dir: get_option('bindir'),
  output: thumb_bin_name,
  command: [
    find_program('python3', 'python'),
    '-c', 'import sys, shutil; shutil.copy2(sys.argv[1], sys.argv[2])',
    meson.current_build_dir() / rust_target / thumb_bin_name,
    '@OUTPUT@',
  ],
)

pps_library_path = '@0@/libview:@0@/libdocument:@0@/shell'.format(build_root)
test_env = backend_env
test_env.set('DYLD_LIBRARY_PATH', pps_library_path)
test_env.set('LD_LIBRARY_PATH', pps_library_path)
test_env.set('G_DEBUG', 'fatal-criticals')

meson_env = environment()
meson_env.prepend('PATH', meson.current_build_dir())
meson.add_devenv(meson_env)

if get_option('tests')
  test(
    'thumbnailer pdf',
    files(thumbnailer_root / 'thumbnailer-test.sh'),
    args: [source_root / 'test-data' / 'utf16le-annot.pdf'],
    env: test_env,
    workdir: meson.current_build_dir(),
    depends: [copy],
    # this writes to thumbnail.png, like the next test, so they shouldn't be run in parallel
    is_parallel: false,
  )
  test(
    'thumbnailer cbz',
    files(thumbnailer_root / 'thumbnailer-test.sh'),
    args: [source_root / 'test-data' / 'empty.cbz'],
    env: test_env,
    workdir: meson.current_build_dir(),
    depends: [copy],
    is_parallel: false,
  )
endif
