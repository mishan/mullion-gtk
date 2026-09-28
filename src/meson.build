model_sources = files('mln-json.c', 'mln-model.c')

libmodel = static_library('mullion-model', model_sources,
  dependencies: [glib, libm],
  include_directories: include_directories('.'))

model_dep = declare_dependency(link_with: libmodel,
  include_directories: include_directories('.'),
  dependencies: [glib, libm])
