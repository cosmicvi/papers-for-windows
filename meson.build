project(
  'papers', ['c'],
  version: '51.beta',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 1.8.0',
  default_options: [
    'buildtype=debugoptimized',
  ]
)

if get_option('profile') == 'devel'
  profile = '.Devel'
  object_profile = '/Devel'
else
  profile = ''
  object_profile = ''
endif
app_id = 'org.gnome.Papers@0@'.format(profile)

pps_name = meson.project_name().to_lower()

pps_version = meson.project_version()
version_array = pps_version.split('.')
pps_major_version = version_array[0].to_int()
pps_minor_version = version_array[1]

pps_prefix = get_option('prefix')
pps_datadir = join_paths(pps_prefix, get_option('datadir'))
pps_includedir = join_paths(pps_prefix, get_option('includedir'))
pps_libdir = join_paths(pps_prefix, get_option('libdir'))
pps_localedir = join_paths(pps_prefix, get_option('localedir'))
pps_mandir = join_paths(pps_prefix, get_option('mandir'))

# Libtool versioning. The backend and view libraries have separate versions.
# Before making a release, the libtool version should be modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A

# Libtool version of the backend library
pps_document_current = 6
pps_document_revision = 0
pps_document_age = 0
pps_document_version = '@0@.@1@.@2@'.format(pps_document_current, pps_document_revision, pps_document_age)
pps_document_current_minus_age = pps_document_current - pps_document_age

# Libtool version of the view library
pps_view_current = 5
pps_view_revision = 0
pps_view_age = 0
pps_view_version = '@0@.@1@.@2@'.format(pps_view_current, pps_view_revision, pps_view_age)
pps_view_current_minus_age = pps_view_current - pps_view_age

pps_api_version = '4.0'

pps_include_subdir = join_paths(pps_name, pps_api_version)

# Backends directory
pps_backends_binary_version = pps_document_current
pps_backends_subdir = join_paths(pps_name, pps_backends_binary_version.to_string(), 'backends')
pps_backendsdir = join_paths(pps_libdir, pps_backends_subdir)

pps_namespace = 'org.gnome.Papers'

pps_code_prefix = 'Pps'

cc = meson.get_compiler('c')

config_h = configuration_data()
config_h.set10('_GNU_SOURCE', true)

# package
config_h.set_quoted('APP_ID', app_id)
config_h.set_quoted('PROFILE', profile)
config_h.set_quoted('OBJECT_PROFILE', object_profile)
config_h.set_quoted('PACKAGE_VERSION', pps_version)
config_h.set_quoted('VERSION', pps_version)
config_h.set_quoted('PPS_LOCALEDIR', pps_localedir)

# i18n
config_h.set_quoted('GETTEXT_PACKAGE', pps_name)

# Support for nl_langinfo (_NL_MEASUREMENT_MEASUREMENT) (optional)
langinfo_measurement_src = '''
  #include <langinfo.h>
  int main() {
    char c;
    c = *((unsigned char *)  nl_langinfo(_NL_MEASUREMENT_MEASUREMENT));
  };
'''
config_h.set('HAVE__NL_MEASUREMENT_MEASUREMENT', cc.compiles(langinfo_measurement_src, name: 'Support for nl_langinfo'),
             description: 'Define if _NL_MEASUREMENT_MEASUREMENT is available')

# compiler flags
common_flags = ['-DHAVE_CONFIG_H'] + cc.get_supported_arguments([
    '-Wnested-externs',
    '-Wstrict-prototypes',
    '-Wformat=2',
    '-Wmissing-include-dirs',
    '-Wmissing-prototypes',
    '-Wpointer-arith',
  ])

common_ldflags = []

libsysprof_capture_dep = dependency('sysprof-capture-4', required: get_option('sysprof'))

if libsysprof_capture_dep.found()
  common_flags += ['-DHAVE_SYSPROF']
endif

add_project_arguments(common_flags, language: 'c')
add_project_arguments('-DI_KNOW_THE_PAPERS_LIBS_ARE_UNSTABLE_AND_HAVE_TALKED_WITH_THE_AUTHORS',
                      language: 'c')

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

build_root = meson.project_build_root()
source_root = meson.current_source_dir()

data_dir = join_paths(source_root, 'data')
data_build_dir = join_paths(meson.project_build_root(), 'data')
po_dir = join_paths(source_root, 'po')

top_inc = include_directories('.')

glib_req_version = '>= 2.75.0'
gtk_req_version = '>= 4.17.1'
libaw_req_version = '>= 1.8.alpha'
exempi_req_version = '>= 2.0'

gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0', version: '>= 2.40.0')
config_h.set_quoted('EXTRA_GDK_PIXBUF_LOADERS_DIR',
                    join_paths (pps_libdir, pps_name, 'gdk-pixbuf', gdk_pixbuf_dep.get_variable(pkgconfig: 'gdk_pixbuf_binary_version')))

gio_dep = dependency('gio-2.0', version: glib_req_version)
glib_dep = dependency('glib-2.0', version: glib_req_version)
gmodule_dep = dependency('gmodule-2.0')
gtk_dep = dependency(
  'gtk4',
  version: gtk_req_version,
  fallback: ['gtk', 'libgtk_dep'],
  default_options: [
    'werror=false',
    'introspection=disabled',
    'build-demos=false',
    'build-testsuite=false',
    'build-tests=false',
    'build-examples=false',
  ])
gthread_dep = dependency('gthread-2.0', version: glib_req_version)
libaw_dep = dependency(
  'libadwaita-1',
  version: libaw_req_version,
  fallback: ['adwaita', 'libadwaita_dep'],
  default_options: [
    'werror=false',
    'introspection=disabled',
    'vapi=false',
    'documentation=false',
    'tests=false',
    'examples=false',
  ])
exempi_dep = dependency('exempi-2.0', version: exempi_req_version, required: false)
config_h.set('HAVE_EXEMPI', exempi_dep.found())

m_dep = cc.find_library('m')

cairo_dep = dependency('cairo', version: '>= 1.14.0')

# ZLIB support (required)
zlib_dep = cc.find_library('z', required: false)
assert(zlib_dep.found() and cc.has_function('inflate', dependencies: zlib_dep) and cc.has_function('crc32', dependencies: zlib_dep),
      'No sufficient zlib library found on your system')

# *** Nautilus property page build ***
enable_nautilus = get_option('nautilus') and host_machine.system() != 'windows'
if enable_nautilus
  libnautilus_extension_dep = dependency('libnautilus-extension-4', version: ['>= 43'])
  nautilus_extension_dir = libnautilus_extension_dep.get_variable(pkgconfig: 'extensiondir', pkgconfig_define: ['libdir', pps_libdir])
endif

# *** GNOME Keyring support ***
enable_keyring = get_option('keyring').allowed() and host_machine.system() != 'windows'

# GKT+ Unix Printing
gtk_unix_print_dep = dependency('gtk4-unix-print', version: gtk_req_version, required: get_option('gtk_unix_print'))
enable_gtk_unix_print = gtk_unix_print_dep.found()
config_h.set10('GTKUNIXPRINT_ENABLED', enable_gtk_unix_print)

# *** GObject Introspection ***
enable_user_doc = get_option('user_doc')
enable_docs = get_option('documentation')
gobject_introspection_dep = dependency(
  'gobject-introspection-1.0',
  version: '>= 1.0',
  required: get_option('introspection'),
)
if not gobject_introspection_dep.found()
  warning('Disable documentation due to introspection is disabled')
  enable_docs = false
endif

# *** Blueprint-Compiler ***
blueprint_compiler = find_program('blueprint-compiler', version: '>= 0.16')

# *** Appstream CLI ***
appstreamcli = find_program('appstreamcli', required: true)

# *** Libspelling ***
libspelling_dep = dependency('libspelling-1', version: '>= 0.2', required: get_option('spell_check'))
enable_spellcheck = libspelling_dep.found()
config_h.set10('HAVE_LIBSPELLING', enable_spellcheck)

# *** Mime types list ***
mime_types_list = {
  'comics': [
    'application/vnd.comicbook-rar',
    'application/vnd.comicbook+zip',
    'application/x-cb7',
    'application/x-cbr',
    'application/x-cbt',
    'application/x-cbz',
    'application/x-ext-cb7',
    'application/x-ext-cbr',
    'application/x-ext-cbt',
    'application/x-ext-cbz',
  ],
  'djvu': [
    'application/x-ext-djv',
    'application/x-ext-djvu',
    'image/vnd.djvu',
    'image/vnd.djvu+multipage',
  ],
  'illustrator': [
    'application/illustrator'
  ],
  'pdf': [
    'application/pdf',
    'application/x-bzpdf',
    'application/x-ext-pdf',
    'application/x-gzpdf',
    'application/x-xzpdf',
  ],
  'tiff': [
    'image/tiff'
  ],
}

backends = {}
papers_mime_types = []

# *** Comic Book ***
libarchive_req_version = '>= 3.6.0'
libarchive_dep = dependency('libarchive', version: libarchive_req_version, required: get_option('comics'))
enable_comics = libarchive_dep.found()
if enable_comics
  backends += {'comics': mime_types_list.get('comics')}
  papers_mime_types += mime_types_list.get('comics')
elif get_option('comics').auto()
  warning('** Comics support is disabled since libarchive (version ' + libarchive_req_version + ') is needed')
