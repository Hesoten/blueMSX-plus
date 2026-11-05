# Debugging MSX programs with an AI agent

[日本語版はこちら / Japanese version](README.ja.md)

blueMSX+ comes with an MCP server, `bluemsx-mcp.exe`. With it, an AI agent
such as Claude Code can look inside the MSX that blueMSX+ is emulating and
operate it. You describe what you want in plain words; the agent starts and
operates the machine, reads memory, sets breakpoints, presses keys and
reports back on its own.

The agent can connect only when you allow it (in the emulator's menu, or
with an option when it is started), and the emulator shows you everything it
did.

# For users

## What you can ask the agent to do

Some examples of requests, and what the agent does to answer them.

| You ask | The agent |
|---|---|
| "The game hangs when the boss appears. Where is it stuck?" | Pauses the machine, reads the registers and the stack, disassembles the code around the program counter. |
| "Find the variable that holds the number of lives." | Searches all of memory for a value, then narrows it down each time you lose a life. |
| "Something overwrites my sprite table. What is it?" | Sets a watch on that part of video memory, stops when it is written, and reports where the program was and what called it. |
| "Show me the code of DRAW_ENEMY in bank 5." | Disassembles that bank even if it is not switched in at the moment, with your labels. |
| "Stop when the routine at bank 3, 0x6000 runs." | Sets a breakpoint that only fires when bank 3 is the one switched in. |
| "Run bank 7's version of this routine once." | Switches bank 7 in where the CPU sees it, steps it, and tells you which bank the program had chosen, so it can be put back. |
| "Give me 99 lives" / "skip that check" | Writes memory or a CPU register. Writes to RAM and video memory can be undone. |
| "Type RUN and tell me what is on the screen." | Types into the machine and reads the screen back as text, or saves it as a PNG file in graphics modes. |
| "I rebuilt the ROM. Run it again." | Hard-resets the machine, so the new ROM file is read. |
| "Remember this moment so we can try the boss again." | Takes a snapshot of the machine and goes back to it as often as you like. |

On megaROMs it can read, disassemble and break on banks that are not
switched in, and with a symbol file that records banks it uses the right
labels for each bank (which mappers: see "Limits worth knowing"). The
agent's replies may call a bank a segment.

## What you need

- blueMSX+ (`blueMSX+.exe`)
- `bluemsx-mcp.exe` and this document, `bluemsx-mcp.md` (in the `Tools`
  folder of blueMSX+)
- An MCP client (Claude Code, or any other program that can use MCP servers)

## Setting it up

### 1. Register the server with your MCP client

The agent can do this for you: tell it where this document is (for example
`C:\blueMSX+\Tools\bluemsx-mcp.md`) and ask it to register the server. In
Claude Code the server's tools appear in sessions started after that. To do
it yourself:

For Claude Code:

```
claude mcp add --scope user bluemsx -- "C:\blueMSX+\Tools\bluemsx-mcp.exe"
```

If a server is already registered under the same name, registering fails:
run `claude mcp remove --scope user bluemsx` first, or pick another name.

For another client, add `bluemsx-mcp.exe`, with no arguments, as a local
command (stdio) server:

```json
{
  "mcpServers": {
    "bluemsx": { "command": "C:\\blueMSX+\\Tools\\bluemsx-mcp.exe" }
  }
}
```

The examples assume blueMSX+ is in `C:\blueMSX+`; change the path if it is
somewhere else.

### 2. Permission questions

Unless you have allowed them beforehand, Claude Code asks you before the
agent does these things:

- Use a tool of this server. To allow all of its tools at once, add
  `"mcp__bluemsx"` (the name you registered the server under) to
  `permissions.allow` in your Claude Code settings.
- Start blueMSX+. That is a shell command, asked like any other.
- Take or read a screenshot. Without a path, the screenshot is saved in
  `%LOCALAPPDATA%\blueMSXplus\agent`.

Other clients have their own way to approve tools.

### 3. How the agent reaches the emulator

#### Have the agent start a new emulator

This is the usual way while you develop: the agent starts blueMSX+, loads
your program, debugs it, and closes it when done. Ask, for example "Start
game.rom on an MSX2 and find out why it hangs at the boss", and the agent
starts blueMSX+ with a command like:

```
"C:\blueMSX+\blueMSX+.exe" --machine "MSX2 - C-BIOS" --rom1 "C:\dev\game\game.rom" --agentcontrol full
```

`--agentcontrol full` (or `read`) allows access for that run only, and does
not touch the settings you use day to day.

It can also be started with no window (`--hidden`). If an emulator keeps
running because nothing closed it, end it with `taskkill`. (For details, see
"If something goes wrong".)

#### Have the agent connect to a running emulator

Use this when you hit a problem while playing or testing and want it looked
at as it is, without restarting. Allow the agent to connect in **Tools →
Agent Control**:

| Connection setting | What the agent may do |
|---|---|
| **Off** | Nothing. The emulator does not accept connections. This is the default. |
| **Read Only** | Pausing the machine and looking at it (memory, registers, the slots, the banks, the screen), searching memory for a value, loading symbol files, and so on. After a pause, make it run again yourself (**Emulation → Run**). |
| **Full** | Everything |

A setting made in the menu is remembered for the next start. In a run
started with `--agentcontrol` it is not saved. Lowering the level takes
effect at once, and **Off** also disconnects the agent.

