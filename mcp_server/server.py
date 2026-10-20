"""MCP stdio server for the opt-in blueMSX+ debugger bridge."""

import json
import os
import time
from typing import Literal

from mcp.server.fastmcp import FastMCP


mcp = FastMCP("blueMSX+ Debugger")
_previous: dict[tuple[int, int], bytes] = {}
_candidates: dict[tuple[int, int], set[int]] = {}
_registered_pid: str | None = None


def bridge(command: str) -> dict:
    global _registered_pid
    pid = os.environ.get("BLUEMSX_PID", "")
    if pid and (not pid.isdecimal() or int(pid) <= 0):
        raise ValueError("BLUEMSX_PID must be a positive process ID")
    if not pid:
        deadline = time.monotonic() + 1.0
        while True:
            matches = [name for name in os.listdir(r"\\.\pipe")
                       if name.startswith("blueMSX-plus-mcp-")]
            if len(matches) == 1:
                pid = matches[0].removeprefix("blueMSX-plus-mcp-")
                break
            if len(matches) > 1:
                raise ValueError("Multiple blueMSX+ instances found; set BLUEMSX_PID")
            if time.monotonic() >= deadline:
                raise RuntimeError("No blueMSX+ MCP bridge found")
            time.sleep(0.01)
    path = rf"\\.\pipe\blueMSX-plus-mcp-{pid}"
    if not command.startswith(("CLIENT_START ", "CLIENT_STOP ")) and _registered_pid != pid:
        try:
            bridge(f"CLIENT_START {os.getpid()}")
        except (OSError, RuntimeError, ValueError):
            pass
    try:
        deadline = time.monotonic() + 1.0
        while True:
            try:
                pipe = open(path, "r+b", buffering=0)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.01)
        with pipe:
            pipe.write((command + "\n").encode("ascii"))
            chunks = []
            total = 0
            while True:
                chunk = pipe.read(4096)
                if not chunk:
                    break
                chunks.append(chunk)
                total += len(chunk)
                if b"\n" in chunk:
                    break
                if total > 1_000_000:
                    raise RuntimeError("Bridge response is too large")
            line = b"".join(chunks)
    except OSError as exc:
        raise RuntimeError(f"Cannot connect to {path}; is McpBridge.dll loaded? {exc}") from exc
    if not line:
        raise RuntimeError("The emulator bridge closed without a response")
    result = json.loads(line)
    if "error" in result:
        raise ValueError(result["error"])
    if command.startswith("CLIENT_START "):
        _registered_pid = pid
    elif command.startswith("CLIENT_STOP "):
        _registered_pid = None
    return result


def _telemetry(command: str) -> None:
    """Status updates are best effort; a missing UI never aborts a cheat search."""
    try:
        bridge(command)
    except (OSError, RuntimeError, ValueError):
        pass


@mcp.tool()
def emulator_state() -> dict:
    """Return whether the emulator is running, paused or stopped."""
    return bridge("STATE")


@mcp.tool()
def pause_emulator() -> dict:
    """Pause emulation before inspecting memory or registers."""
    return bridge("PAUSE")


@mcp.tool()
def resume_emulator() -> dict:
    """Resume the game after inspection or a cheat edit."""
    return bridge("RUN")


@mcp.tool()
def step_emulator() -> dict:
    """Execute one debugger step while paused."""
    return bridge("STEP")


@mcp.tool()
def list_devices() -> dict:
    """List debugger devices and their memory blocks; requires pause. Addresses are decimal."""
    return bridge("LIST")


@mcp.tool()
def read_memory(device: int, block: int, address: int, size: int) -> str:
    """Read 1 to 2048 bytes from one debugger memory block; returns hexadecimal bytes."""
    return bridge(f"READ {device} {block} {address} {size}")["hex"]


@mcp.tool()
def write_memory(device: int, block: int, address: int, hex_bytes: str) -> dict:
    """Write bytes to a writable debugger memory block while paused. This changes game state."""
    data = bytes.fromhex(hex_bytes)
    if not 1 <= len(data) <= 2048:
        raise ValueError("Write 1 to 2048 bytes")
    return bridge(f"WRITE {device} {block} {address} {len(data)} {data.hex()}")


