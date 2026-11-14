#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Y8960 OPLLEX preset voice file from scripts/y8960-opll-voices.txt.

Run from a developer shell with `python scripts/make-y8960-opll-voices.py`.
The output, blueMSX/ReleaseFiles/Machines/Shared Roms/y8960_opll_voices.bin,
holds four banks of 18 voices (voices 1-15, then the three rhythm entries),
eight bytes each, 576 bytes in all. The emulator takes a file of any other
size as missing and plays emu2413's presets instead.
"""
import argparse
from pathlib import Path
import re

BANKS = 4
VOICES_PER_BANK = 18
VOICE_BYTES = 8


def parse(text):
    voices = []
    for number, line in enumerate(text.splitlines(), 1):
        fields = line.split('#', 1)[0].split()
        if not fields:
            continue
        if len(fields) != VOICE_BYTES or not all(re.fullmatch(r'[0-9A-Fa-f]{2}', f) for f in fields):
            raise SystemExit(f'line {number}: expected {VOICE_BYTES} hex bytes')
        voices.append(bytes(int(f, 16) for f in fields))
    if len(voices) != BANKS * VOICES_PER_BANK:
        raise SystemExit(f'{len(voices)} voices; expected {BANKS * VOICES_PER_BANK}')
    return b''.join(voices)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source', type=Path, default=root / 'scripts/y8960-opll-voices.txt')
    parser.add_argument('--output', type=Path,
                        default=root / 'blueMSX/ReleaseFiles/Machines/Shared Roms/y8960_opll_voices.bin')
    args = parser.parse_args()
    data = parse(args.source.read_text(encoding='utf-8'))
    args.output.write_bytes(data)
    print(f'{args.output}: {len(data)} bytes')


if __name__ == '__main__':
    main()