While the setting is Read Only or Full, a minimized emulator keeps drawing
its screen.

### 4. Use it

Start blueMSX+ and load your program, or let the agent do it, and ask the
agent. The MCP server (the agent starts it) and blueMSX+ can start in either
order: the server connects to the emulator when the agent first uses it, and
reconnects if the emulator is restarted.

If you stop the agent in the middle of a request, or close its session, the
emulator stops that request too.

Breakpoints the agent sets stop the machine for you too. They usually go
when the session ends; those set on a bank, and watches, stay until they are
cleared. The debugger window shows none of the agent's breakpoints and
watches.

If several emulators accept connections and you always want one of them,
register the server with its machine name:

```
claude mcp add --scope user bluemsx-fdd -- "C:\blueMSX+\Tools\bluemsx-mcp.exe" --instance "MSX2 - C-BIOS FDD"
```

## Seeing what the agent did

**Tools → Agent Control → Agent Activity...** opens a window that lists
what the agent does to the machine. It looks like this:

```
Listening on 127.0.0.1:53093     access: Full
Connected: mcp #1

Time          Client  Operation       Target                                          Result
09:24:44.120  mcp #1  connected
09:24:44.310  mcp #1  load symbols    game.sld  (C:\dev\game)
09:24:45.002  mcp #1  pause
09:24:45.100  mcp #1  read x8         slot3.2:seg0+0x0000-0x001F (Z80 0xC000-0xC01F)
09:24:45.420  mcp #1  write           slot3.2:seg0+0x0010 (Z80 0xC010)                was FF now 63
09:24:45.600  mcp #1  undo write      slot3.2:seg0+0x0010 (Z80 0xC010)                was 63 now FF
09:24:45.700  mcp #1  write           slot1.0:seg0+0x0010 (Z80 0x4010)                not kept (still 31)
09:24:45.810  mcp #1  switch bank     slot1.0 (Z80 0x6000-0x7FFF)                     seg0 -> seg3
09:24:45.900  mcp #1  set breakpoint  slot1.0:seg3+0x0000 (Z80 0x6000)
09:24:46.020  mcp #1  tap key         space
```

The first line says whether the emulator accepts connections, and at which
access level. It accepts them only from programs on the same PC. The second
line names each connected tool and its number (`mcp #1` is this server's
first connection). **(cut off)** after a name means you set Agent Control to
Off: that tool can do nothing more, and its connection ends shortly. When
the window has dropped old rows, a third line says how many.

Below them, each action is one row, oldest first:

| Column | Meaning |
|---|---|
| Time | When it happened, to the millisecond. |
| Client | Which connection did it. |
| Operation | The action. `x8` means 8 reads in a row of the same memory, or the same failure 8 times in a row, shown as one row. |
| Target | What it was done to. `slot3.2:seg0+0x0010 (Z80 0xC010)` is segment 0 of the RAM in slot 3.2, seen by the Z80 at 0xC010 now. |
| Result | What came of it: the bytes before and after a write (after it, read back from memory), a register's old and new value, how many candidates a search has left, or why it was refused. **failed (emulator stopped)** means it was asked for but did not happen, and why. |

Refused and failed actions, and writes the memory did not keep, are drawn in
amber.

Reading registers, the slots or the VDP leaves no row, so the window is not a
complete list of what the agent looked at.

Ctrl and + / − change the text size, and Ctrl+0 resets it.

## Files it creates

These are the files Agent Control creates. All of them go in
`%LOCALAPPDATA%\blueMSXplus\agent`, never in the blueMSX+ folder.

| File | What it holds | Created and deleted |
|---|---|---|
| `instance-<pid>.json` | The port and token the agent connects with, the access level and the machine name. `<pid>` is the process ID of blueMSX+. | Created while Agent Control is Read Only or Full; deleted when it is set to Off and when blueMSX+ exits. A file left behind by a killed emulator is deleted later by blueMSX+ or the MCP server. |
| `screen-<pid>.png` | A screenshot taken without a path. | Overwritten by each such screenshot. Never deleted automatically. |

A screenshot the agent saves to a path of its own goes to that path (Full
only). Snapshots and the activity list are kept in memory only, never in a
file. The MCP server, `bluemsx-mcp.exe`, creates no files.

## Giving the agent your symbol files

With a symbol file the agent shows your labels in disassembly and stack
traces, and you can name places by label ("break at DRAW_ENEMY"). Tell the
agent where the file is, for example "load C:\dev\game\game.sld".

| Tool | File to give | Bank numbers |
|---|---|---|
| sjasmplus | `--sld` output (`.sld`) | Yes, from the device pages |
| sjasmplus | `--sym` output | No |
| asMSX | `.sym` | Yes |
| WLA-DX | `wlalink -S` symbol file | Yes |
| SDCC, and libraries built on it (MSXgl, Fusion-C, libmsx, ...) | `.noi` or the linker `.map` | Yes, for code linked above 64 kB (for example `-Wl-b_SEG5=0x58000` is bank 5) |
| pasmo, Glass, tniASM | `NAME EQU value` symbol file | No |
| z88dk | `.map` | No |

Give the full path, starting with the drive letter. Several files can be
loaded at once; a file with the same name as one already loaded, ignoring
the folder and the extension, replaces it.

