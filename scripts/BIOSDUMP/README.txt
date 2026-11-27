====================================================
 blueMSX+ BIOS SLOT DUMPER  (BIOSDUMP.COM)
====================================================

This program runs on a real MSX. It reads a blueMSX+
Machines/<machine>/config.ini and dumps the BIOS ROM of
each [Slots] entry.

It has been tested on real hardware only on an FS-A1GT (MSX-DOS 2).


Copyright and legal note
------------------------

BIOS ROMs are copyrighted works of their manufacturers.
Dump them only from hardware you own, and use them
only for your own personal use.
Do not distribute, share or upload the dumped ROM files.


Files
-----

- BIOSDUMP.COM  the program, run from MSX-DOS
- BIOSDUMP.ASM  source code of BIOSDUMP.COM (Z80 assembly for sjasmplus)
- build.ps1     PowerShell script that builds BIOSDUMP.COM from
                BIOSDUMP.ASM (run on the PC; needs sjasmplus)
- join.ps1 / join.bat
                PowerShell script / batch file that joins a ROM
                written in parts into one file (run on the PC)


Requirements
------------

- MSX1 / MSX2 / MSX2+ / MSX turbo R (Z80, R800)
- MSX-DOS 1, MSX-DOS 2 or Nextor
- 64 KB of RAM
- A drive to write to (floppy disk, SD card, etc.)


Usage
-----

1. Put these files in the root of a disk the MSX can use (floppy
   disk, SD card, Wi-Fi cartridge, etc.):

   - MSX-DOS system files
       MSX-DOS 2: MSXDOS2.SYS and COMMAND2.COM
       Nextor:    NEXTOR.SYS (or MSXDOS2.SYS) and COMMAND2.COM
       MSX-DOS 1: MSXDOS.SYS and COMMAND.COM
   - BIOSDUMP.COM
   - the target machine's config.ini (in blueMSX+'s
     Machines\<machine>\)

2. Boot the MSX from that disk. When the MSX-DOS prompt (such as
   A>) appears, run BIOSDUMP by typing:

     A> BIOSDUMP

3. At "Config file (ENTER = CONFIG.INI):" press ENTER to read
   CONFIG.INI, or type another file name.
   At "Drive to save to (ENTER = current):" type the drive to
   save to (A-H); ENTER keeps the current drive.

4. The list of ROM files to dump appears.
   Y starts, N or ESC cancels.
   Each ROM is dumped in turn, with progress such as "  64/256 KB".

5. When the drive runs out of room, what fits is written in 128 KB
   parts and it stops with:

     Not enough room. Put a disk with free
     space in drive B: and press a key (ESC skips this ROM)

   Insert another disk with free space and press a key to write
   the rest. ESC stops that ROM and moves on to the next one. See
   "Split files and disk swaps" for details.

6. When all ROMs are dumped and any split files are joined on the
   PC, put them in blueMSX+'s Machines/<machine>/ under the file
   names used in config.ini.

7. In blueMSX+, pick that machine from Options -> Properties ->
   Machine. If the title logo or start-up screen matches the real
   machine, you are done.


Command-line options
--------------------

What steps 3-4 ask for can also be given after BIOSDUMP when
running it. Items given this way are not asked.

     BIOSDUMP [config file] [d:] [/Y] [/S]

     d:   drive to save to
     /Y   start without asking
     /S   write in 128 KB parts even when there is room
     /?   show usage

Example: BIOSDUMP CONFIG.INI B: /Y

Under MSX-DOS 2 / Nextor the output can be kept in a file, as in
BIOSDUMP CONFIG.INI B: /Y > LOG.TXT. /Y is required then, because
the program cannot ask questions. It cannot ask for a disk swap
either, so a ROM that runs out of room is stopped and the next ROM
follows.


Files written
-------------

For each line in the [Slots] section of config.ini that names a
file, the ROM is written under that file's 8.3 name
(e.g. A1WSBIOS.ROM).

Each [Slots] line reads <slot> <sub-slot> <start page>
<page count> <ROM type> "<file name>". A page is 8 KB, so
0 0 0 4 means 32 KB from 0000h in slot 0-0.

- A name longer than 8 characters (3 for the extension) is cut,
  and characters not allowed in file names (spaces, + etc.)
  become _. The original name to restore on the PC is shown.
- If a cut name clashes with another file, the last character of
  the later one is changed to 1, 2, ..., and "Same 8.3 name as
  another file; written as ..." is shown with the original name.
  Example: expertAC88+bios.rom, expertAC88+ext.rom and
  expertAC88+asm.rom become EXPERTAC.ROM, EXPERTA1.ROM and
  EXPERTA2.ROM.
- When several lines name the same file, only the largest one is
  dumped.


Split files and disk swaps
--------------------------

When the drive lacks room for a whole ROM (or with /S), it is
written in 128 KB parts named <name>.000, <name>.001, ... (the
suffix is hexadecimal). The count and total are shown first, as in
"Writing 32 parts of 128 KB, 4096 KB in all." Disk swaps when room
runs out work as in step 5 of "Usage".

- A stopped ROM is reported as "Stopped: this ROM is INCOMPLETE".
  The parts already written stay, but do not join them.
- If the inserted disk holds parts of the same name from an
  earlier run, those numbered from the part about to be written
  upwards are deleted, and this is shown. Lower-numbered ones are
  kept and reported as "Kept ...". Insert a different disk at
  every swap.

