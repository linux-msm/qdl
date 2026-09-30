# Multi-programmer targets

On some targets multiple files need to be loaded in order to reach the
Firehose programmer; these targets will request multiple images over Sahara.
Three mechanisms for providing these images are provided:

## Command line argument

The *programmer* argument allows specifying a comma-separated list of
colon-separated "id" and "filename" pairs. Each filename should refer to the
Sahara image of the specified Sahara image id.

```bash
qdl 13:prog_firehose_ddr.elf,42:the-answer rawprogram.xml
```

## Sahara configuration XML file

Flattened METAs include the various images that need to be loaded to
enter Firehose mode, as well as a sahara_config XML file, which defines the
Sahara image id for each of these images.

If the specified device programmer is determined to be a Sahara configuration
XML file, it will be parsed and the referenced files will be loaded and
serviced to the device upon request.

```bash
qdl sahara_programmer.xml rawprogram.xml
```

## Programmer archive

Directly providing a list of ids and filenames is cumbersome and error-prone,
so QDL accepts a "*programmer archive*". This allows the user to use the
tool in the same fashion as was done for single-programmer targets.

The *programmer archive* contains the Sahara images to be loaded, identified by
the Sahara *id* needed by the target.

QDL can create such an archive from the same programmer descriptions accepted
for flashing. To create an archive from a command-line Sahara image list:

```bash
qdl create-sahara-archive programmer.bin 13:prog_firehose_ddr.elf,42:the-answer
```

To create an archive from a Sahara configuration XML file:

```bash
qdl create-sahara-archive programmer.bin sahara_programmer.xml
```

To create an archive from a contents XML file:

```bash
qdl create-sahara-archive programmer.bin contents.xml
```

When the contents XML describes multiple storage or flavor combinations, select
exactly one with the same `::specifier` syntax used by `flash`:

```bash
qdl create-sahara-archive programmer.bin contents.xml::ufs
```

*programmer.bin* can now be passed to QDL and the included images will be served
in order to reach Firehose mode.
