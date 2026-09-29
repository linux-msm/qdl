# Installer packages and contents.xml

## Flashing installer packages

If you have an installer package instead of individual binaries and XML
definitions, you can flash this using the *flash* subcommand:

```bash
qdl flash <installer.zip>
```

If the *installer package* is unpacked it can be installed as:

```bash
qdl flash flashmap.json
```

These can of course be combined with e.g. *--serial*.

A subset of the installer package can be selected for installation by appending
a **::storage1[,storage2...]** suffix to the file name.

If a flashmap contains multiple layouts, select the desired layout by appending
**::layout-name**. The layout selector can be combined with storage filters
using **::layout-name/storage1[,storage2...]**, for example:

```bash
qdl flash installer.zip::layout1/ufs
```

## Flashing contents.xml

QDL also supports flashing builds described by *contents.xml* files:

```bash
qdl flash contents.xml
```

As the contents XML can describe the content for multiple storage types and
multiple flavors, it might be necessary to select which content to flash. This
is done by appending the **::specifier1,specifier2...** suffix to the file
name. The specifier is matched against **storage types** and **flavors**. At
most one resolved specifier per storage is allowed, and only the selected parts
are flashed. As an example:

```bash
qdl flash contents.xml::ufs,safe_rtos
```

will flash the UFS storage with the only applicable flavor, and will flash
*safe_rtos* onto the spinor.

## Creating installer packages

QDL can also create installer packages from builds described by *contents.xml*
files. The generated zip contains a `flashmap.json`, the selected programmer
images, and the referenced rawprogram, patch, and image files:

```bash
qdl create-zip installer.zip contents.xml
```

When the contents XML describes multiple storage or flavor combinations, select
the desired content with the same `::specifier1,specifier2...` suffix used for
flashing contents XML files:

```bash
qdl create-zip installer-ufs.zip contents.xml::ufs
```

The resulting archive can be flashed later with `qdl flash installer.zip`.
Creating and flashing zip archives requires QDL to be built with libzip support,
which is enabled by default.
