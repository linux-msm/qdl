# Contributing

Please submit any patches to the qdl (`master` branch) by using the GitHub pull
request feature. Fork the repo, create a branch, do the work, rebase with upstream,
and submit the pull request.

The preferred coding style for this tool is [Linux kernel coding style](https://www.kernel.org/doc/html/v6.15/process/coding-style.html).

Before creating a commit, please ensure that your changes adhere to the coding style
by using the `meson compile check-cached -C build` target, for example:

```bash
$ git status
On branch improvements
Changes to be committed:
  (use "git restore --staged <file>..." to unstage)
  modified:   qdl.c
  modified:   qdl.h

$ meson compile check-cached -C build
[0/1] Running external command check-cached (wrapped by meson to set env)
Running checkpatch on staged changes...
ERROR: trailing whitespace
#28: FILE: qdl.h:32:
+^IQDL_DEVICE_USB,   $

total: 1 errors, 0 warnings, 0 checks, 27 lines checked

NOTE: For some of the reported defects, checkpatch may be able to
      mechanically convert to the typical style using --fix or --fix-inplace.

NOTE: Whitespace errors detected.
      You may wish to use scripts/cleanpatch or scripts/cleanfile

Your patch has style problems, please review.
```

To verify a series of commits the same way the CI does (per-commit, in
patch mode), use the `check-range` target. It runs checkpatch on every
commit in `$CHECKPATCH_BASE..$CHECKPATCH_HEAD` (defaulting to
`origin/master..HEAD`):

```bash
meson compile check-range -C build
```

To restrict the range explicitly, set the environment variables before
invoking meson:

```bash
CHECKPATCH_BASE=origin/master CHECKPATCH_HEAD=HEAD \
    meson compile check-range -C build
```

The full file-mode check (run against every tracked C/H/sh source) is
also available:

```bash
meson compile check -C build
```

Markdown sources are linted with
[mdl](https://github.com/markdownlint/markdownlint). Install it via
`sudo apt install markdownlint` (or `gem install mdl`) and run:

```bash
meson compile markdown-lint -C build
```

The wrapper script is invoked from the GitHub Actions workflow as well,
so a green local run reflects what CI will report.
