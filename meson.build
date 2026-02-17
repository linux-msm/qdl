project('qdl', 'c',
  default_options : ['warning_level=2', 'optimization=2'],
  license: 'BSD-3-Clause',
  meson_version: '>=1.1.0'
)

# Determine version
git_check_ver = run_command('git', 'describe', '--dirty', '--always', '--tags', check: false).stdout().strip()
git_v = git_check_ver != '' ? git_check_ver : 'unknown-version'
env_v = get_option('VERSION')
ver   = env_v != '' ? env_v : git_v

# Dependencies
libusb_dep = dependency('libusb-1.0')
libxml_dep = dependency('libxml-2.0')
help2man = find_program('help2man', required: false)

# Common include dirs
inc = include_directories('.')

# Windows-specific link flags
ws2_dep = []
if host_machine.system() == 'windows'
  ws2_dep = meson.get_compiler('c').find_library('ws2_32', required : false)
endif

common_dep = [libusb_dep, libxml_dep, ws2_dep]

# --- version header generation ---
conf = configuration_data()
conf.set('version', ver)

version_h = configure_file(
  input: 'version.h.in',
  output: 'version.h',
  configuration: conf
)

common_sources = files(
  'sahara.c', 'util.c', 'ux.c', 'oscompat.c'
)

shared_lib = static_library('qdl_common', common_sources, dependencies: common_dep)

# --- qdl executable ---
qdl_sources = files(
  'firehose.c',
  'io.c', 'qdl.c', 'patch.c',
  'program.c', 'read.c', 'sha2.c', 'sim.c', 'ufs.c', 'usb.c',
  'vip.c', 'sparse.c', 'gpt.c'
)

qdl_exe = executable('qdl',
  sources : qdl_sources + [version_h],
  link_with: shared_lib,
  dependencies : common_dep,
  include_directories : inc,
  install : true
)

# --- qdl-ramdump executable ---
ramdump_sources = files(
  'ramdump.c', 'io.c', 'sim.c', 'usb.c'
)

qdl_ramdump_exe = executable('qdl-ramdump',
  sources : ramdump_sources + [version_h],
  link_with: shared_lib,
  dependencies : common_dep,
  include_directories : inc,
  install : true
)

# --- ks executable ---
ks_sources = files('ks.c')

ks_exe = executable('ks',
  sources : ks_sources + [version_h],
  link_with: shared_lib,
  dependencies : common_dep,
  include_directories : inc,
  install : true
)

# -- tests ---
test(
  'Integration tests',
  find_program('bash'),
  args: [meson.current_source_dir() / 'tests/run_tests.sh', '--builddir', meson.current_build_dir()],
  depends: [qdl_exe, qdl_ramdump_exe, ks_exe]
)

# --- checkpatch targets  ---
check_script = join_paths(meson.project_source_root(), 'scripts', 'checkpatch_wrapper.sh')
foreach checkpatchparam : ['download-checkpatch', 'check', 'check-cached']
  run_target(checkpatchparam,
    command: ['bash', '-lc', check_script + ' ' + checkpatchparam]
  )
endforeach

# --- man generation ---
if help2man.found()
  # Test if we can run the built executable (might fail in cross-compilation)
  can_run_exe = not meson.is_cross_build()

  if can_run_exe
    manpage_targets = []
    foreach prog : [
      ['qdl', qdl_exe, 'Qualcomm Download'],
      ['qdl-ramdump', qdl_ramdump_exe, 'Qualcomm Download Ramdump'],
      ['ks', ks_exe, 'KS']
    ]
      manpage_targets += custom_target(prog[0] + '.1',
        input: prog[1],
        output: prog[0] + '.1',
        command: [help2man, '-N', '-n', prog[2], prog[1].full_path()],
        capture: true,
        install: true,
        install_dir: get_option('mandir') / 'man1')
    endforeach
  endif
endif

# Optional: replicate the "manpages" phony target
alias_target('manpages', manpage_targets)