"Bank numbers" matters for megaROMs. When the file has them, each label
belongs to its own bank, so the same address in different banks gets the
right label. When it does not, a label applies to its address in whatever bank
is switched in.

A file with bank numbers has to be attached to the cartridge it belongs to.
This happens automatically when the file has the same name as the ROM file
(`game.rom` and `game.sld`). Otherwise the agent is told which slots could
hold it and asks you which one; until then the file gives no labels. A program
that runs from RAM (from a disk, for example) always needs this answer: the
slot of the memory mapper. In a disk program the labels can be off when DOS
gives out other segments.

## Limits worth knowing (mappers, flash, what it cannot do)

- Banks work on these mappers: reading and disassembling a bank that is not
  switched in, breakpoints on a bank, attaching a symbol file with bank
  numbers, and bank names in the stack.
  - Memory mapper
  - ASCII 8, ASCII 16, ASCII 8 (SRAM), ASCII 16 (SRAM)
  - Konami, Konami SCC, Konami Generic
  - SCC, SCC-I, The Snatcher, SD Snatcher, SCC mirrored, SCC extended
  - ASCII16-X, Yamanooto, NEO-8, NEO-16
  - Mega Flash Rom SCC, Mega Flash Rom SCC+
  - Manbow 2, Manbow 2 v2
  - Super Pierrot, Hamaraja Night, Standard
- Of these, on ASCII 8 and ASCII 16 (also with SRAM), Konami, Konami SCC,
  Konami Generic, ASCII16-X, Yamanooto, NEO-8, NEO-16 and Super Pierrot the
  agent can also switch banks.
- With other cartridges the agent sees only what the CPU sees at the moment.
- To learn which mapper the emulator picked for your ROM, ask the agent: it
  sees the mapper name of each slot. If it is the wrong one, start the
  emulator again with `--romtype1` (`blueMSX+.exe --listromtypes` lists the
  names).
- Searching memory for a value covers RAM and video memory. The machine has
  to run between the steps of a search; with Read Only you start it again
  yourself.
- A flash cartridge (ASCII16-X, Yamanooto, Flash-ROM SCC and the Mega Flash
  Rom SCC family) keeps what the MSX flashed in a file named after the ROM
  (`game.flash` or `game.sram`) in the `SRAM` folder of blueMSX+, and the next
  start uses it, so a rebuilt ROM may not be what runs. The agent is told
  when it resets the machine, but not when the emulator is started fresh:
  delete that file (or have the agent delete it) before starting a rebuilt
  ROM.
- A snapshot puts back only the machine: not the disks, the tape, or the
  SRAM and flash of most cartridges.
- Typing needs the machine to be running and ready to read the keyboard. It
  works on machines with the Japanese or the international keyboard layout;
  on others (German, French, Russian and so on) the agent can still press
  keys by name. Long text is typed in several requests.

What the agent cannot do, and what it does instead:

| Not possible | Instead |
|---|---|
| Insert or eject a cartridge, disk or tape while the emulator runs | For a file rebuilt under the same name, a hard reset reads it again. For another file or mapper type, start the emulator again (`--rom1`, `--romtype1`, `--diskA`). |
| Change the machine while the emulator runs | Start the emulator again with `--machine`. |
| Save a state to a file, or load one | A snapshot keeps one state in memory. To start from a state file, use `--state` when starting the emulator. |
| Change the emulation speed | `--speed` when starting the emulator. |
| Record and replay a session | Not possible. |
| Run for a number of frames | It waits a given time for a stop, or steps a number of instructions. |
| Breakpoints with a condition or a hit count | A watch can stop only when a value is written that compares as asked. |
| Stop when memory, video memory or a port is read | Watches stop on writes and on OUT only. |
| Watch a bank that is not switched in | A breakpoint on that bank. |
| Write to an I/O port | Not possible. |
| Decode what was written to the VDP, the palette or the sprites | It reads the VDP registers and a screenshot. |
| List a BASIC program, break on a BASIC line, read BASIC variables | It types `LIST` and reads the screen, and reads memory (on a machine with BASIC). |
| Use an assembler listing (`.lst`) for labels | Give it the symbol file (`.sld`, `.sym`, `.noi`, `.map`). |
| Trace, profile, measure code coverage, assemble into memory, act as a DAP debugger, or show graphical viewers | Not possible. |

## If something goes wrong

| What happens | What to do |
|---|---|
| The agent says no emulator accepts connections. | Start blueMSX+ and choose **Tools → Agent Control → Read Only** or **Full**, or let the agent start it. |
| The agent says another session is connected. | Close the other session (perhaps an earlier one still open), or start another emulator. |
| The agent says two emulators accept connections. | Close the one you do not mean, or set Agent Control to Off in it. |
| The agent says the emulator window does not answer (a dialog or the like is open). | Close the menu, dialog or folder disk question open in the emulator window, then let the agent try again. |
| With Read Only, the game stays paused after the agent looked. | The agent cannot resume in Read Only. Choose **Emulation → Run**. |
| You want the agent to stop at once. | Set Agent Control to Off. The agent is disconnected at once. Breakpoints set on a bank and watches stay. |
| Read Only or Full cannot be turned on again after Off. | A request from before is still ending. Wait a moment and choose again. |
| The machine still stops after the session has ended. | Ask the agent in a new session to clear its breakpoints and watches, or restart blueMSX+. |
| An emulator the agent started with no window is still running. | Run `taskkill /PID <pid>` (without `/F`: Task Manager or `/F` loses the SRAM save); the pid is in the name of `%LOCALAPPDATA%\blueMSXplus\agent\instance-<pid>.json`. |
| A rebuilt flash cartridge ROM does not seem to run. | Delete `<rom>.flash` or `<rom>.sram` in the `SRAM` folder of blueMSX+ while no machine runs, then start it again. |
| The emulator shows an error the agent does not mention. | The agent cannot see the emulator's own messages. Tell it what the window says. |

