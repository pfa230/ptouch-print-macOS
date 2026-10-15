# ptouch-print (extended fork)

**Notice:** This repository is a fork of DavidPhillipOster/ptouch-print-macOS, itself derived from Dominic Radermacher's original ptouch-print project. All credit for the base code goes to those authors.

This fork focuses on label cutting control and predictable image handling for Brother P-Touch printers on macOS.

## What's different here?

- **Extended cutting controls.** Adds runtime options for automatic pre-cut, explicit chain cuts between segments, and optional post-cut suppression. These changes are aimed at reducing leader waste on devices that support hardware cutting.
- **No automatic image scaling.** PNG assets are sent to the printer at their original resolution. Prepare artwork at the correct tape pixel height before printing.
- **360 dpi printers.** Supports the PT-P900 series raster protocol (560-pin printhead, 360 dpi), including the Brother **PT-P910BT**.
- **Bluetooth.** Prints to a paired PT-P910BT over Bluetooth as well as USB.
- **Target hardware.** Cutting features have been tested on the Brother **PT-2730**; 360 dpi and Bluetooth support on the **PT-P910BT**. Other models in `libptouch.c` remain unverified.

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
- `--bluetooth [name]`: Print over Bluetooth instead of USB. `name` is (part of) the paired printer's name or its address; by default the first paired `PT-*` printer is used. See [Bluetooth](#bluetooth).
- `--info`: Print printer and tape info (model, dpi, max print width in pixels, media type, width in mm, colors, error code) and exit.
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
- Pixels are printer dots: 180 dpi on most models, 360 dpi on the PT-P900 series and PT-P910BT. For example, 24 mm tape is 128 px tall at 180 dpi but 320 px at 360 dpi.
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

## PT-P910BT and other 360 dpi printers

The PT-P910BT, PT-P900Wc and other PT-P900 series printers have a 560-pin, 360 dpi printhead and use the command sequence from Brother's [Raster Command Reference PT-P900/P900W/P950NW/P910BT](https://download.brother.com/welcome/docp100407/cv_ptp900_eng_raster_102.pdf). `--info` shows the model and resolution:

```text
printer = PT-P910BT (360 dpi, 560 px printhead)
maximum printing width for this tape is 320px
```

Printable heights at 360 dpi: 3.5 mm = 48 px, 6 mm = 64 px, 9 mm = 106 px, 12 mm = 150 px, 18 mm = 234 px, 24 mm = 320 px, 36 mm = 454 px. Only 24 mm tape has been tested on hardware so far.

## Bluetooth

The PT-P910BT can print over Bluetooth:

1. Pair the printer in **System Settings > Bluetooth**.
2. Print with `--bluetooth`, or just leave the USB cable unplugged: when no USB printer is found, `ptouch-print` falls back to the first paired `PT-*` printer.

```sh
./build/Release/ptouch-print --bluetooth --text "Hello"
```

Notes:

- The first time, macOS asks whether `ptouch-print` may use Bluetooth. If it is denied, enable it under **System Settings > Privacy & Security > Bluetooth**. Rebuilding the tool may make macOS ask again.
- Connecting takes a couple of seconds, or longer when the printer has just woken up. If the printer has turned itself off, it reports a connection timeout.
- The `/dev/cu.PT-P910BT…` serial port that macOS creates for the printer does not work; `ptouch-print` talks to the printer through IOBluetooth instead.
- `--debug` shows the printer's Bluetooth services and its status replies.

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
