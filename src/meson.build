# Source lists for the qdl binaries. Paths here are relative to src/,
# so this is the one file to touch when adding or removing a source.

# Shared by every executable; built into the qdl_common static library.
common_sources = files(
  'sahara.c', 'util.c', 'ux.c', 'oscompat.c', 'file.c',
)

# qdl: the full flashing tool.
qdl_sources = files(
  'auto.c', 'qud.c',
  'firehose.c',
  'io.c', 'qdl.c', 'patch.c',
  'program.c', 'read.c', 'sha2.c', 'sim.c', 'ufs.c', 'usb.c',
  'vip.c', 'sparse.c', 'gpt.c', 'flashmap.c', 'json.c', 'contents.c', 'pathbuf.c',
  'zipper.c',
)

# qdl-ramdump: crash-dump collection only.
ramdump_sources = files(
  'ramdump.c', 'auto.c', 'qud.c', 'io.c', 'sha2.c', 'sim.c', 'usb.c',
)

# qdl-ks: Sahara kickstart helper.
ks_sources = files('ks.c')

# Individual sources reused by the cmocka unit tests.
pathbuf_src = files('pathbuf.c')
program_src = files('program.c')
util_src    = files('util.c')
