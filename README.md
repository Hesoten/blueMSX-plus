# blueMSX+

blueMSX+ is an unofficial fork of the MSX emulator [blueMSX](https://msxblue.com/bluemsx/).  
Modernization focuses on the UI and audio paths, targeting Windows 11.

[日本語版はこちら / Japanese version](README.ja.md)


## What's new in v3.1.0

- The bundled C-BIOS machines gained FDD and turbo R support ([v0.29+](https://github.com/Hesoten/cbios-nextor_FDD-and-turboR))
- Shortcuts, MSX keyboard keys, and controller buttons can now each be bound to up to three keys or buttons
- Improved cassette tape support — read/write for cas and wav, plus reading tsx images
- MSX mouse speed improvements and a sensitivity adjustment
- Improved MoonSound (OPL4) audio quality
- Added Direct3D 12 scaling filters
- Added Flash-ROM SCC (Developer Edition) support
- Expanded command line options
- Fixed the machines folder unintentionally landing under `<Documents>` in cases such as installing below Program Files
- Debugger UI and assorted bug fixes
- Improved ROM auto-detection, and fixed the zipped database failing to load (database also updated)
- VDP bug fixes (blink handling and others)
- Improved UI spots where text did not fit in some languages
- Many other smaller fixes and improvements

See [`blueMSX/changes.txt`](blueMSX/changes.txt) for the full change history.


## Key improvements over the original blueMSX

- **Native Windows 11 support**
  - 64-bit binary, dark-mode UI, and other modern Windows features
- **High-resolution display support**
  - Window zoom up to ×8 and DPI scaling
- **Direct3D 12 support**
- **WASAPI support**
  - Low-latency audio output via WASAPI (shared mode only)
- **MSX-MUSIC / MSX-AUDIO multi-backend**
  - Added high-quality FM synthesis emulators such as [Nuked-OPLL](https://github.com/nukeykt/Nuked-OPLL), [emu2413](https://github.com/digital-sound-antiques/emu2413), and [emu8950](https://github.com/digital-sound-antiques/emu8950)
  - Real-time switching (for A/B comparison)
- **Natural brightness compensation for scanlines under HDR**
- **TMS9918A (MSX1 VDP) color reproduction**
  - Reproduces the original TMS9918A colors (merged [uniskie's patch](https://uniskie.hatenablog.com/entry/ar1884677))
- **Recording modernization**
  - MP4 recording (H.264 or HEVC) for live capture and replay rendering
  - Preview window during replay rendering
- **XInput controller + hot-plug support**
- **MegaFlashROM SCC+ SD cartridge support**
  - Emulates the breadth of MegaFlashROM SCC+ SD features including the SD card  
    (overriding the internal MSX PSG via the cartridge-side PSG port is not yet supported)
- Other bug fixes and improvements


## System requirements

- Windows 11: verified that the major features of 64-bit blueMSX+ work
- Windows 10 (64-bit): likely works, but not verified
- Windows 10 (32-bit): 32-bit blueMSX+ may work, but not verified
- Direct3D 12-capable GPU and HDR-capable display recommended


## Notes & Disclaimer

- MSX is a registered trademark of MSX Licensing Corporation.

- blueMSX+ is **AS-IS** software with **no warranty** that it will always operate correctly. **Neither the blueMSX+ authors nor the original blueMSX authors / contributors accept any liability for damages of any kind (including but not limited to data loss, hardware damage, or financial loss) arising from the use of this software.**

- blueMSX+ is an **unofficial fork** of blueMSX. **Please do not direct inquiries about this software to the original blueMSX team or to any MSX-related companies / organizations.**

- **About using HDR mode on OLED displays**  
  The HDR scanline brightness-compensation feature uses locally elevated luminance — higher than that of regular pixels — to compensate for the screen being darkened by scanlines. **Displaying the same content at excessively high brightness settings, or extended continuous use, may accelerate burn-in on OLED displays.** Using it together with the display's built-in protection features, and exiting the emulator when it is not in use, is recommended.


## License

- Includes GPLv2-licensed source code, so blueMSX+ as a whole is licensed under GPLv2. Anyone is free to copy, modify, and redistribute it, but distributing a modified executable requires GPL-compliant handling such as publishing the modified source.
- See https://www.gnu.org/licenses/old-licenses/gpl-2.0.html for the full text of GPLv2.
- The files bundled with the C-BIOS machine configurations carry their own licenses:
  - C-BIOS: `cbios.txt` in each C-BIOS machine folder
  - Nextor: `LICENSE-Nextor.md` in each C-BIOS FDD machine folder
  - TC8566AF FDC driver: `LICENSE-TC8566AF.txt` in each C-BIOS FDD machine folder
- For the license of "Kanji ROM image file for msx emulaters", see `Machines/Shared Roms/LICENSE-KANJI.txt`.


## Installation

1. Download the release archive from [Releases](https://github.com/Hesoten/blueMSX-plus/releases)
   - Normally take the 64-bit archive (`x64`); download the `Win32` archive if you need the 32-bit build
2. Extract the archive anywhere you like
3. (Optional) If you own real MSX hardware and want to run its BIOS, place the BIOS ROM file(s) into the matching machine's folder under `Machines/` (use the filenames listed in that machine's `config.ini`)
4. Launch `blueMSX+.exe` from inside the extracted folder


### Reusing an existing blueMSX / blueMSX+ setup

If you already have a blueMSX or older blueMSX+ folder whose settings you want to keep, copy the release archive contents over that folder **preserving the directory layout** and launch `blueMSX+.exe`.

Note: launching `blueMSX+.exe` upgrades the existing blueMSX configuration files (`*.ini`) to the blueMSX+ format. The original `blueMSX.exe` in the same folder may no longer start or operate correctly afterwards.


## Recommended settings

After starting blueMSX+, the following adjustments from the `Options` menu let you enjoy higher-quality rendering and audio.

### Direct3D 12 renderer

Select **Direct3D 12** under `Options` → `Video` → `Driver`.  
This enables HDR output, high-quality scanlines with brightness compensation, monitor emulation, live recording, replay video rendering, and more.

### WASAPI (low-latency audio)

Select **WASAPI** under `Options` → `Sound` → `Driver`.  
A shorter `Sound buffer` setting matched to your PC environment yields lower-latency audio.

Note: the actual buffer size in use is determined by your PC's audio hardware. (Displayed as `Actual buffer: NN ms`.) Setting a lower value than this rounds up internally to the actual buffer size.

### MSX-MUSIC / MSX-AUDIO backend (optional)

Under `Options` → `Sound`, the `MSX-MUSIC backend` / `MSX-AUDIO backend` controls let you pick which FM synthesis emulator runs:

- **Nuked-OPLL**: high-accuracy YM2413 implementation by Nuke.YKT
- **emu2413**: high-quality YM2413 implementation by Mitsutaka Okazaki
- **emu8950**: high-quality Y8950 implementation by Mitsutaka Okazaki
- **openMSX**: the openMSX MSX-AUDIO implementation, ported in
- **original blueMSX**: the legacy implementation from original blueMSX

Only one backend per chip (MSX-MUSIC, MSX-AUDIO) can be audible at a time.  
When multiple backends are enabled, **emulation runs in all enabled backends simultaneously**. **CPU load grows accordingly,** but you can switch the audible output backend in real time via a hotkey or the settings dialog to A/B compare. Once you settle on a favorite, set it as the default (and disable the other backends).


## Using MegaFlashROM SCC+ SD

1. Download the openMSX-format file for MegaFlashROM SCC+ SD from the [MSX Cartridge Shop](https://www.msxcartridgeshop.com)
   - Flash → MegaFlashROM SCC+ SD → openMSX ROM (`mfrsd.zip`)
2. Extract `mfrsd.zip` and place `mfrsd.rom` in blueMSX+'s `Machines/Shared Roms/` folder
3. Launch blueMSX+ and select `Cartridge Slot 1 (or 2)` → `Insert Special` → `MegaFlashROM SCC+ SD` from the menu
4. From the `File` → `Hard Disk / SD Card` menu, either create a blank image or pick an existing image file to insert as the SD card


## Building from source

- Open the matching solution file (`Make/msvc2022/blueMSX.sln` or `Make/msvc2026/blueMSX.sln`) in **Visual Studio 2022** or **Visual Studio 2026** and build.


## Ideas for future releases

- More debugging features (VRAM and sprite viewers, a tracer, and so on)
- Better directory mounting (writing back, large disk support)
- A data recorder UI
- A reverse play UI (OSD?)
- More accurate VDP rendering timing
- Faster emulation
- vgm recording


## Acknowledgments

Many thanks to Daniel Vik and the rest of the original blueMSX development team and contributors for creating such an excellent MSX emulator.

openMSX has been a constant reference when extending and debugging blueMSX+. Many thanks and much respect to the openMSX development members and contributors, who continue to develop an outstanding MSX emulator.

Nuked-OPLL is a work by Nuke.YKT.  
emu2413 and emu8950 are works by Mitsutaka Okazaki.  
The TMS9918A patch is a work by uniskie.  
It is an honor to incorporate these excellent contributions into blueMSX+. Many thanks to all of them.

blueMSX+ ships with machine configurations based on C-BIOS, an open-source MSX BIOS replacement, extended here with turbo R and disk-boot support.  
This lets you run much of the MSX software library without owning a real MSX BIOS ROM.  
(Note: software that relies on MSX-BASIC will not run.)

For details on C-BIOS, see the bundled `cbios.txt` in each C-BIOS machine folder under `Machines/`, or
<https://cbios.sourceforge.net/>.  
Many thanks to the C-BIOS project — BouKiCHi, Reikan, Maarten ter Huurne, Albert Beevendorp, Patrick van Arkel, Manuel Bilderbeek, Joost Yervante Damad, Jussi Pitkänen, Eric Boon, and the other contributors — for developing such an excellent compatible BIOS and releasing it in freely redistributable form.

The C-BIOS FDD machines use **Nextor** as their disk kernel.  
Many thanks to Nestor Soriano Vilchez (Konamiman) for creating Nextor and continuing to improve it, and for releasing the source code in a form that allows a project like this to exist.

The JP and turbo R C-BIOS machines use "Kanji ROM image file for msx emulaters" by A to C.
Many thanks to A to C for creating this freely redistributable Kanji font ROM, built from the public-domain jiskan16 font plus hand-drawn MSX-specific glyphs.

Many thanks as well to everyone who has sent feedback and feature requests, on the MSX Resource Center, GitHub, and elsewhere.

blueMSX+ enhancements are developed using Claude Code.  
Its ability to ship feature requests and bug fixes one after another is a constant source of amazement — and a little awe.