endif

# *** DJVU ***
ddjvuapi_req_version = '>= 3.5.22'
ddjvuapi_dep = dependency('ddjvuapi', version: ddjvuapi_req_version, required: get_option('djvu'))
enable_djvu = ddjvuapi_dep.found()
if enable_djvu
  backends += {'djvu': mime_types_list.get('djvu')}
  papers_mime_types += mime_types_list.get('djvu')
elif get_option('djvu').auto()
  warning('Djvu support is disabled since a recent version of the djvulibre library was not found. You need at least djvulibre ' + ddjvuapi_req_version + ' which can be found on http://djvulibre.djvuzone.org')
endif

# *** PDF ***
poppler_req_version = '>= 25.07.0'
poppler_glib_dep = dependency('poppler-glib', version: poppler_req_version, required: get_option('pdf'))

enable_pdf = poppler_glib_dep.found()
if enable_pdf
  cairo_pdf_dep = dependency('cairo-pdf', required: false)
  cairo_ps_dep = dependency('cairo-ps', required: false)

  if cairo_pdf_dep.found()
    config_h.set('HAVE_CAIRO_PDF', true)
  endif

  if cairo_ps_dep.found()
    config_h.set('HAVE_CAIRO_PS', true)
  endif

  backends += {'pdf': mime_types_list.get('pdf') + mime_types_list.get('illustrator')}
  papers_mime_types += mime_types_list.get('pdf')
  papers_mime_types += mime_types_list.get('illustrator')

  if cc.has_function('poppler_page_render_transparent_selection', prefix: '#include <poppler.h>', dependencies: poppler_glib_dep)
    add_project_arguments ('-DHAVE_TRANSPARENT_SELECTION', language: 'c')
  endif
  if cc.has_function('poppler_annot_ink_set_draw_below', prefix: '#include <poppler.h>', dependencies: poppler_glib_dep)
    add_project_arguments('-DPOPPLER_HAS_HIGHLIGHT_INK', language: 'c')
  endif

elif get_option('pdf').auto()
  warning('PDF support is disabled since poppler-glib version ' + poppler_req_version + ' not found')
endif

# *** TIFF ***
libtiff_dep = dependency('libtiff-4', required: get_option('tiff'))
enable_tiff = libtiff_dep.found()
if enable_tiff
  backends += {'tiff': mime_types_list.get('tiff')}
  papers_mime_types += mime_types_list.get('tiff')
elif get_option('tiff').auto()
  warning('Tiff support is disabled since tiff library version 4.0 or newer not found')
endif

mime_types_conf = configuration_data()
mime_types_conf.set('PAPERS_MIME_TYPES', ';'.join(papers_mime_types))
mime_types_conf.set('PACKAGE_ICON_NAME', app_id)

subdir('libdocument')
subdir('libview')
subdir('nautilus')

subdir('rust')

# Print Previewer
enable_previewer = get_option('previewer')
if enable_previewer
  subdir('previewer')
endif

subdir('data')

# *** Papers ***
enable_shell = get_option('shell')
if enable_shell
  subdir('shell')
endif

subdir('po')
subdir('help')

# *** Thumbnailer ***
enable_thumbnailer = get_option('thumbnailer')
if enable_thumbnailer
  subdir('thumbnailer')
endif

configure_file(
  output: 'config.h',
  configuration: config_h,
)

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: host_machine.system() != 'windows',
)

# *** File Tests ***
enable_file_tests = get_option('file_tests')
if enable_file_tests
  subdir('libview/tests')
endif


summary({'Build type'                 : get_option('buildtype'),
         'Application ID'             : app_id,
         'Rust build mode'            : rust_target,
        }, section: 'General', bool_yn: true)
summary({'Shell'                     : enable_shell,
         'Previewer'                  : enable_previewer,
         'Thumbnailer'                : enable_thumbnailer,
         'Nautilus extension'         : enable_nautilus,
        }, section: 'Frontends', bool_yn: true)
summary({'Comics'                     : enable_comics,
         'DJVU'                       : enable_djvu,
         'PDF'                        : enable_pdf,
         'TIFF'                       : enable_tiff,
        }, section: 'Backends', bool_yn: true)
summary({'tests'                      : get_option('tests'),
         'Developer documentation'    : enable_docs,
         'User documentation'         : enable_user_doc,
         'GObject introspection'      : gobject_introspection_dep.found(),
         'Sysprof support'            : libsysprof_capture_dep.found(),
         'Keyring integration'        : enable_keyring,
         'GTK+ Unix print'            : enable_gtk_unix_print,
         'Spell checker'              : enable_spellcheck,
         'File Tests'                 : enable_file_tests,
        }, section: 'Features', bool_yn: true)
