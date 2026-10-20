project('mullion-gtk', 'c',
  version: '0.3.2',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.60',
  default_options: ['c_std=gnu11', 'warning_level=2', 'default_library=static'])

api = '0'
package = 'mullion-gtk-' + api

glib = dependency('glib-2.0', version: '>= 2.74')
gtk = dependency('gtk4', version: '>= 4.10')
libm = meson.get_compiler('c').find_library('m', required: false)
# Optional: GLib needs libintl too, so where it is not in libc (macOS) it
# comes in with GLib's flags, and a separate search there finds nothing.
intl = dependency('intl', required: false)
i18n = import('i18n')

add_project_arguments('-Wno-unused-parameter', '-Wno-missing-field-initializers',
                      language: 'c')

subdir('src')

# Translations install with the library even as a subproject: its strings
# are looked up under the app's localedir, which is where they then go.
subdir('po')

# Built into another project with meson, it is a subproject and nothing more.
if not meson.is_subproject()
  subdir('demo')
  subdir('tests')
endif
