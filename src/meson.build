incs = include_directories('.')

# The model on its own: GLib only, and what the unit tests link.
libmodel = static_library('mullion-model', files('mln-json.c', 'mln-model.c'),
  dependencies: [glib, libm],
  include_directories: incs,
  pic: true)

model_dep = declare_dependency(link_with: libmodel, include_directories: incs,
  dependencies: [glib, libm])

# The library: static by default (default_library), so an app bundle has
# no shared object to find.
libpanes = library(package, files('mln-panes.c'),
  dependencies: [gtk, model_dep],
  link_whole: libmodel,
  include_directories: incs,
  version: meson.project_version(),
  install: true)

panes_dep = declare_dependency(link_with: libpanes, include_directories: incs,
  dependencies: [gtk])

meson.override_dependency(package, panes_dep)

install_headers('mullion-gtk.h', 'mln-panes.h', subdir: package)

import('pkgconfig').generate(libpanes,
  name: 'mullion-gtk',
  filebase: package,
  description: 'Tiled panes for GTK 4 apps',
  subdirs: package,
  requires: ['gtk4 >= 4.10'])
