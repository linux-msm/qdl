project('qdl', 'c',
  default_options : ['warning_level=2', 'optimization=2']
)

# Determine version
git_check_ver = run_command('git', 'describe', '--dirty', '--always', '--tags', check: false).stdout().strip()
git_v = git_check_ver != '' ? git_check_ver : 'unknown-version'
env_v = get_option('VERSION')
ver   = env_v != '' ? env_v : git_v

# Dependencies
libusb_dep = dependency('libusb-1.0')
libxml_dep = dependency('libxml-2.0')

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

# --- qdl executable ---
qdl_sources = files(
  'firehose.c',
  'io.c', 'qdl.c', 'patch.c',
  'program.c', 'read.c', 'sha2.c', 'sim.c', 'ufs.c', 'usb.c',
  'vip.c', 'sparse.c', 'gpt.c'
)

executable('qdl',
  sources : qdl_sources + common_sources + [version_h],
  dependencies : common_dep,
  include_directories : inc,
  install : true
)

# --- qdl-ramdump executable ---
ramdump_sources = files(
  'ramdump.c', 'io.c', 'sim.c', 'usb.c'
)

executable('qdl-ramdump',
  sources : ramdump_sources + common_sources + [version_h],
  dependencies : common_dep,
  include_directories : inc,
  install : true
)

# --- ks executable ---
ks_sources = files('ks.c')

executable('ks',
  sources : ks_sources + common_sources + [version_h],
  dependencies : common_dep,
  include_directories : inc,
  install : true
)

# -- Tests ---
test(
  'run_tests.sh',
  find_program('bash'),
  args: [meson.current_source_dir() / 'tests/run_tests.sh', '--builddir', meson.current_build_dir()]
)

# --- checkpatch targets  ---
# Absolute path to the script (works from build dir)
check_script = join_paths(meson.project_source_root(), 'scripts', 'checkpatch_wrapper.sh')

run_target('download-checkpatch',
  command: ['bash', '-lc', check_script + ' download'],
)

run_target('check',
  command: ['bash', '-lc', check_script + ' check'],
)

run_target('check-cached',
  command: ['bash', '-lc', check_script + ' check-cached'],
)