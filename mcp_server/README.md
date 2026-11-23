# blueMSX+ MCP debugger bridge (experimental)

This opt-in plugin lets a local MCP client inspect a paused emulator, read and
write debugger memory blocks, inspect registers, and compare RAM captures to
find values such as lives or scores. It does not send ROMs or memory anywhere
by itself. The AI client receives any data returned by tools it calls.

## Build and install

1. Build `blueMSX/Make/msvc2022/Plugins/McpBridge/McpBridge.vcxproj` in
   `Release|x64` (or the matching 2026/Win32 project and platform).
2. Copy `McpBridge.dll` from the build output to the `Tools` directory next to
   `blueMSX+.exe`. Keep the DLL's architecture the same as the emulator's.
3. Restart blueMSX+. The Tools menu should contain **MCP Bridge**. The pipe
   exists only while this plugin is loaded. Remove the DLL to disable it.
4. Install Python 3.10+ and create a local environment from the repository root:
   `python -m venv mcp_server/.venv`, then
   `mcp_server/.venv/Scripts/python.exe -m pip install -r mcp_server/requirements.txt`.
5. Start blueMSX+. The server discovers its pipe automatically when one
   instance is running. For multiple instances, find the desired PID with
   `Get-Process | Where-Object ProcessName -Like '*blueMSX*' | Select-Object Id,ProcessName`.

The pipe name is `\\.\pipe\blueMSX-plus-mcp-<PID>`. It accepts local clients
running under the same Windows user. Set `BLUEMSX_PID` only to select between
multiple emulator instances.

Open **Tools → MCP Bridge** to see whether the pipe is listening, how many
local MCP server processes are connected, the emulator state and a log of MCP
activity. The log records device and memory reads, register reads, writes,
emulator controls and RAM search summaries. Individual reads made during a RAM
scan are grouped into its start and completion entries. The compact panel uses
the debugger's fixed-width font and host theme. It can remain open while
playing. Closing it hides the panel; the bridge continues running. A client
count of zero means no MCP server process is currently attached.

## Connect a client

For **Claude Desktop**, add an entry to
`%APPDATA%\Claude\claude_desktop_config.json`, using absolute paths:

```json
{
  "mcpServers": {
    "bluemsx": {
      "command": "D:\\Path\\To\\blueMSX-plus\\mcp_server\\.venv\\Scripts\\python.exe",
      "args": ["D:\\Path\\To\\blueMSX-plus\\mcp_server\\server.py"]
    }
  }
}
```

For the **ChatGPT desktop app / Codex local host**, add a STDIO MCP server
named `bluemsx`, with the same Python executable and script path. A
`~/.codex/config.toml` equivalent is:

```toml
[mcp_servers.bluemsx]
command = "D:\\Path\\To\\blueMSX-plus\\mcp_server\\.venv\\Scripts\\python.exe"
args = ["D:\\Path\\To\\blueMSX-plus\\mcp_server\\server.py"]
```

ChatGPT **web** uses hosted remote MCP tools; this local STDIO server is for
the local desktop/Codex host. Do not expose the pipe or an HTTP wrapper to the
internet.

## Find a cheat value

1. Pause the game at a known state and call `capture_ram`.
2. Resume, change one value in game (for example lose a life), then pause.
3. Call `filter_ram(change="decreased")`; repeat the game action and filter
   until few candidates remain. `equals` can match a known byte value.
4. Read candidate addresses, then use `write_memory` on a writable RAM block
   to test a candidate. Values and addresses are byte oriented and decimal;
   `hex_bytes` is a hexadecimal byte string such as `63` for 99.

The debugger exposes physical device blocks. A RAM mapper can contain several
banks, and an address in a listed block is not necessarily the CPU's currently
visible address. ROM blocks can be read but cannot be modified by this bridge.
Snapshots and writes require a paused emulator.

The server currently stores its search baseline only in memory. Restarting it
clears candidate history. Each call creates a fresh debugger snapshot, so
device indices may change after resetting the machine or changing cartridges.

## Verified game test

With a 128 KiB Nemesis 2 ROM (SHA-256
`1A0F09DF467036758C3CC8C9382E8FB79319278575E7A86B9461ED835F7D7608`),
the lives byte was found at physical RAM address `0xE200` (`57856` decimal).
While paused, writing `05` to that byte made the HUD show 5 lives; normal
gameplay then reduced it to 4 and 3. This address is specific to the tested
ROM and machine configuration. The ROM is not part of this repository or the
test archive.

## Scope for the pull request

The plugin uses the existing `Create12` debugger tool ABI and does not change
the emulator core. The native project is intentionally separate from the
default packaged plugins: copying the DLL to `Tools` is the opt-in step. The
MCP transport is the Python SDK over STDIO; the DLL only exposes a local,
same-user named pipe. This keeps network services and model credentials out of
the emulator.

References: [MCP Python SDK](https://py.sdk.modelcontextprotocol.io/),
[OpenAI local MCP setup](https://learn.chatgpt.com/docs/extend/mcp),
[Claude Desktop setup](https://py.sdk.modelcontextprotocol.io/get-started/real-host/).