---

# For the agent

The sections below are reference for the AI agent. It can read any of them
with `bmsx_reference <topic>`; you do not need to know them to use the server.

Each tool's arguments, their allowed values and defaults reach the agent
with the tool list when it connects. These sections cover what does not fit
there.

At the start of a session, `bmsx_break op=list` and `bmsx_watch op=list`
show the breakpoints and watches earlier clients left; segment breakpoints
and watches outlive the end of a session, Agent Control Off and a new
session, with no note.

## Paused and running: what works when

The machine is inspected only while it is paused. Reading memory, registers,
the stack, disassembly, the bank list, a hunt and the text screen needs a
paused machine, and so do memory and register writes, undo, bank switches and
snapshots. On a running machine those calls are refused with `not_paused`.
Nothing pauses the machine behind your back. Mistakes in the arguments are
reported before `not_paused`.

To get a paused machine:

- `bmsx_run how=pause`. This also works in Read Only, where the user
  resumes the machine: Read Only cannot resume, step or `run_until`.
- `bmsx_run_until` until a breakpoint or a watch stops it.
- `bmsx_run_until timeout_ms=N pause_at_timeout=true`: a wait that times out
  pauses the machine. To look at a running program now and then, read, then
  call it again.

While the machine runs these still work: `bmsx_status` (the state, the
segments paged in, the machine's name and the frames run; no registers, CPU
or VDP), `bmsx_slots` (without the `*` marks of switched banks),
`bmsx_mem_resolve` of a `vram:` address or of a segment past the end,
breakpoints and watches (set, clear, list), `bmsx_hunt op=end`, `bmsx_key`
and `bmsx_type`, `bmsx_screen as=png` (`as=auto` gives the PNG),
`bmsx_symbols`, `bmsx_reset`, `bmsx_run how=pause` and `how=resume`, and
`bmsx_run_until`. Stepping is refused with `not_paused`.

What the paging of a running machine would decide is not answered while it
runs, as it can be another answer a moment later. `bmsx_mem_resolve` of a
Z80 address, a segment or `ram:` answers `not_paused`, and so does
`bmsx_watch op=set` on a segment that is not paged in. A register in `at`
(`pc`, `hl+2`) on a running machine is refused with the same `not_paused`,
after the other arguments are checked. Where an answer is still given from
the running paging, the reply says so. A video memory watch set while the
machine runs is not checked against the screen mode, and its stop does not
say which layout it was set in.

## Addresses

Every tool that takes an address accepts these forms:

