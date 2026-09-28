fixtures = meson.current_source_dir()

foreach t : ['json', 'layouts', 'model']
  exe = executable('test-' + t, 'test-' + t + '.c', dependencies: model_dep)
  test(t, exe, env: ['MLN_FIXTURES=' + fixtures])
endforeach

# MlnPanes in a real window, driven with shotbox (github.com/mishan/shotbox)
# on a private X display: the installed one, or a checkout given with
# -Dshotbox. Skipped without either.
python = find_program('python3', required: false)

if python.found()
  widget_env = environment()
  if get_option('shotbox') != ''
    widget_env.prepend('PYTHONPATH', get_option('shotbox'))
  endif
  test('widget', python,
       args: [files('widget.py'), demo],
       env: widget_env,
       timeout: 900,
       is_parallel: false)
endif
