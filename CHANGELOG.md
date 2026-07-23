# Changelog

All notable changes to QDL, mirroring the [GitHub releases](https://github.com/linux-msm/qdl/releases). Each entry lists the pull requests merged for that release.

This project uses `vMAJOR.MINOR` tags. Releases prior to v2.4 were tagged but not published as GitHub releases; see the [git tags](https://github.com/linux-msm/qdl/tags) for their history.

## [v2.8](https://github.com/linux-msm/qdl/releases/tag/v2.8) - 2026-08-03

### Highlights

- One `qdl` binary: the separate `qdl-ks` and `qdl-ramdump` tools are folded in as subcommands ([#250](https://github.com/linux-msm/qdl/pull/250))
- New `chipinfo` and `reset` verbs for reading chip identity and resetting the device without flashing ([#280](https://github.com/linux-msm/qdl/pull/280),
  [#281](https://github.com/linux-msm/qdl/pull/281))
- nbdkit plugin exposing a device LUN as a block device ([#283](https://github.com/linux-msm/qdl/pull/283))
- Faster, leaner skipblock flashing: short/offset files can be skipped, digest memory is bounded, progress is reported
  ([#256](https://github.com/linux-msm/qdl/pull/256), [#264](https://github.com/linux-msm/qdl/pull/264), [#265](https://github.com/linux-msm/qdl/pull/265))
- Hardening sweep across firehose, sahara, GPT and program parsing of device-controlled data ([#284](https://github.com/linux-msm/qdl/pull/284),
  [#270](https://github.com/linux-msm/qdl/pull/270))
- Improved crash-mode/ramdump device detection ([#274](https://github.com/linux-msm/qdl/pull/274), [#286](https://github.com/linux-msm/qdl/pull/286))
- EDL devices are identified by their vendor-specific interface signature instead of a product-id allowlist, so the libusb and Windows QUD backends
  now share one notion of an EDL device ([#287](https://github.com/linux-msm/qdl/pull/287), [#289](https://github.com/linux-msm/qdl/pull/289))

### Breaking changes

- `qdl-ks` and `qdl-ramdump` no longer exist as standalone binaries; scripts
  and packaging must switch to `qdl ks ...` / the ramdump verb
  ([#250](https://github.com/linux-msm/qdl/pull/250))
- Mixing input XML files with command verbs on one invocation is now
  rejected instead of silently accepted
  ([#282](https://github.com/linux-msm/qdl/pull/282))

### What's Changed

- firehose: zero-pad local digest so short/offset files can be skipped by @igoropaniuk in [#256](https://github.com/linux-msm/qdl/pull/256)
- meson: don't require git when overriding the version by @obbardc in [#257](https://github.com/linux-msm/qdl/pull/257)
- firehose: report progress through the skipblock fast-path by @igoropaniuk in [#264](https://github.com/linux-msm/qdl/pull/264)
- [RFC] github: request reviews from qdl-maintainers via CODEOWNERS by @igoropaniuk in [#266](https://github.com/linux-msm/qdl/pull/266)
- tree: move sources under src/ and consolidate on a single qdl binary by @igoropaniuk in [#250](https://github.com/linux-msm/qdl/pull/250)
- README: describe how to use QDL from WSL2 on Windows by @brgl in [#267](https://github.com/linux-msm/qdl/pull/267)
- README: mark WSL2 support as unreliable by @brgl in [#273](https://github.com/linux-msm/qdl/pull/273)
- src/firehose: fix type error in flash log by @shoudil in [#275](https://github.com/linux-msm/qdl/pull/275)
- tests: fix test_contents_selectors link failure at -O0 by @igoropaniuk in [#272](https://github.com/linux-msm/qdl/pull/272)
- tests: add a hardware-in-the-loop (HIL) test suite by @igoropaniuk in [#271](https://github.com/linux-msm/qdl/pull/271)
- ramdump: create the output directory if it does not exist by @igoropaniuk in [#269](https://github.com/linux-msm/qdl/pull/269)
- README: use consistent dash bullets for unordered lists by @igoropaniuk in [#276](https://github.com/linux-msm/qdl/pull/276)
- firehose: tolerate zero-length packets while reading sector data by @igoropaniuk in [#270](https://github.com/linux-msm/qdl/pull/270)
- For linux msm/flashmap multi layout by @quic-bjorande in [#263](https://github.com/linux-msm/qdl/pull/263)
- firehose: chunk skipblock digest into bounded sub-regions by @igoropaniuk in [#265](https://github.com/linux-msm/qdl/pull/265)
- github: package self-contained binaries on Linux and macOS by @igoropaniuk in [#277](https://github.com/linux-msm/qdl/pull/277)
- doc: restructure and enrich the README by @igoropaniuk in [#268](https://github.com/linux-msm/qdl/pull/268)
- doc: add RELEASING.md describing the release process by @igoropaniuk in [#278](https://github.com/linux-msm/qdl/pull/278)
- [RFC] doc: add AGENTS.md guide for automation agents by @igoropaniuk in [#279](https://github.com/linux-msm/qdl/pull/279)
- usb: recognize 0x90db as an EDL ramdump product ID by @yangh in [#274](https://github.com/linux-msm/qdl/pull/274)
- qdl: Add reset verb for device reset by @edsj-sony in [#281](https://github.com/linux-msm/qdl/pull/281)
- sahara: add chipinfo command to read chip identity by @igoropaniuk in [#280](https://github.com/linux-msm/qdl/pull/280)
- [RFC] usb: decouple open/list paths and share EDL device identity across backends by @igoropaniuk in [#287](https://github.com/linux-msm/qdl/pull/287)
- tests: extend HIL test suite by @igoropaniuk in [#285](https://github.com/linux-msm/qdl/pull/285)
- Fix critical correctness issues (silent failures, crashes, OOB accesses) by @igoropaniuk in [#284](https://github.com/linux-msm/qdl/pull/284)
- [RFC] qdl: reject mixing input XML files with command verbs by @igoropaniuk in [#282](https://github.com/linux-msm/qdl/pull/282)
- Enhance crash mode device detection by @ykaire-qti in [#286](https://github.com/linux-msm/qdl/pull/286)
- nbdkit: expose a device LUN as a block device by @igoropaniuk in [#283](https://github.com/linux-msm/qdl/pull/283)
- vip: open signed tables in binary mode on Windows by @igoropaniuk in [#288](https://github.com/linux-msm/qdl/pull/288)
- Lint all markdown files and extend the release documentation by @igoropaniuk in [#292](https://github.com/linux-msm/qdl/pull/292)
- Address oscompat warnings on Windows by @igoropaniuk in [#291](https://github.com/linux-msm/qdl/pull/291)
- qdl: drop the EDL product id allowlist and match the EDL interface signature when enumerating by @igoropaniuk in [#289](https://github.com/linux-msm/qdl/pull/289)
- firehose: do not probe sector size when storage init is skipped by @igoropaniuk in [#297](https://github.com/linux-msm/qdl/pull/297)

### New Contributors

- @brgl made their first contribution in [#267](https://github.com/linux-msm/qdl/pull/267)
- @shoudil made their first contribution in [#275](https://github.com/linux-msm/qdl/pull/275)
- @yangh made their first contribution in [#274](https://github.com/linux-msm/qdl/pull/274)
- @edsj-sony made their first contribution in [#281](https://github.com/linux-msm/qdl/pull/281)

**Full Changelog**: [v2.7...v2.8](https://github.com/linux-msm/qdl/compare/v2.7...v2.8)

## [v2.7](https://github.com/linux-msm/qdl/releases/tag/v2.7) - 2026-06-08

### What's Changed
- [RFC] meson: replace GNU Make-based build system with Meson by @igoropaniuk in [#150](https://github.com/linux-msm/qdl/pull/150)
- tests: replace monolithic test wrapper with per-suite Meson tests by @igoropaniuk in [#208](https://github.com/linux-msm/qdl/pull/208)
- meson: rename ks to qdl-ks by @obbardc in [#211](https://github.com/linux-msm/qdl/pull/211)
- Update ks/ramdump description by @igoropaniuk in [#212](https://github.com/linux-msm/qdl/pull/212)
- sahara: increase MAPPING_SZ to 128 by @mukeshojha-linux in [#215](https://github.com/linux-msm/qdl/pull/215)
- For linux msm/flashmap support by @andersson in [#210](https://github.com/linux-msm/qdl/pull/210)
- Unify markdown-lint and checkpatch checks by @igoropaniuk in [#222](https://github.com/linux-msm/qdl/pull/222)
- tests: stop fixture generation from timing out CI on slow runners by @igoropaniuk in [#230](https://github.com/linux-msm/qdl/pull/230)
- tests: give each integration test its own fixture directory by @igoropaniuk in [#231](https://github.com/linux-msm/qdl/pull/231)
- commitlint: Recognize kernel-style trailers as footer items by @igoropaniuk in [#229](https://github.com/linux-msm/qdl/pull/229)
- [RFC] ux: Route libxml2 parser diagnostics through ux_err() by @igoropaniuk in [#227](https://github.com/linux-msm/qdl/pull/227)
- usb: Allow devices with no serial number to show in the usb list by @Zephyrus29 in [#213](https://github.com/linux-msm/qdl/pull/213)
- qdl: fix list/ramdump subcommands ignored when flags precede them by @mukeshojha-linux in [#217](https://github.com/linux-msm/qdl/pull/217)
- meson: avoid duplicate library entries on executable link lines by @igoropaniuk in [#228](https://github.com/linux-msm/qdl/pull/228)
- For linux msm/program search relative xml by @quic-bjorande in [#223](https://github.com/linux-msm/qdl/pull/223)
- Add --skip-reset and auto-skip Sahara when programmer is already running by @igoropaniuk in [#232](https://github.com/linux-msm/qdl/pull/232)
- qdl: Drop alloca definition by @quic-bjorande in [#236](https://github.com/linux-msm/qdl/pull/236)
- For linux msm/contents xml flashing by @quic-bjorande in [#225](https://github.com/linux-msm/qdl/pull/225)
- qdl: add QUD backend so on Windows flashing via the official QDLoader 9008 driver works by @igoropaniuk in [#233](https://github.com/linux-msm/qdl/pull/233)
- [RFC] qdl: add sha256 subcommand by @igoropaniuk in [#239](https://github.com/linux-msm/qdl/pull/239)
- github: Include missing build artifacts in Windows build by @quic-bjorande in [#240](https://github.com/linux-msm/qdl/pull/240)
- qdl: add --skipblock=sha256 to skip programs already on flash by @igoropaniuk in [#242](https://github.com/linux-msm/qdl/pull/242)
- For linux msm/erase all by @quic-bjorande in [#244](https://github.com/linux-msm/qdl/pull/244)
- contents: Look for OS-specific path tag by @quic-bjorande in [#245](https://github.com/linux-msm/qdl/pull/245)
- sahara: Deal with QUD discarding the HELLO request by @quic-bjorande in [#246](https://github.com/linux-msm/qdl/pull/246)
- firehose: detect VIP mode mismatch from programmer startup logs by @igoropaniuk in [#252](https://github.com/linux-msm/qdl/pull/252)
- program: Cast away const when freeing start_sector in erase by @igoropaniuk in [#253](https://github.com/linux-msm/qdl/pull/253)
- tree: drop unused .gitreview by @igoropaniuk in [#251](https://github.com/linux-msm/qdl/pull/251)
- sahara: auto-assemble minidump ELF after ramdump download by @mukeshojha-linux in [#243](https://github.com/linux-msm/qdl/pull/243)

### New Contributors
- @mukeshojha-linux made their first contribution in [#215](https://github.com/linux-msm/qdl/pull/215)
- @Zephyrus29 made their first contribution in [#213](https://github.com/linux-msm/qdl/pull/213)

**Full Changelog**: [v2.6...v2.7](https://github.com/linux-msm/qdl/compare/v2.6...v2.7)

## [v2.6](https://github.com/linux-msm/qdl/releases/tag/v2.6) - 2026-04-06

### What's Changed
- Documentation improvements by @igoropaniuk in [#194](https://github.com/linux-msm/qdl/pull/194)
- Address VIP mode regressions by @igoropaniuk in [#189](https://github.com/linux-msm/qdl/pull/189)
- A collection of bug fixes and a small refactor across vip/usb/firehose by @igoropaniuk in [#195](https://github.com/linux-msm/qdl/pull/195)
- usb: reinitialise libusb on each poll iteration in usb_open by @igoropaniuk in [#199](https://github.com/linux-msm/qdl/pull/199)
- A second batch of bug fixes and code quality improvements by @igoropaniuk in [#196](https://github.com/linux-msm/qdl/pull/196)
- qdl: fix absolute image_path handling in sahara_config by @MrCry0 in [#198](https://github.com/linux-msm/qdl/pull/198)
- program: Add erase command by @jcreedon in [#190](https://github.com/linux-msm/qdl/pull/190)
- [RFC] ci: enforce commit message formatting in PRs by @igoropaniuk in [#203](https://github.com/linux-msm/qdl/pull/203)
- Add qdl-ramdump usage documentation and fix segment filter matching by @igoropaniuk in [#202](https://github.com/linux-msm/qdl/pull/202)
- sahara, usb: improve robustness and user feedback by @igoropaniuk in [#204](https://github.com/linux-msm/qdl/pull/204)
- [RFC] sim: implement proper Firehose protocol state machine by @igoropaniuk in [#192](https://github.com/linux-msm/qdl/pull/192)
- [RFC] sim: validate VIP digest tables and chunk hashes in dry-run mode by @igoropaniuk in [#205](https://github.com/linux-msm/qdl/pull/205)

### New Contributors
- @MrCry0 made their first contribution in [#198](https://github.com/linux-msm/qdl/pull/198)

**Full Changelog**: [v2.5...v2.6](https://github.com/linux-msm/qdl/compare/v2.5...v2.6)

## [v2.5](https://github.com/linux-msm/qdl/releases/tag/v2.5) - 2026-02-25

### What's Changed
- makefile: Allow cross-building by @obbardc in [#174](https://github.com/linux-msm/qdl/pull/174)
- qdl: Allow absolute paths in Windows again by @andersson in [#179](https://github.com/linux-msm/qdl/pull/179)
- sahara: Unbreak ramdump by making images optional again by @andersson in [#180](https://github.com/linux-msm/qdl/pull/180)
- firehose: Increase reset delay after flashing by @ykaire-qti in [#175](https://github.com/linux-msm/qdl/pull/175)
- firehose: Increase timeout for read of program-ack by @andersson in [#182](https://github.com/linux-msm/qdl/pull/182)
- usb: add --list flag for list devices  by @lucarin91 in [#176](https://github.com/linux-msm/qdl/pull/176)
- For linux msm/drop single image hack by @andersson in [#181](https://github.com/linux-msm/qdl/pull/181)
- For linux msm/ramdump subcmd by @andersson in [#183](https://github.com/linux-msm/qdl/pull/183)
- qdl: Propagate the success of decode_sahara_config() by @andersson in [#185](https://github.com/linux-msm/qdl/pull/185)
- firehose: ensure we properly set skip_storage_init in provision by @kcxt in [#186](https://github.com/linux-msm/qdl/pull/186)
- Address memory leaks/mitigate buffer overflows by @igoropaniuk in [#161](https://github.com/linux-msm/qdl/pull/161)

### New Contributors
- @lucarin91 made their first contribution in [#176](https://github.com/linux-msm/qdl/pull/176)
- @kcxt made their first contribution in [#186](https://github.com/linux-msm/qdl/pull/186)

**Full Changelog**: [v2.4...v2.5](https://github.com/linux-msm/qdl/compare/v2.4...v2.5)

## [v2.4](https://github.com/linux-msm/qdl/releases/tag/v2.4) - 2025-12-19

### What's Changed
- For linux msm/read write gpt by @andersson in [#130](https://github.com/linux-msm/qdl/pull/130)
- For linux msm/missing files early and list h by @andersson in [#128](https://github.com/linux-msm/qdl/pull/128)
- checkpatch: Skip list.h by @quic-bjorande in [#138](https://github.com/linux-msm/qdl/pull/138)
- firehose: Provide progress bar on read as well by @quic-bjorande in [#142](https://github.com/linux-msm/qdl/pull/142)
- For linux msm/db410c timeout and improvements by @quic-bjorande in [#139](https://github.com/linux-msm/qdl/pull/139)
- sahara: Print an error when device isn't reachable by @quic-bjorande in [#140](https://github.com/linux-msm/qdl/pull/140)
- patch: Don't spam the log when no patch file was loaded by @quic-bjorande in [#141](https://github.com/linux-msm/qdl/pull/141)
- gpt: gpt_load_table_from_partition: fix off-by-one error for num_sectors by @mike-scott in [#143](https://github.com/linux-msm/qdl/pull/143)
- Add build instructions for MacPorts by @idlethread in [#144](https://github.com/linux-msm/qdl/pull/144)
- usb: increase bulk transfer timeout for firehose raw write by @loicpoulain in [#145](https://github.com/linux-msm/qdl/pull/145)
- Misc fixes by @igoropaniuk in [#147](https://github.com/linux-msm/qdl/pull/147)
- For linux msm/spinor by @andersson in [#146](https://github.com/linux-msm/qdl/pull/146)
- github: Bump macos runner versions by @andersson in [#151](https://github.com/linux-msm/qdl/pull/151)
- Add -h/--help support to ks and ramdump by @rogers0 in [#136](https://github.com/linux-msm/qdl/pull/136)
- github: Two improvements for --version on CI build by @z3ntu in [#152](https://github.com/linux-msm/qdl/pull/152)
- Suppress unused var warnings by @igoropaniuk in [#153](https://github.com/linux-msm/qdl/pull/153)
- Move __unused macro after variable definitions by @igoropaniuk in [#155](https://github.com/linux-msm/qdl/pull/155)
- Add manpages generated by help2man by @rogers0 in [#137](https://github.com/linux-msm/qdl/pull/137)
- For linux msm/multi programmer by @quic-bjorande in [#154](https://github.com/linux-msm/qdl/pull/154)
- README/CI updates/improvements by @igoropaniuk in [#157](https://github.com/linux-msm/qdl/pull/157)
- [housekeeping] Integer-related compiler warnings by @igoropaniuk in [#159](https://github.com/linux-msm/qdl/pull/159)
- Address more -Wsign-compare issues by @igoropaniuk in [#160](https://github.com/linux-msm/qdl/pull/160)
- firehose: Add slot flag by @jcreedon in [#158](https://github.com/linux-msm/qdl/pull/158)
- Check for empty patch filenames. by @ykaire-qti in [#156](https://github.com/linux-msm/qdl/pull/156)
- firehose/usb: Explicitly handle ZLP on USB read transfers by @loicpoulain in [#162](https://github.com/linux-msm/qdl/pull/162)
- github: Resolve liblzma-related failure in Windows builds by @andersson in [#165](https://github.com/linux-msm/qdl/pull/165)
- firehose: Increase image write timeout to 60 seconds by @andersson in [#166](https://github.com/linux-msm/qdl/pull/166)
- usb: Fix checkpatch warning about unnecessary parenthesis by @andersson in [#167](https://github.com/linux-msm/qdl/pull/167)
- vip: Fix integer underflow in digest count by @jerry-skydio in [#172](https://github.com/linux-msm/qdl/pull/172)

### New Contributors
- @mike-scott made their first contribution in [#143](https://github.com/linux-msm/qdl/pull/143)
- @idlethread made their first contribution in [#144](https://github.com/linux-msm/qdl/pull/144)
- @loicpoulain made their first contribution in [#145](https://github.com/linux-msm/qdl/pull/145)
- @rogers0 made their first contribution in [#136](https://github.com/linux-msm/qdl/pull/136)
- @jcreedon made their first contribution in [#158](https://github.com/linux-msm/qdl/pull/158)
- @ykaire-qti made their first contribution in [#156](https://github.com/linux-msm/qdl/pull/156)
- @jerry-skydio made their first contribution in [#172](https://github.com/linux-msm/qdl/pull/172)

**Full Changelog**: [v2.2...v2.4](https://github.com/linux-msm/qdl/compare/v2.2...v2.4)
