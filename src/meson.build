# Source lists for the qdl binaries. Paths here are relative to src/,
# so this is the one file to touch when adding or removing a source.

# Shared by every executable; built into the qdl_common static library.
common_sources = files(
  'sahara.c', 'util.c', 'ux.c', 'oscompat.c', 'file.c',
)

# Everything except main(); reused by the qdl binary and the nbdkit plugin.
lib_sources = files(
  'auto.c', 'qud.c',
  'firehose.c',
  'io.c', 'patch.c',
  'program.c', 'read.c', 'sahara_config.c', 'sha2.c', 'sim.c', 'ufs.c', 'usb.c',
  'vip.c', 'sparse.c', 'gpt.c', 'flashmap.c', 'json.c', 'contents.c', 'pathbuf.c',
  'zipper.c',
)

# qdl: the full flashing tool (shared sources plus the CLI front-end).
qdl_sources = lib_sources + files('qdl.c')

# nbdkit plugin exposing a LUN as a block device.
nbdkit_plugin_src = files('nbdkit-qdl-plugin.c')

# Individual sources reused by the cmocka unit tests.
flashmap_src = files('flashmap.c')
json_src     = files('json.c')
pathbuf_src = files('pathbuf.c')
program_src = files('program.c')
util_src    = files('util.c')