| Form | Meaning |
|---|---|
| `0xC000`, `$C000`, `#C000`, `C000h`, `49152`, `cpu:0xC000` | The Z80 address as the CPU sees it right now. A bare number is decimal. |
| `pc`, `hl+2`, `sp` | Where a register points. The machine must be paused. |
| `@DRAW_ENEMY`, `@DRAW_ENEMY+3` | A loaded symbol, in the segment it belongs to. |
| `slot1.0:seg5+0x100` | Offset 0x100 in segment 5 of slot 1, subslot 0, whether or not it is paged in. |
| `slot0.0+0x0100` | Z80 address 0x0100 in a slot that reports no segments; readable only while that slot is paged into that Z80 page. |
| `ram:seg12+0x1A30` | Offset 0x1A30 in segment 12 of the memory mapper in the slot page 3 (0xC000-0xFFFF) shows. If page 3 shows another slot, the machine's built-in mapper; machines without a memory mapper (most MSX1) cannot use it. The reply says which slot it took. |
| `vram:0x1B00` | Video memory, at the address the program uses through the VDP (R#14 and ports 0x98/0x99, or a VDP command). In SCREEN 7, 8 and 10-12 the VDP interleaves its two 64 kB banks, so vram:0x8001 is not the 0x8001st byte of the chips; the tools still mean the program's address. |

A segment is what the user part calls a bank. Every address the tools print
is in one of these forms and can be passed back unchanged. A Z80 address is
reported through the Z80 page it was given in, even when the same segment is
also paged into another.

In an address a bare number is decimal: `4010` is 0x0FAA. Write `0x4010`,
`$4010`, `#4010` or `4010h` for hexadecimal. Symbol files are the other way
round: bare digits there are hexadecimal. When an address has a bare number of
4 or more digits, the reply's first line says how it was read and, when the
same digits read as hexadecimal are at most 0xFFFF (0x2FFFF for `vram:`), how
to write them in hexadecimal. A number argument (a count, a value, a port),
bare or in quotes, is decimal unless it starts with
`0x` (or `-0x`); a leading zero does not make it octal, and one too large to
be an address is refused. A name in a symbol file longer than 191 bytes is
cut when it loads.

An offset larger than its segment carries on into the following segments, and
the reply prints where it landed. A segment past the slot's last one is never
wrapped onto another but refused (`bmsx_mem_resolve` answers it as
information). `seg<n>` on a slot that reports no segments is a `bad_address`
error that says so; use a Z80 address while that slot is paged in.

`ram:` follows page 3, and each reply that uses it says which slot it took
and why. A memory mapper in a cartridge counts when the BIOS put page 3 on
it. With neither, `ram:` is refused and the refusal names the memory mappers
there are, by slot; on a machine with none, use `0xC000` or
`slotX.Y+0xC000`. A `bmsx_reg_write` value given as `ram:...` gets the same
line.

`bmsx_mem_read`, `bmsx_mem_write` and `bmsx_undo` take the same `vram:`
address: an undo puts the bytes back where the write went, even after a
screen mode change, and says so. On a 64 kB VDP in SCREEN 7 or 8 an odd
`vram:` address has no video memory behind it and is refused
(`out_of_range`). Expansion video memory (vram:0x20000-0x2FFFF, the V9938 MXC
expansion) is numbered as the chips are, in every screen mode.

`vram:` is always the VDP's video memory. A V9990 (GFX9000, Video9000) has
its own, which the tools cannot read, write, watch or search.

When the bytes the CPU reads at an address differ from what the segment
stores (SCC registers, flash status and the like), a read says so and gives
the segment address of the stored bytes.

## Tools and errors

| Tool | More |
|---|---|
| `bmsx_status` | The segment in each Z80 page shows both 8 kB halves on an 8 kB mapper, as `seg3,seg5`. While paused also whether the CPU is the Z80 or the R800 (turbo R), and the screen mode, named as BASIC does (`SCREEN 1`, `SCREEN 0 WIDTH 80`), or that the mode bits are mixed or undefined. With a V9990 (GFX9000, Video9000), while paused, it also says whether the V9990's picture is shown, in which mode, and whether it is superimposed on the VDP's; the vdp lines still describe the VDP (V9938, V9958 or TMS99x8), not the V9990. While running, the machine's name and the frames run instead. |
| `bmsx_mem_resolve` | In SCREEN 7, 8 and 10-12, while paused, it also gives the chips' byte number for a `vram:` address, which is where the debugger window's VRAM view shows the byte. |
| `bmsx_stack` | A frame is marked when no CALL, RST or HALT ends just before it in the segment paged in now, since its label may then be wrong. |
| `bmsx_dasm` | From a Z80 address it reads on as the Z80 sees memory and says where another slot or segment starts, up to 0xFFFF; from a segment address it stops at the end of that segment. |

Read Only allows the reading tools (`bmsx_status`, `bmsx_slots`, the bank
list, `bmsx_mem_read`, `bmsx_mem_resolve`, `bmsx_stack`, `bmsx_dasm`,
`bmsx_symbols`, `bmsx_hunt`, `bmsx_screen` without a path, and the
breakpoint and watch lists) and `bmsx_run how=pause`; everything else needs
Full.

Errors name the argument at fault. An argument the tool does not take, a
required one left out, a value of the wrong type, or a word not in the
argument's list of values is refused by the server, with no error code;
nothing is sent. Ranges of numbers are checked by the emulator
(`out_of_range`). Nothing was done for `arg_too_long`, or for
`line_too_long` (the whole call is over 64 kB).

The other errors about the machine and the emulator:

- `not_paused`: the call needs a paused machine. Pause it, or run to a stop.
- `no_machine`: the emulator is stopped or being reset. `bmsx_run how=resume`
  never starts a machine; `bmsx_reset how=hard` does.
- `busy`: nothing was done. The emulator is busy with something else, or has
  a menu or dialog open. Call again once it is closed.
- `dialog_open`: only `bmsx_reset` answers this, while a dialog or menu is
  open or the window is being moved or resized.
- `read_only`: Agent Control is Read Only. `revoked`: it was set to Off.

With no machine these still work: `bmsx_status`, the breakpoint and watch
lists, symbol lookups, `clear_all`, and clearing a watch by the address
`op=list` printed. A single breakpoint cannot be cleared then: the
breakpoints set here stay and are armed again when the machine starts, so
clear them then, or use `clear_all`.

A request the client cancels, or whose connection closes, ends there and gets
no reply.

## Writing memory and undo

`bmsx_mem_write` writes at most 511 bytes per call, however the bytes are
written (`"3E 01 C9"`, `"0x3E, 0x01"`, `"3E01C9"`). More is refused with
`arg_too_long` naming `data`, and nothing is written; data longer than one
request line is refused with `line_too_long`. A write to RAM or video memory
can be taken back with `bmsx_undo`, for the last 32 writes; a reset or a
snapshot restore drops that journal.

A write that goes to ROM, flash or a mapper register reaches the device in
that slot, which decides what it does (often a bank switch, whose old value
cannot be read); read back to see, and it cannot be undone. A write to a bank
register of a switchable mapper counts as a switch of yours (see
`bmsx_bank`). A write that crosses from one segment into the next cannot be
undone either, nor can a register write (`bmsx_reg_write` says the old value,
so it can be put back by hand). Undo puts the bytes back even if the
program changed them since.

## Switching a bank (bmsx_bank)

A segment switched by you is marked with `*` in `bmsx_slots` and in the run
card. The reply says how to switch back before resuming. Segment numbers
count from the start of the ROM or flash, whatever offset a Yamanooto program
has set. `segment="sram"` pages in the SRAM of the SRAM cartridges. A segment
no bank value of the program can select (an ASCII16 nf ROM whose size is not
a power of two) is refused with `out_of_range`. While the SRAM is switched
into a window the program writes, its writes there go to the SRAM file.

Switches are not undone by `bmsx_undo`. A snapshot taken while a bank is
switched keeps that segment as the program's.

## Launching the emulator (--hidden, several instances)

When no emulator accepts connections, every tool answers with a command to
start one.

- Write the options in the `--` form; Git Bash turns an argument such as
  `/rom1` into a path. In PowerShell the quoted program path needs `&` in
  front: that is PowerShell's call operator, not background, so use
  `Start-Process` or your tool's own background option to start it in the
  background.
- After starting it, call `bmsx_status` until it answers (a few seconds). Do
  not start another one meanwhile.
- Give full paths. A relative path is taken from the emulator's folder, not
  from the current directory (`--rootdir`, `--machinedir` and `--inifile`
  excepted).
- Only the C-BIOS machines start out of the box (`MSX - C-BIOS`,
  `MSX2 - C-BIOS` and others); the release has no `MSX2.rom` or other system
  ROMs. C-BIOS has no BASIC. For a disk program use `MSX2 - C-BIOS FDD`, which
  has Nextor. BASIC or another machine needs the user's own ROM files in
  `Machines\Shared Roms`.
- `--listmachines` lists the machines, `--listromtypes` the mapper names for
  `--romtype1` (short forms such as `KONAMI GEN`, `SUPERPIERR`,
  `MEGAFLSHSCC+`, not the names `bmsx_slots` prints), and `--help` the other
  options: `--diskA`, `--state` to start from a state file, `--speed`,
  `--paused` to pause as soon as the machine starts, and
  `--exitafter <frames>` with `--screenshot <file>`. `--exitafter` closes the
  emulator the way the window's X does, with exit code 0; a windowed run whose
  folder disks changed asks first.

Close an emulator you started with `taskkill /PID <pid>` without `/F`, which
closes it the way the window's X does: SRAM is saved and the instance file is
removed. A windowed one asks first when its folder disks changed. The pid
comes from `Start-Process -PassThru`, or from the name of
`%LOCALAPPDATA%\blueMSXplus\agent\instance-<pid>.json`.

`--hidden` runs it with no window, sound or dialogs; the tools work the
same, screenshots included. Start it with `--agentcontrol full --hidden`:
there is no menu, so Agent Control cannot be turned on later. It keeps
running when the MCP client exits. On `taskkill` its folder disks are written
back only when their writeback option (`--diskAwriteback` and the like) was
given. If a hidden emulator cannot start a machine at its first start it
exits with code 2; after that (a hard reset, for example) it reports the
failure on stderr and keeps running with no machine.

When several emulators accept connections, the reply lists them with their
pid and the machine each last started, and says what to do. `--instance` in
the MCP client's settings pins one for good. Case does not matter. A full
machine name wins, otherwise part of a name works when only one emulator
matches it, and a name matching several is refused with the list. A value of
only digits is a pid (or a machine name of exactly those digits). A pid stops
matching when the emulator restarts, but it is the only way to tell apart two
emulators running the same machine, and a row with no machine started
matches by pid only. An emulator with Agent Control Off is not listed at
all.

## Key names

`bmsx_key` takes these names; join several with `+`, as in `ctrl+stop`.

- Function and editing: `f1`–`f5` `stop` `cls` `select` `ins` `del` `esc`
  `backspace` `tab` `return` `space` `pause` `print`
- Modifiers: `ctrl` `leftshift` `rightshift` `caps` `graph` `code`
  `torikeshi` `jikkou`
- Letters and digits: `a`–`z` `0`–`9`
- Symbols: `neg` `circomflex` `backslash` `at` `leftbracket` `rightbracket`
  `semicolon` `colon` `comma` `period` `div` `underscore`
- Cursor: `left` `up` `down` `right`
- Keypad: `num0`–`num9` `numdiv` `nummul` `numsub` `numadd` `numperiod`
  `numcomma`
- Joysticks: `joy1-up` `joy1-down` `joy1-left` `joy1-right`
  `joy1-button1`–`joy1-button6`, and the same with `joy2-`

An unknown name is refused with the full list.

## Symbol binding (symbols per segment)

Symbols are keyed by slot, subslot, segment and address. A file with bank (or
SLD page) numbers is bound to one slot: by the `slot` argument, or by its file
name matching the ROM's, if that ROM has enough segments. An SLD file whose
Z80 pages each hold one of its pages is read as a flat program, and the load
says so. Otherwise it stays unbound and gives no labels and no `@name`; the
reply lists the slots that could hold it, and the file is loaded again with
one of them, as in `slot="1.0"`. A slot whose segments are larger than the
file's pages, so that a page would never show at the address it was assembled
for, is neither bound nor listed. A slot that reports no segments is never
bound: a file named after its ROM stays unbound, and `slot=` naming it is
refused. A file is never bound by size alone. After the cartridge is swapped,
the status says so. Symbols at 0xC000 and above are never bound to a ROM,
since page 3 is RAM.

A banked file bound to the memory mapper reads its banks as the mapper's own
segments; 8 kB SLD pages on its 16 kB segments are read as page n = seg n/2.
A program that gets its segments from MSX-DOS 2 or Nextor at run time is
usually given other numbers, and then the labels name the wrong segment; the
load says so.

A file with the same name stem as a loaded one (ignoring folder and
extension) replaces it. Labels from a file loaded without a slot are used
only where the slot paged in has no bound file of its own, and always at
0xC000 and above. Identical definitions in several files count as one, and
with several files loaded a lookup says which file each name came from. In a
disassembly a call into a Z80 page with banks is named only when one segment
defines that address; a relative jump in a segment is named by the label at
that offset, or, with no label there, shown as a segment address when the
segment is not paged in.

## Hunt: finding a variable

```
bmsx_hunt       op=start   width=u8
bmsx_run_until  timeout_ms=3000 pause_at_timeout=true
   ... or resume and pause after the value changed ...
bmsx_hunt       op=refine  compare=decreased
```

`start`, `refine` and `list` need a paused machine, and the machine has to
run between refines; in Read Only the user resumes it. The search covers
every RAM a slot reports (the memory mapper, plain RAM, the RAM of an SCC-I
cartridge) and video RAM; not battery backed SRAM, MegaRAM or flash. Wider
widths test every byte offset, so a 2-byte value at X also shows at X-1 until
an exact value is given. A value to compare with (`eq`, `lt`, `in_range` and
the like) must fit the width (0 to 255 for `u8`, -128 to 127 for `s8`, and so
on). A reset or a snapshot restore ends the hunt.

Video memory is searched in the layout the VDP had when the hunt started; if
the screen mode changes the layout during the hunt, the list says so. On a
64 kB VDP in SCREEN 7 or 8 video memory is left out of the hunt.

## Watches (watchpoints): catching what changed a byte

```
bmsx_watch      at="@lives"
bmsx_run_until  timeout_ms=10000
bmsx_status
```

A port is given as `port=152` or `port="0x98"`, not together with `at`. The
stop is reported as a watchpoint with the address written, and PC is already
past the writing instruction. The address must be visible to the CPU; for a
segment that is not paged in, use `bmsx_break` on a segment address. A watch
on memory watches the Z80 address: given as a segment address that is paged
in, it is set on the Z80 address where that segment shows now, and fires on
any write there, whatever segment is paged in later. One watch covers one
address: setting another there replaces it, and `value` is used only with a
condition that compares. At most 64 watches can be set through the socket.
Watches stay until cleared.

A video memory watch watches the address as the VDP lays memory out at each
write. If the program changes screen mode between the SCREEN 0-6 layout and
the SCREEN 7/8/10-12 one, the same `vram:` address is another byte; a stop
then says the watch was set in the other layout. In SCREEN 7, 8 and 10-12 a
stop also gives the chips' byte number, as the debugger window shows it.
Plugin and debugger-window watches keep using the chips' own byte numbers. A
VDP command that started before a screen mode change goes on with the layout
it started in, and its writes are reported in the layout the VDP has now. On
a VDP with 64 kB, a watch on an odd address in SCREEN 7 or 8 is refused:
there is no video memory behind it. There, a VDP command can also write at
vram:0x10000 and above, which the tools refuse as past the end: such writes
can be neither read nor watched.

## Breakpoints in other banks, code not paged in, and when they are dropped

```
bmsx_break      at=slot1.0:seg5+0x0100
bmsx_run_until  timeout_ms=10000
```

A segment breakpoint fires in whichever Z80 page the segment is paged into;
an 8 kB mapper can show one segment in two Z80 pages, and an address given
through either is the same breakpoint. At most 64 segment breakpoints can be
set. A breakpoint on a HALT stops once each time the CPU reaches it.

If plain breakpoints were dropped because a server was killed, was
disconnected for more than 5 s, or Agent Control was turned Off, the next
call that reconnects says how many: to that same server if it comes back, and
once to the first other client that connects. A normal exit drops them
without a note.

`clear_all` removes every breakpoint set through MCP. Breakpoints set in the
debugger window stay: MCP cannot clear them. Clearing one of them, or
anything else not set, answers `not_set`. `op=list` shows them too, but the
window does not show the ones set through MCP. A segment breakpoint kept from
an earlier start of the machine, on a slot that now reports no segments, can
only be cleared with `clear_all`.

## Running and stepping (run_until, step)

`run_until` waits for a new stop; a stop that happened before the call and was
not yet shown (one met while a `resume` was being answered, for example), or
in a new session the stop the machine is paused at, is reported once, saying
"this stop happened before the call". The stop reasons are `breakpoint`,
`watchpoint`, `step`, `user` (a pause), `step_back` (the emulator's step back
reached its moment), `vram_timing` (the debugger's VRAM access check, MSX1
VDP only), `frame` (the emulator's frame step reached the end of a frame of
the picture on screen) and `machine` (stopped, reset or replaced).

Each step waits for the emulator's next frame when it syncs to the display,
so a long step count is slower than a breakpoint with `run_until`. After a
watch stop the first step may only finish the interrupted instruction, and
says so. `step_out` refuses when the stack holds no return address the CPU
recorded, for example at the top of a main loop; `step_over` and `step_out`
report their finish as a step. Read Only applies to new requests, and a step
loop in progress ends at once.

## Resetting, snapshots and flash

Both kinds of `bmsx_reset` keep the breakpoints and the symbols, and end the
hunt and the undo journal. If the machine was switched off and did not start
again (`not_started`), no machine runs until a hard reset succeeds.

The reply of a hard reset names what did not start afresh:

- A disk folder the MSX has written to, and any hard disk folder, are kept as
  they were. Ejecting and inserting it again in the emulator reads it afresh.
- Flash cartridges keep what they flashed, in the `SRAM` folder of blueMSX+.
  Yamanooto, ASCII16-X and Flash-ROM SCC lay `<rom>.flash` over the ROM (or a
  whole `<rom>.sram` of the flash size); the Mega Flash Rom SCC family (SCC,
  SCC+, Manbow 2, Hamaraja Night) starts from the whole `<rom>.sram`. The
  reply names the file. To start from the ROM alone, delete it while no
  machine runs (**Emulation → Stop** in the emulator, or close it), then
  start the machine again. Only a hard reset says this: an emulator started
  fresh with a rebuilt ROM uses the file without a word, so check that folder
  first. These cartridges also report the flash size (8192 kB for ASCII16-X),
  not the ROM file's.
- A symbol file that changed on disk since it was loaded, or a ROM written
  after the symbols were loaded: load the symbols again.
- Segment breakpoints stay at the same segment and offset; check them against
  the new symbols.

`bmsx_snapshot` needs a paused machine, and the machine stays paused after a
restore. A soft reset does not drop the snapshot. There is no snapshot to a
file; for a state file use `--state` at launch.

- If DOS wrote to a disk image after the take, its FAT in RAM goes back but
  the image does not, so reset before it writes to that disk again.
- Breakpoints and watches stay as they are. The emulator's own step back can
  go back only to moments after the restore. After a restore the window and
  `as=png` show the old picture until a frame runs; `as=text` is current. As
  with the emulator's own rewind, some devices' timers do not run again at
  once, among them turbo R PCM sound, serial, MIDI and network input, and the
  ColecoVision spinner.
- A breakpoint at the restored PC: restored from a pause, the next
  `run_until` stops there at once; restored while stopped at a breakpoint (or
  at the end of a step over or step out), the next `run_until` runs past it;
  restored into a HALT, it fires when the CPU arrives there again.
- Take and restore are refused with `busy` while a dialog or menu is open.

## Typing (bmsx_type), keys and the screen (PNG, screenshots)

`bmsx_key` accepts taps shorter than the 4 frames `bmsx_type` needs, for games
that read the keyboard themselves.

Text longer than one call can type is refused before anything is typed. If
the machine stops before every key went in, the reply is
`input_interrupted`: after a pause or breakpoint the rest go in when it runs
again, so do not send them again. A reply means the keys reached the
keyboard, not that the program read them.

`bmsx_screen` returns text in text modes (SCREEN 0, SCREEN 0 WIDTH 80 and
SCREEN 1, including the 27 rows of 212-line mode); in graphics modes, or
when a path is given, it writes a PNG and returns its path. While the
machine runs `as=text` is `not_paused`. A path must be a full Windows path
(`C:\...` or `\\server\...`); `/c/...` is refused. The text is the character
codes the program wrote, not the shapes drawn, so a program's own font still
reads as its codes; codes outside 0x20-0x7E show as `.` and are listed by
number. The reply adds a note when a letter's pattern is cleared, the text
and background colours are drawn the same (in SCREEN 1, for each letter by
its colour table entry), the display is off, the screen is scrolled, or the
mode is mixed; check the PNG then.

While a V9990 picture (GFX9000, Video9000) is shown, `as=auto` gives the
PNG, since the V9990's screen has no text to read. `as=text` still returns
the VDP's text, with a first line saying that a V9990 picture is shown over
it or instead of it. `bmsx_status` says which picture is shown.

With Agent Control on, a minimized emulator keeps drawing, and `--hidden`
draws too, so a PNG is current. For a window minimized while Agent Control
was Off, a PNG is refused with `not_drawing`, and the reply says what to do.

## Wire protocol

The server only translates; the emulator does the work. Other programs can use
the same socket: line-delimited JSON on 127.0.0.1, with the port and a token in

```
%LOCALAPPDATA%\blueMSXplus\agent\instance-<pid>.json
```

Send `{"op":"hello","token":"...","owner":N}` first, within 2 s of
connecting or the emulator closes the connection, then `{"op":"help"}` for
the list of operations. `owner` is a positive number the client keeps for its
lifetime. A hello without one, or with another client's, drops the plain
breakpoints the previous client kept, so a script that connects between an
agent's calls silently removes the agent's plain breakpoints. One client is
served at a time; a second connection waits, with no reply, until the first
closes. A client that will not come back sends `{"op":"session.end"}` before
closing, which ends its plain breakpoints at once.

A request line is at most 65535 bytes; a value of the wrong JSON type answers
`bad_arg`. `null` is the same as leaving the argument out, and keys an op does
not read are ignored.

Reply texts name the MCP tools, as in "pause it first (bmsx_run how=pause)".
On the socket each tool is an op: `bmsx_run` is `run`, `bmsx_break op=set` is
`bp.set`, `bmsx_bank` is `bank.list` or `bank.set`, and so on; `help` lists
them.

# Acknowledgements

Searching all of the memory mapper's physical RAM for a changing value, and
the Agent Activity panel, build on ideas Sebbeug proposed in pull request #87.
