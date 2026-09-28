fixtures = meson.current_source_dir()

foreach t : ['json', 'layouts', 'model']
  exe = executable('test-' + t, 'test-' + t + '.c', dependencies: model_dep)
  test(t, exe, env: ['MLN_FIXTURES=' + fixtures])
endforeach
