incs = include_directories('.')

# The model on its own: GLib only, and what the unit tests link.
libmodel = static_library('mullion-model', files('mln-json.c', 'mln-model.c'),
  dependencies: [glib, libm],
  include_directories: incs,
  gnu_symbol_visibility: 'hidden',
  pic: true)

model_dep = declare_dependency(link_with: libmodel, include_directories: incs,
  dependencies: [glib, libm])

# Only the API is exported from a shared build (MLN_EXPORT, mln-panes.h):
# gnu_symbol_visibility hides the rest, and on Windows the header needs
# telling that this is the DLL being built.
exporting = []
if host_machine.system() == 'windows' and get_option('default_library') != 'static'
  exporting = ['-DMLN_BUILDING_SHARED']
endif

if meson.is_subproject()
  # Built into an app: static whatever the app's default_library says, so
  # its bundle has no shared object to find, and nothing installed.
  libpanes = static_library(package, files('mln-panes.c'),
    dependencies: [gtk, model_dep],
    link_whole: libmodel,
    include_directories: incs)
else
  # Static by default here too (default_library in meson.build).
  libpanes = library(package, files('mln-panes.c'),
    dependencies: [gtk, model_dep],
    link_whole: libmodel,
    include_directories: incs,
    c_args: exporting,
    gnu_symbol_visibility: 'hidden',
    version: meson.project_version(),
    soversion: api,
    install: true)

  install_headers('mullion-gtk.h', 'mln-panes.h', subdir: package)

  import('pkgconfig').generate(libpanes,
    name: 'mullion-gtk',
    filebase: package,
    description: 'Tiled panes for GTK 4 apps',
    subdirs: package,
    requires: ['gtk4 >= 4.10'])
endif

panes_dep = declare_dependency(link_with: libpanes, include_directories: incs,
  dependencies: [gtk])

meson.override_dependency(package, panes_dep)
