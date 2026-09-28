# A meson project that takes mullion-gtk as a subproject: CI builds it
# with the default library shared, to check that mullion-gtk stays static
# there and installs nothing into it. subprojects/mullion-gtk is a link to
# this checkout, made by CI.
project('mln-consumer', 'c', default_options: ['default_library=shared'])

mullion = dependency('mullion-gtk-0', fallback: ['mullion-gtk'])

executable('consumer', '../../demo/demo.c', dependencies: mullion, install: true)
