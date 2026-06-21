# Test suite. The integration tests drive the built qdl binary through
# shell scripts; the unit tests are cmocka programs that pull in a few
# src/ translation units directly. Both reuse variables declared in the
# root and src/ meson.build (qdl_exe, inc, version_h, *_src, etc.).

# --- integration tests ---
# The scripts locate the qdl binary under the top-level build dir, so
# pass meson.project_build_root() rather than this subdir's build dir.
test(
  'VIP table generation',
  find_program('bash'),
  args: [meson.current_source_dir() / 'test_vip_generation.sh', '--builddir', meson.project_build_root()],
  depends: [qdl_exe],
  suite: 'integration',
)

test(
  'Flashmap dry-run',
  find_program('bash'),
  args: [meson.current_source_dir() / 'test_flashmap.sh', '--builddir', meson.project_build_root()],
  depends: [qdl_exe],
  suite: 'integration',
  protocol: 'tap',
)

# --- unit tests (cmocka) ---
if cmocka_dep.found()
  test_program_load_xml = executable('test_program_load_xml',
    sources : [
      'test_program_load_xml.c',
      'common.c',
      pathbuf_src,
      program_src,
      util_src,
      version_h,
    ],
    dependencies : common_dep + [cmocka_dep],
    include_directories : inc,
    c_args : ['-Daccess=mock_access'],
  )

  test(
    'program_load_xml path resolution',
    test_program_load_xml,
    suite: 'unit',
    protocol: 'tap',
    env: ['CMOCKA_MESSAGE_OUTPUT=TAP'],
  )

  test_pathbuf = executable('test_pathbuf',
    sources : [
      'test_pathbuf.c',
      pathbuf_src,
    ],
    dependencies : common_dep + [cmocka_dep],
    include_directories : inc,
  )

  test(
    'pathbuf dirname behavior',
    test_pathbuf,
    suite: 'unit',
    protocol: 'tap',
    env: ['CMOCKA_MESSAGE_OUTPUT=TAP'],
  )

  test_contents_selectors = executable('test_contents_selectors',
    sources : [
      'test_contents_selectors.c',
      pathbuf_src,
    ],
    dependencies : common_dep + [cmocka_dep],
    include_directories : inc,
  )

  test(
    'contents selector resolution',
    test_contents_selectors,
    suite: 'unit',
    protocol: 'tap',
    env: ['CMOCKA_MESSAGE_OUTPUT=TAP'],
  )

  test_contents_xml = executable('test_contents_xml',
    sources : [
      'test_contents_xml.c',
      'common.c',
      pathbuf_src,
    ],
    dependencies : common_dep + [cmocka_dep],
    include_directories : inc,
  )

  test(
    'contents XML parsing',
    test_contents_xml,
    suite: 'unit',
    protocol: 'tap',
    env: ['CMOCKA_MESSAGE_OUTPUT=TAP'],
  )

  test_oscompat = executable('test_oscompat',
    sources : [
      'test_oscompat.c',
    ],
    dependencies : common_dep + [cmocka_dep],
    include_directories : inc,
  )

  test(
    'oscompat path helpers',
    test_oscompat,
    suite: 'unit',
    protocol: 'tap',
    env: ['CMOCKA_MESSAGE_OUTPUT=TAP'],
  )
else
  warning('cmocka not found; skipping unit tests')
endif
