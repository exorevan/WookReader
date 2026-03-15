# WookReader

A comic and e-book reader for the Nintendo Switch.

Fork of eBookReaderSwitch with major improvements to performance, format support, and usability.

## Features

- **Format support:** PDF, EPUB, XPS, CBZ, CBR, CBT, CB7
- **Reading modes:** Portrait, Landscape, Vertical (fit-to-width), Spread (two-page)
- **Cover grid browser** with folder navigation and thumbnail previews
- **Async prefetch** — next/previous pages pre-rendered for instant page turns
- **Raw image LRU cache** — revisited pages load instantly
- **Early first-page display** — cover shown in ~0.5s while large archives enumerate in background
- **Page-name disk cache** — second+ open of any comic skips enumeration entirely
- **RAR3 fast enumeration** — header-only scan (~50ms for 500MB solid RAR)
- **Progressive navigation** — flip through pages while archive is still scanning
- **Analog stick scrolling** — full 360° proportional panning with left stick
- **Pinch-to-zoom** and right-stick zoom
- **Dark and light mode**
- **Saves last page, orientation, and dark mode settings**
- **Sorted file browser** (folders first A-Z, files A-Z, case-insensitive)

## Installation

1. Copy `WookReader.nro` to `/switch/` on the SD card
2. Create the folder `/switch/WookReader/`
3. Put your comics and books in folders inside `/switch/WookReader/`

## Controls

| Input | Portrait / Vertical | Landscape |
|---|---|---|
| D-Pad Left/Right | Prev / Next page | Prev / Next page |
| D-Pad Up/Down | Zoom max / Zoom out | Prev / Next page |
| ZL / ZR | Prev / Next page | Zoom out / Zoom in |
| R+ZR / L+ZL | Skip 9 pages forward/back | Skip 9 pages forward/back |
| Left Stick | Analog scroll (360°) | Analog scroll (360°) |
| Right Stick | Zoom in/out | Prev / Next page |
| Y | Cycle layout modes | Cycle layout modes |
| Touch | Drag=scroll, Pinch=zoom, Tap sides=page turn | Drag=scroll, Pinch=zoom |

## Screenshots

Dark Mode Folders:

<img src="screenshots/NewDarkModeFolder.jpg" width="512" height="288">

Dark Mode Reading:

<img src="screenshots/NewDarkModeBook.jpg" width="512" height="288">

Dark Mode Options:

<img src="screenshots/NewDarkModeOptions.jpg" width="512" height="288">

Light Mode Folders:

<img src="screenshots/NewLightModeFolders.jpg" width="512" height="288">

Light Mode Reading:

<img src="screenshots/NewLightModeBook.jpg" width="512" height="288">

## Building

Requires [devkitPro](https://devkitpro.org/) with devkitA64 and Switch portlibs:

```
pacman -S libnx switch-portlibs
```

Build:

```
make mupdf
make
```

If you don't have twili debugger installed, remove the `-ltwili` flag from the Makefile.

## Credits

- moronigranja — expanded file format support
- NX-Shell Team — original application code base
- Papirus Icon Theme — folder/file icon design inspiration
