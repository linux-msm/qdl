# Releasing QDL

This document describes how a new QDL release is cut, from preparing the
changelog to publishing the GitHub release with prebuilt artifacts.

Releases are versioned `vMAJOR.MINOR` (for example `v2.8`). Release
candidates append an `-rcN` suffix (`v2.8-rc1`, `v2.8-rc2`, ...).

The process is, in short:

- [1. Populate the changelog](#1-populate-the-changelog)
- [2. Open the changelog PR and tag a release candidate](#2-open-the-changelog-pr-and-tag-a-release-candidate)
- [3. Iterate on release candidates](#3-iterate-on-release-candidates)
- [4. Merge the changelog PR and apply the release tag](#4-merge-the-changelog-pr-and-apply-the-release-tag)
- [5. Create the GitHub release](#5-create-the-github-release)

Throughout, export the version once so the commands below can be copied
verbatim:

```bash
VERSION=v2.8
```

## 1. Populate the changelog

`CHANGELOG.md` mirrors the GitHub releases: one section per version, listing
the pull requests merged since the previous release. Add a new section at the
top for the upcoming version.

GitHub can produce the raw list of merged PRs for the range, which is the same
content used in the existing entries:

```bash
# Everything merged since the previous release:
gh api repos/linux-msm/qdl/releases/generate-notes \
    -f tag_name="${VERSION}" -f target_commitish=master \
    -f previous_tag_name="$(git describe --tags --abbrev=0 master)" \
    --jq .body
```

Format the output to match the existing entries (PR references as `#NNN`
links, a `### What's Changed` and `### New Contributors` subsection, and a
`**Full Changelog**` compare link), and prepend it to `CHANGELOG.md` under a
new `## [vX.Y](...) - YYYY-MM-DD` heading.

Once the changelog draft is ready, ask people to start testing the current
`master` so issues are found before the tag is cut.

## 2. Open the changelog PR and tag a release candidate

Open a pull request with the changelog update. With that PR up, tag the latest
`master` as the first release candidate so testers have a stable reference
point to build from:

```bash
git fetch upstream
git tag "${VERSION}-rc1" upstream/master
git push upstream "${VERSION}-rc1"
```

Ask testers to exercise the hardware-in-the-loop (HIL) suite against real
hardware. The suite is skipped by default and only runs when pointed at an
attached EDL device:

```bash
meson setup build
QDL_HIL_BUILD=/path/to/build-images \
QDL_HIL_STORAGE=<emmc|ufs|nvme|...> \
    meson test -C build --suite hil --print-errorlogs
```

The release must be validated across all four supported host/driver
combinations:

- Windows + QUD driver: the official Qualcomm QDLoader 9008 driver
  (`qdl --backend=qud ...`).
- Windows + WinUSB driver / libusb (`qdl --backend=usb ...`).
- Linux.
- macOS.

In addition, validate VIP (Validated Image Programming) mode: generate the
digest tables with `--create-digests`, sign them, and flash a board with
`--vip-table-path` to confirm secure-boot flashing still works end to end. See
the VIP section in [README.md](README.md) for the full workflow.

## 3. Iterate on release candidates

Collect the testing results. If a regression is found, fix it on `master` and
cut the next candidate (`-rc2`, `-rc3`, ...):

```bash
git fetch upstream
git tag "${VERSION}-rc2" upstream/master
git push upstream "${VERSION}-rc2"
```

Repeat until a candidate passes all four configurations (and VIP validation)
cleanly. That commit becomes the stable release.

## 4. Merge the changelog PR and apply the release tag

Merge the changelog PR. Then tag the resulting `master` commit with the final,
immutable release tag.

Note on tag type: the history is mixed. `v2.4` is an annotated tag while
`v2.5`, `v2.6` and `v2.7` are lightweight tags (that is what GitHub's "create
release" UI produces). Prefer an annotated tag for releases: it records the
tagger, date and a message, and is what `git describe` expects.

```bash
git fetch upstream
git tag -a "${VERSION}" -m "qdl ${VERSION}" upstream/master
git push upstream "${VERSION}"
```

A tag is a ref, not a branch. `git push upstream "${VERSION}"` publishes the
tag itself; it does not push to the `master` branch. Once pushed, treat the tag
as immutable: never move or delete it.

(If matching the recent lightweight-tag convention is preferred instead, drop
the `-a`/`-m` flags: `git tag "${VERSION}" upstream/master`.)

## 5. Create the GitHub release

The `Buildtest` workflow already builds and uploads per-platform binaries for
every push. Reuse those artifacts for the release rather than rebuilding.

Download the artifacts from the green `Buildtest` run for the tagged commit and
bundle each platform into its own archive:

```bash
# Find the successful Buildtest run that built the tagged commit. Filtering
# with --commit happens server-side, so it is not limited to the most recent
# runs (a plain `gh run list` only fetches the latest 20).
SHA=$(git rev-parse "${VERSION}^{commit}")
RUN_ID=$(gh run list --workflow build.yml --commit "${SHA}" --status success \
    --json databaseId --jq '.[0].databaseId')

# Pull every platform artifact into dist/<artifact-name>/...
rm -rf dist && gh run download "${RUN_ID}" -D dist

# Zip each platform directory into a single release asset
( cd dist && for d in */; do
      name="${d%/}"
      zip -r "${name}-${VERSION}.zip" "${d}"
  done )
```

Extract this release's notes straight from `CHANGELOG.md` (the block between
this version's heading and the next `## [` heading) and create the release,
attaching the zipped artifacts:

```bash
NOTES=$(awk -v v="${VERSION}" '
    $0 ~ ("^## \\[" v "\\]") { found=1; next }
    /^## \[/ && found        { exit }
    found                    { print }
' CHANGELOG.md | sed '1{/^$/d}')

echo "Release notes preview:"
echo "---"
echo "${NOTES}"
echo "---"
read -r -p "Create GitHub release ${VERSION}? [y/N] " reply
[ "${reply}" = "y" ] || { echo "GitHub release skipped"; exit 0; }

gh release create "${VERSION}" dist/*.zip \
    --title "${VERSION}" \
    --notes "${NOTES}"

echo "GitHub release created: $(gh release view "${VERSION}" --json url -q .url)"
```

The release name matches the existing convention (the tag itself, for example
`v2.8`). After publishing, sanity-check the release page: confirm the notes
rendered correctly and that every platform archive is attached.
