lib_sources = files(
  'firehose.c', 'gpt.c', 'io.c', 'oscompat.c',
  'patch.c', 'program.c', 'read.c', 'sahara.c',
  'sha2.c', 'sim.c', 'sparse.c', 'ufs.c',
  'usb.c', 'util.c', 'ux.c', 'vip.c',
)

libqdl = static_library('qdl', lib_sources,
  dependencies: common_dep,
  include_directories: inc,
)