@mcp.tool()
def read_registers(device: int) -> dict:
    """Read the debugger register banks for one device while paused."""
    return bridge(f"REGISTERS {device}")


def _ram_blocks() -> list[tuple[int, int, int, int]]:
    blocks = []
    for device in list_devices()["devices"]:
        if device["type"] == 4:  # DEVTYPE_RAM
            for block in device["blocks"]:
                blocks.append((device["index"], block["index"], block["start"], block["size"]))
    return blocks


def _read_all(device: int, block: int, start: int, size: int,
              progress_base: int = 0, progress_total: int = 0) -> bytes:
    data = bytearray()
    for offset in range(0, size, 2048):
        chunk = bytes.fromhex(read_memory(device, block, start + offset, min(2048, size - offset)))
        data.extend(chunk)
        if progress_total and (len(data) == size or len(data) % 8192 == 0):
            _telemetry(f"SEARCH_PROGRESS {progress_base + len(data)} {progress_total}")
    return bytes(data)


@mcp.tool()
def capture_ram() -> dict:
    """Capture all debugger RAM blocks. Pause at a known game state, then resume, change the value, pause and call filter_ram."""
    _previous.clear()
    _candidates.clear()
    blocks = _ram_blocks()
    total_size = sum(size for _, _, _, size in blocks)
    _telemetry(f"SEARCH_BEGIN capture {total_size}")
    total = 0
    for device, block, start, size in blocks:
        _previous[(device, block)] = _read_all(device, block, start, size, total, total_size)
        _candidates[(device, block)] = set(range(size))
        total += size
    _telemetry(f"SEARCH_END {total}")
    return {"blocks": len(_previous), "bytes": total}


@mcp.tool()
def filter_ram(change: Literal["changed", "unchanged", "increased", "decreased", "equals"],
               value: int = 0, limit: int = 100) -> dict:
    """Compare RAM with the previous pause and narrow candidate byte addresses. Use equals with value 0..255; each call updates the baseline."""
    if not _previous:
        raise ValueError("Call capture_ram first")
    if change == "equals" and not 0 <= value <= 255:
        raise ValueError("value must be a byte")
    if not 1 <= limit <= 500:
        raise ValueError("limit must be 1..500")
    found = []
    total = 0
    current_blocks = {(d, b): (start, size) for d, b, start, size in _ram_blocks()}
    scan_total = sum(size for _, size in current_blocks.values())
    _telemetry(f"SEARCH_BEGIN filter_{change} {scan_total}")
    scanned = 0
    for key, before in list(_previous.items()):
        if key not in current_blocks:
            _previous.pop(key)
            _candidates.pop(key)
            continue
        start, size = current_blocks[key]
        after = _read_all(*key, start, size, scanned, scan_total)
        scanned += size
        if len(after) != len(before):
            _candidates[key] = set(range(len(after)))
            before = after
        matches = set()
        for offset in _candidates[key]:
            old, new = before[offset], after[offset]
            if ((change == "changed" and new != old) or
                (change == "unchanged" and new == old) or
                (change == "increased" and new > old) or
                (change == "decreased" and new < old) or
                (change == "equals" and new == value)):
                matches.add(offset)
                if len(found) < limit:
                    found.append({"device": key[0], "block": key[1],
                                  "address": start + offset, "previous": old, "current": new})
        _candidates[key] = matches
        _previous[key] = after
        total += len(matches)
    rows = " ".join(f"{item['device']}:{item['block']}:{item['address']:X}:"
                    f"{item['previous']}:{item['current']}" for item in found[:50])
    _telemetry(f"SEARCH_END {total}" + (f" {rows}" if rows else ""))
    return {"count": total, "candidates": found, "truncated": total > limit}


if __name__ == "__main__":
    _telemetry(f"CLIENT_START {os.getpid()}")
    try:
        mcp.run(transport="stdio")
    finally:
        _telemetry(f"CLIENT_STOP {os.getpid()}")
