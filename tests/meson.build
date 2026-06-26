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

# --- hardware-in-the-loop tests ---
# Each step is a separate test in the "hil" suite. Run explicitly against an
# attached EDL device with:
#   QDL_HIL_BUILD=... QDL_HIL_STORAGE=... meson test --suite hil
# Without that environment every step exits 77 (SKIP), so the suite is
# harmless if picked up by a plain "meson test" run.
#
# The steps are stateful (flash -> read -> erase -> write -> reboot), so they
# must not overlap and must run in order: is_parallel:false serializes them
# and a descending priority fixes the order. Flashing a full build can take a
# while, hence the long timeout.
hil_steps = [
  ['flash whole build',       'flash_full',         100],
  ['read EFI partition',      'read_partition',      90],
  ['read GPT table',          'read_gpt',            80],
  ['read address P/S+L',      'read_addr_sectors',   70],
  ['read address P/name',     'read_addr_partname',  60],
  ['flash skipblock sha256',  'flash_skipblock',     50],
  ['sha256 command',          'sha256',              40],
  ['erase EFI partition',     'erase',               30],
  ['write EFI partition',     'write_image',         20],
  ['reboot board',            'reboot',              10],
]

foreach s : hil_steps
  test(
    'HIL ' + s[0],
    find_program('bash'),
    args: [meson.current_source_dir() / 'test_hil.sh',
           '--builddir', meson.project_build_root(),
           '--step', s[1]],
    depends: [qdl_exe],
    suite: 'hil',
    is_parallel: false,
    priority: s[2],
    timeout: 1800,
  )
endforeach

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
else
  warning('cmocka not found; skipping unit tests')
endif
