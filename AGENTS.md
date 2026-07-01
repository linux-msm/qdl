# Agent Guide for QDL

This file guides automation agents working in the QDL repository. Human
contributors should read [CONTRIBUTING.md](CONTRIBUTING.md); this document
captures the same rules in a form suited to automated workflows.

## Project overview

QDL is a command-line tool for flashing firmware on Qualcomm-based devices
over USB in Emergency Download (EDL) mode. It uploads a flash loader to the
device and drives it using the Sahara and Firehose protocols.

## Prerequisites and build

### Linux

```bash
sudo apt install libxml2-dev libusb-1.0-0-dev libzip-dev meson ninja-build help2man
meson setup build
meson compile -C build
```

### macOS (Homebrew)

```bash
brew install libxml2 libusb libzip meson ninja help2man
meson setup build
meson compile -C build
```

### macOS (MacPorts)

```bash
sudo port install libxml2 libusb libzip meson ninja help2man
meson setup build
meson compile -C build
```

## Before committing

Every change must build cleanly and pass the style and test checks. Run:

```bash
meson setup build              # once, or after changing meson.build
meson compile -C build         # build must succeed
meson test -C build            # run the test suite
meson compile check -C build   # checkpatch (Linux kernel style)
meson compile markdown-lint -C build   # lint Markdown sources
```

To check only staged changes against coding style, use
`meson compile check-cached -C build`. To validate a whole series the way CI
does (per-commit, patch mode), use `meson compile check-range -C build`.

## Coding style

- Follow the [Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html).
- Style violations are caught by the `check` / `check-cached` targets above.
- Use ASCII only. Neither code, documentation, nor commit messages may
  contain non-ASCII characters. Replace them with ASCII alternatives, e.g.
  use `-` or `--` instead of en/em dashes, straight `'` and `"` instead of
  typographic quotes, and `...` instead of an ellipsis character.
- Use appropriate SPDX license identifiers in sources files

## Commit conventions

Every commit message must follow the repository convention:

- **The subject must carry a subsystem tag.** Prefix it with the affected
  subsystem, followed by a lowercase, imperative summary, e.g.
  `usb: add support for listing devices`,
  `firehose: report progress through the skipblock fast-path`,
  `github: package self-contained binaries on Linux and macOS`. Common
  prefixes include `qdl`, `usb`, `sahara`, `firehose`, `program`, `vip`,
  `ramdump`, `tests`, `github`, `meson`, `README`, and `doc`. Use `github:`
  (not `ci:`) for CI and workflow changes.
- **The commit must have a body explaining the rationale** - why the change
  is needed and any key design decisions - not just what changed. The subject
  alone is not sufficient for anything beyond a trivial fix.
- Keep the subject short and specific; capture intent, not a file-by-file dump.
- Use the imperative mood (Add, Update, Drop, Enable, Revert).
- Wrap body lines at ~72 characters.
- Do not mix unrelated changes in one commit; split them logically.
- Each patch must be logically coherent, self-contained, and independently
  buildable. The tree must remain functional after every commit.

## Sign-off

Every commit must carry a `Signed-off-by` trailer using the identity from the
local git configuration.

```bash
Signed-off-by: $(git config user.name) <$(git config user.email)>
```
When committing programmatically,
**AI agents MUST NOT add Signed-off-by tags**, only a human can certify the
Developer Certificate of Origin (DCO).
Do not add any other trailers (for example, co-author trailers).

## Submitting

Submit patches against the `master` branch as a GitHub pull request: fork the
repo, create a branch, do the work, rebase onto upstream, and open the PR.
