# blueMSX+

blueMSX+ is an unofficial fork of the MSX emulator [blueMSX](https://msxblue.com/bluemsx/).  
Modernization focuses on the UI and audio paths, targeting Windows 11.

[日本語版はこちら / Japanese version](README.ja.md)


## What's new in v3.0.1

- **Self-contained package** — the upstream blueMSX data files that 3.0.0 required you to install separately are now bundled
- Also bundles the latest Rom/Cas/Disk database and cheat database published on the [official blueMSX site](https://msxblue.com/bluemsx/resource.html)
- **C-BIOS 0.29a** bundled
- Release binaries are no longer UPX-packed (avoids antivirus false positives)
- Added support for new memory mappers: **ASCII16-X**, **NEO-8**, **NEO-16**, and **Yamanooto**
- Emulator start failure now reports a specific cause instead of a generic error

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
- **Other bug fixes and improvements**
  - Fixed sprite rendering issues (e.g. position offset of magnified sprites)
  - Fixed VDP command behavior in SCREEN 0–4
  - Fixed R800 block I/O instruction flag handling
  - Fixed TurboR PCM frequency and write timing
  - Added long filename support for directory-as-disk insertion + warning when contents exceed 720 KB
  - And more


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
  The HDR scanline brightness-compensation feature uses locally elevated luminance — higher than that of regular pixels — to compensate for the screen being darkened by scanlines. **Displaying the same content at excessively high brightness settings, or extended continuous use, may accelerate burn-in on OLED displays.** We recommend using it in combination with the display's built-in protection features, and exiting the emulator when it is not in use.


## License

- Includes GPLv2-licensed source code, so blueMSX+ as a whole is licensed under GPLv2. Anyone is free to copy, modify, and redistribute it, but distributing a modified executable requires GPL-compliant handling such as publishing the modified source.
- See https://www.gnu.org/licenses/old-licenses/gpl-2.0.html for the full text of GPLv2.


## Installation

1. Download the release archive from [Releases](https://github.com/Hesoten/blueMSX-plus/releases)
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


## Acknowledgments

We extend our deepest gratitude to Daniel Vik and the rest of the original blueMSX development team and contributors for creating such an excellent MSX emulator.

When extending and debugging blueMSX+, we frequently consulted openMSX. Our respect and gratitude also go to the openMSX development members and contributors, who continue to develop an outstanding MSX emulator.

Nuked-OPLL is a work by Nuke.YKT.  
emu2413 and emu8950 are works by Mitsutaka Okazaki.  
The TMS9918A patch is a work by uniskie.  
We are honored to incorporate these excellent contributions into blueMSX+. Our heartfelt thanks.

blueMSX+ ships with **C-BIOS** 0.29a, an open-source MSX BIOS replacement, so it can boot without any real MSX system BIOS ROM.
See the bundled `cbios.txt` in each C-BIOS machine folder under `Machines/`, or <https://cbios.sourceforge.net/>, for details.
Our thanks to the C-BIOS project — BouKiCHi, Reikan, Maarten ter Huurne, Albert Beevendorp, Patrick van Arkel, Manuel Bilderbeek, Joost Yervante Damad, Jussi Pitkänen, Eric Boon, and the other contributors — for developing such an excellent compatible BIOS and releasing it in freely redistributable form.

blueMSX+ enhancements are developed using Claude Code.  
We are continually amazed (and a little awed) by its ability to ship feature requests and bug fixes one after another.