On the PC, join the parts by running either of these in the folder
that holds them:

  .\join.ps1 A1GTFIRM.ROM
  join.bat A1GTFIRM.ROM

Check that the joined file has the total size BIOSDUMP showed.
join.ps1 warns if a part number is missing.


Stopping with CTRL+C
--------------------

Hold CTRL+C until it stops. A short press while the disk is being
written may go unnoticed.

It then shows "Stopped by CTRL+C." and deletes the file being
written. In the middle of a split ROM it also shows "INCOMPLETE".
Files and parts already finished stay.

Under MSX-DOS 1, choosing "Abort" on a disk error may leave the
file being written behind. Reset the MSX and delete that file.


Supported ROMs
--------------

- Plain ROMs such as the main ROM, sub ROM, kanji driver,
  MSX-MUSIC and DISK ROM
- Kanji ROM (JIS level 1 and 2)
- 12-dot kanji ROM of the FS-A1FM / FS-4600 family
- Panasonic built-in software (FS-A1ST, FS-A1GT, FS-A1WX,
  FS-A1WSX, FS-A1FM) and modem ROM
- Sony built-in software (HB-F1XDJ, HB-F1XV)
- turbo R DISK ROM

Not supported (skipped with "not supported, use another tool" or
"not in memory"):

- Dictionary ROMs, FS-4600 built-in software, MoonSound wave ROM,
  MSX-AUDIO ROM, FM-PAC ROM, Sunrise IDE ROM, etc.

Dump skipped ROMs with another tool such as TINY SLOT CHECKER
(https://tiny-yarou.com/slotchecker.html). Rename files dumped
with another tool to the names in config.ini before putting them
in Machines/<machine>/.


Notes
-----

- Some ROMs contain areas filled with FFh. Areas whose reading
  changes the hardware state, such as FDC registers, are not read
  and are written as FFh; so are parts of the config.ini range
  that hold no ROM.
- The 12-dot kanji ROM may fail to dump after 12-dot characters
  have been displayed. Then reset the MSX and run BIOSDUMP first.
- If "The mapper registers could not be read back; reset the MSX
  before using its firmware." appears at the end, reset the MSX
  before using its built-in software.
- On a turbo R the CPU is switched to R800 ROM mode only while
  dumping, and put back afterwards.


Example: dumping with ESERAMair and an AI agent
-----------------------------------------------

The Wi-Fi cartridge ESERAMair (https://github.com/t-bucchi/ESERAMair)
appears as a drive to the MSX, while its files can be read and
written from the PC at http://eseram.local/. With an AI agent on
the PC (Claude Code etc.) doing the PC side, the person at the MSX
only runs BIOSDUMP and presses keys. A BIOS file too large for
ESERAMair, such as the FS-A1GT built-in software (4 MB), can be
dumped to the end by having the agent collect and delete the parts
each time room runs out.

Give the agent the text below, replacing the three < > parts to
match your setup:

  <blueMSX+ folder>  the folder blueMSX+ was extracted to
                     (e.g. C:\blueMSX+)
  <machine>          the machine's folder name under Machines
                     (e.g. MSXturboR - Panasonic FS-A1GT)
  <DOS folder>       the folder holding the MSX-DOS system files

----- from here -----
Please help me dump the BIOS ROMs of a real MSX. I operate the MSX
and you handle the PC side. The procedure is in
<blueMSX+ folder>\Tools\BIOSDUMP\README.txt, section
"Example: dumping with ESERAMair and an AI agent".
The target machine is <machine>; the MSX-DOS system files are in
<DOS folder>.
----- to here -----

The rest is for the agent.

[Operating ESERAMair]
It is driven over HTTP on port 8080. It can differ between device
versions, so read http://eseram.local/app.js first to confirm.
  GET    :8080/fat/status        free space (free_size) etc.
  GET    :8080/fat/files         root file list (JSON)
  GET    :8080/fat/files/<name>  read a file
  POST   :8080/fat/files/<name>  write the body as is (8.3, upper case)
  DELETE :8080/fat/files/<name>  delete a file
Example write (no -G on curl; it would move the body into the URL):
  curl --data-binary @BIOSDUMP.COM http://eseram.local:8080/fat/files/BIOSDUMP.COM

[Procedure]
1. Write the files from step 1 of "Usage" (config.ini named
   CONFIG.INI) and read them back to check.
2. The user boots the MSX and runs BIOSDUMP.
3. When room runs out, BIOSDUMP stops after writing a 128 KB part
   and waits for a disk swap. It is waiting when free space is
   below 131072 bytes and the file list has not changed for about
   20 seconds. Copy the finished files to the PC, read each twice
   to check they match, delete them, and ask the user to press
   a key.
4. When it finishes, collect the rest, join the parts with
   join.ps1, and check the size matches the total BIOSDUMP showed.
   Also compare with the SHA1s in openMSX's
   share/machines/<machine>.xml (openMSX sometimes combines several
   ROMs into one file, so a mismatch is not by itself a bad dump).

[Rules]
- Never delete a file before it is checked, nor the system files,
  BIOSDUMP.COM or CONFIG.INI.
- Change ESERAMair's contents only while BIOSDUMP waits for a disk
  swap or while the MSX is not in use. If changed at any other
  time, ask for the MSX to be reset.
- Never upload the dumped ROMs anywhere.
