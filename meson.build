project('qdl', 'c',
  default_options : ['warning_level=2', 'optimization=2'],
  license: 'BSD-3-Clause',
  meson_version: '>=1.1.0'
)

# Determine version
env_v = get_option('VERSION')
if env_v != ''
  ver = env_v
else
  git = find_program('git', required: false)
  git_v = 'unknown-version'
  if git.found()
    git_check_ver = run_command(git, 'describe', '--dirty', '--always', '--tags', check: false).stdout().strip()
    git_v = git_check_ver != '' ? git_check_ver : git_v
  endif
  ver = git_v
endif

# Dependencies
libusb_dep = dependency('libusb-1.0')
libxml_dep = dependency('libxml-2.0')
libzip_dep = dependency('libzip')
help2man = find_program('help2man', required: false)
cmocka_dep = dependency('cmocka', required: get_option('tests'))

# Public API headers live in include/, implementation headers in src/.
# '.' adds the top-level build dir, where configure_file generates
# version.h, so targets in subdirs (e.g. tests/) resolve it too.
inc = include_directories('include', 'src', '.')

# Windows-specific link flags
ws2_dep = []
setupapi_dep = []
if host_machine.system() == 'windows'
  ws2_dep = meson.get_compiler('c').find_library('ws2_32', required : false)
  # SetupAPI / cfgmgr32 are needed by the QUD backend to enumerate Qualcomm
  # COM ports exposed by the official QDLoader 9008 driver.
  setupapi_dep = [
    meson.get_compiler('c').find_library('setupapi', required : false),
    meson.get_compiler('c').find_library('cfgmgr32', required : false),
  ]
endif

common_dep = [libusb_dep, libxml_dep, libzip_dep, ws2_dep, setupapi_dep]

# Compile-only view of common_dep (includes + cflags, no link args).
# Used on executables so their own sources see the headers, while link
# args propagate exactly once through the static library below, avoiding
# duplicate "-l..." entries on the link line.
common_compile_dep = []
foreach d : [libusb_dep, libxml_dep, libzip_dep]
  common_compile_dep += d.partial_dependency(compile_args: true, includes: true)
endforeach

# --- version header generation ---
conf = configuration_data()
conf.set('version', ver)

version_h = configure_file(
  input: 'include/version.h.in',
  output: 'version.h',
  configuration: conf
)

# Source lists (common_sources, qdl_sources and the per-test source
# vars) are declared next to the sources.
subdir('src')

shared_lib = static_library('qdl_common', common_sources,
  dependencies: common_dep,
  include_directories: inc,
)

# --- qdl executable ---
qdl_exe = executable('qdl',
  sources : qdl_sources + [version_h],
  link_with: shared_lib,
  dependencies : common_compile_dep,
  include_directories : inc,
  install : true
)

# --- tests ---
subdir('tests')

# --- checkpatch targets  ---
check_script = join_paths(meson.project_source_root(), 'scripts', 'checkpatch_wrapper.sh')
foreach checkpatchparam : ['download-checkpatch', 'check', 'check-cached', 'check-range']
  run_target(checkpatchparam,
    command: ['bash', '-lc', check_script + ' ' + checkpatchparam]
  )
endforeach

# --- markdown lint target ---
markdown_script = join_paths(meson.project_source_root(), 'scripts', 'markdownlint_wrapper.sh')
run_target('markdown-lint',
  command: ['bash', '-lc', markdown_script + ' check']
)

# --- man generation ---
if help2man.found()
  # Test if we can run the built executable (might fail in cross-compilation)
  can_run_exe = not meson.is_cross_build()

  if can_run_exe
    manpage_targets = []
    foreach prog : [
      ['qdl', qdl_exe, 'Qualcomm Download'],
    ]
      manpage_targets += custom_target(prog[0] + '.1',
        input: prog[1],
        output: prog[0] + '.1',
        command: [help2man, '-N', '-n', prog[2], prog[1].full_path()],
        capture: true,
        install: true,
        install_dir: get_option('mandir') / 'man1')
    endforeach

    # Replicate the "manpages" phony target. alias_target requires at
    # least one dependency, so it is only declared when we have actually
    # produced manpage targets above.
    alias_target('manpages', manpage_targets)
  endif
endif
