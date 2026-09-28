model_sources = files('mln-json.c', 'mln-model.c')

libmodel = static_library('mullion-model', model_sources,
  dependencies: [glib, libm],
  include_directories: include_directories('.'))

model_dep = declare_dependency(link_with: libmodel,
  include_directories: include_directories('.'),
  dependencies: [glib, libm])

libpanes = static_library('mullion-gtk', files('mln-panes.c'),
  dependencies: [gtk, model_dep])

panes_dep = declare_dependency(link_with: libpanes,
  include_directories: include_directories('.'),
  dependencies: [gtk, model_dep])
