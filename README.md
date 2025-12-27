# ptouch-print (extended fork)

**Notice:** This repository is a fork of DavidPhillipOster/ptouch-print-macOS, itself derived from Dominic Radermacher's original ptouch-print project. All credit for the base code goes to those authors.

This fork focuses on label cutting control and predictable image handling for Brother P-Touch printers on macOS.

## What's different here?

- **Extended cutting controls.** Adds runtime options for automatic pre-cut, explicit chain cuts between segments, and optional post-cut suppression. These changes are aimed at reducing leader waste on devices that support hardware cutting.
- **No automatic image scaling.** PNG assets are sent to the printer at their original resolution. Prepare artwork at the correct tape pixel height before printing.
- **Target hardware.** Features have only been tested on the Brother **PT-2730**. Other models in `libptouch.c` remain unverified.

## Quick start

1. Open `ptouch-print.xcodeproj` in Xcode and build the `ptouch-print` target.
2. The command-line tool is produced at `build/Release/ptouch-print`.
3. Connect the printer via USB and confirm the tape info:

```sh
./build/Release/ptouch-print --info
```

4. Print a simple label:

```sh
./build/Release/ptouch-print --text "Hello"
```

## Usage guide

### Command syntax

```text
ptouch-print [options] <print-command(s)>
```

- Options set global behavior (font, output mode, cutter defaults).
- Print commands append content to the current label buffer in the order you provide them.
- The buffer prints at the end of the command line, or immediately when you pass `--cut`.

### Options

- `--font <file|name>`: Use a font file or font name (default: `Helvetica`).
- `--fontsize <n>`: Force a specific font size in points.
- `--writepng <file>`: Write the output to a PNG instead of printing. This only works with **exactly one** `--text` command.
- `--info`: Print tape and device info (max print width in pixels, media type, width in mm, colors, error code) and exit.
- `--debug`: Enable verbose debug output while rendering/printing.
- `--version`: Print version info and exit.

### Print commands

- `--image <file>`: Print the given PNG image (black/white recommended).
- `--text <text> [<text> ...]`: Print 1-4 lines of text. If the text contains spaces, wrap it in quotes.
- `--cutmark`: Print a dashed cut mark on the tape.
- `--pad <n>`: Add `n` pixels of blank tape (1-256 px; values outside this range are clamped to 1).
- `--cut`: Flush the current label, cut it, and start a new label buffer.
- `--no-precut`: Disable automatic pre-cut before printing.
- `--no-postcut`: Disable cutting after the final label in the command line.

### Cutting behavior

- **Default:** Both pre-cut and post-cut are enabled.
- **Pre-cut** (`--no-precut` to disable): The printer performs an automatic pre-cut/advance if the hardware supports it.
- **Post-cut** (`--no-postcut` to disable): The printer cuts after the final label at the end of the command line.
- **Explicit cut** (`--cut`): Forces a cut immediately and keeps printing subsequent content into a new label buffer.
- `--cut` cannot be combined with `--writepng`.

### Image preparation

- Only PNG files are supported.
- Image height must be **less than or equal to** the maximum print width for the current tape. Use `--info` to see the exact pixel width.
- Images are not scaled; they are centered within the printable width.
- Pixels are thresholded to on/off at a grayscale value of 128, so high-contrast, two-color art works best.

### Text rendering notes

- `--text` accepts 1-4 lines after a single `--text` flag (e.g., `--text "Line 1" "Line 2"`).
- If `--fontsize` is not set, font size is automatically chosen to fit the tape width and line count.
- Use `--font` to provide a font name or a path to a font file.

### Examples

Print two lines with a custom font:

```sh
./build/Release/ptouch-print --text "Line 1" "Line 2"
```

Print a PNG image (height must match tape width):

```sh
./build/Release/ptouch-print --image ./assets/logo.png
```

Create a PNG preview (text only, single --text command):

```sh
./build/Release/ptouch-print --writepng preview.png --text "Preview"
```

Print two labels with a cut between them:

```sh
./build/Release/ptouch-print \
  --text "First" \
  --cut \
  --text "Second"
```

Add padding and a cut mark on a continuous strip:

```sh
./build/Release/ptouch-print \
  --no-postcut \
  --text "Section A" \
  --pad 40 \
  --cutmark \
  --text "Section B"
```

### Batch printing from a zip

`print_zip_on_ptouch.sh` is a small helper that prints every PNG inside a zip file, cutting between each image.

```sh
./print_zip_on_ptouch.sh /path/to/labels.zip
```

Notes:

- Images are printed in sorted path order.
- Requires one of: `unzip`, `7z`, or macOS `ditto`.
- If `ptouch-print` is not on your `PATH`, set `PTOUCH_PRINT` (or build via Xcode and it will fall back to `./build/Release/ptouch-print`).

## Building

The project remains self-contained. Open `ptouch-print.xcodeproj` in Xcode and use **Product > Run**, or build the command-line tool via the provided scheme.

### CLI build

List schemes:

```sh
xcodebuild -list -project ptouch-print.xcodeproj
```

Build Release from the CLI:

```sh
xcodebuild -project ptouch-print.xcodeproj -scheme ptouch-print -configuration Release
```

The binary is produced at `build/Release/ptouch-print`.

## License

This fork retains the GNU GPL 3 license of the upstream projects. Included `libusb` sources remain under their original MIT license.
