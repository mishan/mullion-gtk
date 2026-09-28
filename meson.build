project('mullion-gtk', 'c',
  version: '0.1.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.60',
  default_options: ['c_std=gnu11', 'warning_level=2', 'default_library=static'])

glib = dependency('glib-2.0', version: '>= 2.74')
gtk = dependency('gtk4', version: '>= 4.10')
libm = meson.get_compiler('c').find_library('m', required: false)

add_project_arguments('-Wno-unused-parameter', '-Wno-missing-field-initializers',
                      language: 'c')

subdir('src')
subdir('demo')
subdir('tests')
