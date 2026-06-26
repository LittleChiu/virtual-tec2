# /// script
# requires-python = ">=3.10"
# dependencies = ["mcp>=1.2.0"]
# ///
"""
TEC-2 MCP server
================

Exposes the TEC-2 microprogrammed-computer simulator (this repository's
``tec2_cli``) to AI tools over the Model Context Protocol.

An AI agent can: reset the machine, write/inspect memory and registers,
assemble and disassemble TEC-2 programs (including the three custom
course-design instructions MADD / MMOV / JNE), single-step at the machine-
and micro-instruction level, run programs, and dynamically load the custom
microprograms into the control store via LDMC.

How it works
------------
The server launches a single persistent ``tec2_cli --server`` process
(a static, dependency-free binary built from ``tec2_cli.cpp``) and talks to
it over a tiny line protocol: one command per line, terminated by a fixed
``__TEC2_DONE__`` sentinel. All machine state lives in that process and
persists across tool calls.

Run
---
    uv run mcp/tec2_mcp_server.py            # stdio transport (for MCP clients)

If ``tec2_cli`` is not built yet, the server compiles it on first start
(needs a C++17 g++; MSYS2 UCRT64 on Windows).
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import threading
from pathlib import Path
from typing import Optional

from mcp.server.fastmcp import FastMCP

# --------------------------------------------------------------------------
# Locate the repository, the simulator binary and the ROMs
# --------------------------------------------------------------------------
REPO = Path(__file__).resolve().parent.parent          # mcp/ -> repo root
MCDIR = Path(__file__).resolve().parent / "microcode"
IS_WIN = os.name == "nt"
BIN = REPO / ("tec2_cli.exe" if IS_WIN else "tec2_cli")
ROMS = ["INST.ROM", "MAPROM.ROM", "MCR.ROM"]
SENTINEL = "__TEC2_DONE__"
READY = "__TEC2_READY__"

# Bundled custom-instruction microcode: name -> (control-store entry, scratch
# image base). Only the minimal JNE example ships with this repository; drop
# additional <name>.mcr files into mcp/microcode/ and add them here to load more.
CUSTOM = {
    "JNE": (0x160, 0x0900),   # jump-if-not-equal, 4 microinstructions
}
LOADER_ADDR = 0xFE00           # scratch address for the tiny LDMC loader program


def _build_binary() -> None:
    """Compile tec2_cli (statically, so it runs without runtime DLLs)."""
    srcs = ["tec2_cli.cpp", "tec2.cpp", "chip.cpp", "bit.cpp"]
    for s in srcs:
        if not (REPO / s).exists():
            raise FileNotFoundError(f"missing source {s} in {REPO}")
    flags = ["-std=c++17", "-O2"]
    if IS_WIN:
        flags += ["-static", "-static-libgcc", "-static-libstdc++"]
    else:
        flags += ["-static-libgcc", "-static-libstdc++"]
    cmd = ["g++", *flags, "-o", str(BIN), *srcs]
    proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True)
    if proc.returncode != 0 or not BIN.exists():
        raise RuntimeError(
            "failed to build tec2_cli; build it manually with a C++17 g++:\n"
            f"  cd {REPO}\n  {' '.join(cmd)}\n\n{proc.stderr}"
        )


class Engine:
    """Persistent connection to one ``tec2_cli --server`` process."""

    def __init__(self) -> None:
        self.proc: Optional[subprocess.Popen] = None
        self.lock = threading.Lock()

    def start(self) -> None:
        for rom in ROMS:
            if not (REPO / rom).exists():
                raise FileNotFoundError(f"missing ROM {rom} in {REPO}")
        if not BIN.exists():
            _build_binary()
        self.proc = subprocess.Popen(
            [str(BIN), "--server"],
            cwd=str(REPO),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        # consume lines until the ready banner
        assert self.proc.stdout is not None
        while True:
            line = self.proc.stdout.readline()
            if line == "":
                raise RuntimeError("tec2_cli exited before becoming ready")
            if line.strip() == READY:
                break

    def send(self, command: str) -> str:
        """Send one command line; return everything printed before the sentinel."""
        with self.lock:
            if self.proc is None or self.proc.poll() is not None:
                self.start()
            assert self.proc is not None and self.proc.stdin and self.proc.stdout
            self.proc.stdin.write(command.replace("\n", " ").strip() + "\n")
            self.proc.stdin.flush()
            out: list[str] = []
            while True:
                line = self.proc.stdout.readline()
                if line == "":
                    raise RuntimeError("tec2_cli terminated unexpectedly")
                if line.rstrip("\n") == SENTINEL:
                    break
                out.append(line.rstrip("\n"))
            return "\n".join(out)


ENGINE = Engine()


# --------------------------------------------------------------------------
# Output parsers
# --------------------------------------------------------------------------
def parse_registers(text: str) -> dict:
    regs: dict[str, int] = {}
    for m in re.finditer(r"R_?(\d+)=([0-9A-Fa-f]{4})", text):
        regs[f"R{int(m.group(1))}"] = int(m.group(2), 16)
    for name in ("SP", "PC", "IP"):
        m = re.search(name + r"=([0-9A-Fa-f]{4})", text)
        if m:
            regs[name] = int(m.group(1), 16)
    flags: dict[str, int] = {}
    fbits = ""
    fm = re.search(r"F_?=([01]{8})", text)
    if fm:
        fbits = fm.group(1)
        names = ["C", "Z", "V", "S", "INTE", "P2", "P1", "P0"]
        for i, nm in enumerate(names):
            flags[nm] = int(fbits[i])
    # the instruction at PC is echoed on the line after the flags
    cur = ""
    for ln in text.splitlines():
        if re.match(r"\s*[0-9A-Fa-f]{4}:\s", ln):
            cur = ln.strip()
    return {"registers": regs, "flags": flags, "flag_bits_CZVS_IP": fbits, "current_instruction": cur}


def parse_dump(text: str) -> list[dict]:
    rows = []
    for ln in text.splitlines():
        m = re.match(r"^\s*([0-9A-Fa-f]{4})\s+((?:[0-9A-Fa-f]{4}\s+){8})", ln)
        if not m:
            continue
        addr = int(m.group(1), 16)
        words = [int(w, 16) for w in m.group(2).split()]
        rows.append({"address": f"0x{addr:04X}", "words": [f"{w:04X}" for w in words]})
    return rows


def _disasm_word_count(disasm_line: str) -> int:
    """Number of machine words an assembled instruction occupied.

    The disassembly prints the raw machine words right after ``ADDR:`` and
    before the mnemonic; no TEC-2 mnemonic is exactly four hex digits, so the
    count of leading 4-hex-digit tokens is the instruction size.
    """
    after = disasm_line.split(":", 1)[1] if ":" in disasm_line else disasm_line
    n = 0
    for tok in after.split():
        if re.fullmatch(r"[0-9A-Fa-f]{4}", tok):
            n += 1
        else:
            break
    return max(n, 1)


def _hex4(v: int) -> str:
    return f"{v & 0xFFFF:04X}"


# --------------------------------------------------------------------------
# MCP tools
# --------------------------------------------------------------------------
mcp = FastMCP("tec2")


@mcp.tool()
def reset() -> str:
    """Reset the TEC-2 machine to its power-on state (registers cleared, SP=FFFF,
    standard microcode reloaded). Any custom microcode previously loaded via
    load_custom_microcode is cleared and must be reloaded."""
    ENGINE.send("RESET")
    return "machine reset"


@mcp.tool()
def read_registers() -> dict:
    """Read all 16 general registers, the SP/PC/IP aliases (R4/R5/R6), the
    condition flags (C, Z, V, S) and the instruction currently at PC."""
    return parse_registers(ENGINE.send("R"))


@mcp.tool()
def set_register(reg: str, value: str) -> dict:
    """Set a register to a 16-bit hex value. reg is R0..R15 or SP/PC/IP
    (e.g. reg='R7', value='0003'). Returns the new register state."""
    out = ENGINE.send(f"R {reg.upper()} {value}")
    if "Error" in out:
        return {"error": f"could not set {reg}={value}"}
    return parse_registers(ENGINE.send("R"))


@mcp.tool()
def write_memory(address: str, words: list[str]) -> dict:
    """Write a list of 16-bit hex words into main memory starting at `address`
    (hex). Example: address='0A00', words=['0001','0002','0003']."""
    addr = int(address, 16)
    payload = " ".join(f"{int(w, 16) & 0xFFFF:04X}" for w in words)
    ENGINE.send(f"M {addr:04X} {payload}")
    return {"written": len(words), "address": f"0x{addr:04X}"}


@mcp.tool()
def read_memory(address: str, count_words: int = 8) -> dict:
    """Dump `count_words` words of main memory starting at `address` (hex),
    shown 8 per row with their ASCII rendering."""
    rows = max(1, (count_words + 7) // 8)
    text = ENGINE.send(f"D {int(address,16):04X} {rows}")
    parsed = parse_dump(text)
    flat = [w for row in parsed for w in row["words"]][:count_words]
    return {"address": f"0x{int(address,16):04X}", "words": flat, "rows": parsed}


@mcp.tool()
def assemble(start_address: str, instructions: list[str]) -> dict:
    """Assemble TEC-2 assembly lines into memory starting at `start_address`
    (hex). Standard mnemonics plus the custom MADD / MMOV / JNE are supported,
    e.g. ['MOV R0,1234', 'MADD R7,R8,0A00,0A10,0A20', 'RET'].
    Returns the disassembly of each assembled instruction."""
    addr = int(start_address, 16)
    listing = []
    for ins in instructions:
        out = ENGINE.send(f"A {addr:04X} {ins}")
        line = out.strip().splitlines()[-1] if out.strip() else ""
        if "Error" in out or not line:
            return {"error": f"could not assemble '{ins}' at 0x{addr:04X}", "listing": listing}
        listing.append(line)
        addr = (addr + _disasm_word_count(line)) & 0xFFFF
    return {"listing": listing, "next_address": f"0x{addr:04X}"}


@mcp.tool()
def disassemble(address: str, count: int = 8) -> dict:
    """Disassemble `count` instructions starting at `address` (hex).
    The custom instructions MADD/MMOV/JNE are decoded with their mnemonics."""
    text = ENGINE.send(f"U {int(address,16):04X} {count}")
    return {"listing": [l for l in text.splitlines() if l.strip()]}


@mcp.tool()
def run(address: Optional[str] = None) -> dict:
    """Run the program. If `address` (hex) is given, start there; otherwise
    continue from PC. Execution is a subroutine call that stops when the
    matching RET returns (so test programs should end in RET), at a breakpoint,
    or on a dead self-loop. Returns the register/flag state afterwards."""
    cmd = "G" if address is None else f"G {int(address,16):04X}"
    out = ENGINE.send(cmd)
    state = parse_registers(ENGINE.send("R"))
    state["note"] = out.strip() or "ran to RET / breakpoint"
    return state


@mcp.tool()
def step(count: int = 1) -> dict:
    """Single-step `count` machine instructions (T command). Returns the
    register/flag state after the last step."""
    last = ""
    for _ in range(max(1, count)):
        last = ENGINE.send("T")
    return parse_registers(last)


@mcp.tool()
def microstep() -> dict:
    """Execute a single microinstruction (TT command) and return the micro-level
    status: next micro-address, the four pipeline-latch words, AR/MEM and the
    C/Z/V/S flags, plus the register file."""
    out = ENGINE.send("TT")
    return {"microstatus": [l for l in out.splitlines() if l.strip()],
            **parse_registers(out)}


@mcp.tool()
def view_microstate() -> dict:
    """Show the current microinstruction state without executing (V command):
    next micro-address, pipeline-latch words B3..B0, AR/MEM/MRA/A/B and C/Z/V/S."""
    return {"microstatus": [l for l in ENGINE.send("V").splitlines() if l.strip()]}


@mcp.tool()
def set_breakpoint(address: Optional[str] = None) -> str:
    """Set an execution breakpoint at `address` (hex); call with no address to
    clear it. `run` stops when PC reaches the breakpoint."""
    if address is None:
        ENGINE.send("B")
        return "breakpoint cleared"
    ENGINE.send(f"B {int(address,16):04X}")
    return f"breakpoint set at 0x{int(address,16):04X}"


@mcp.tool()
def hex_calc(value1: str, value2: str) -> dict:
    """16-bit hex add and subtract (H command): returns value1+value2 and
    value1-value2, both modulo 0x10000."""
    out = ENGINE.send(f"H {value1} {value2}").strip()
    parts = out.split()
    return {"sum": parts[0] if parts else "", "difference": parts[1] if len(parts) > 1 else "", "raw": out}


@mcp.tool()
def load_custom_microcode(name: str) -> dict:
    """Dynamically load a bundled custom microprogram into the control store via
    LDMC, so its machine instruction becomes executable.

    name is 'JNE' (or 'ALL', which loads every bundled microprogram).
    JNE R2,R3,DISP branches to IP+DISP when R2 != R3, else falls through. The
    standard ROMs do NOT contain this microprogram — loading it here mirrors the
    real lab workflow of staging the microcode in memory and loading it via LDMC.
    (Only the minimal JNE example ships with this repository.)

    Side effects: uses memory 0x0900.. (image) and 0xFE00.. (a tiny loader) as
    scratch; R1/R2/R3 are saved and restored. Lost on reset()."""
    names = list(CUSTOM) if name.upper() == "ALL" else [name.upper()]
    loaded = []
    saved = parse_registers(ENGINE.send("R"))["registers"]
    for nm in names:
        if nm not in CUSTOM:
            return {"error": f"unknown instruction '{nm}'; use MADD, MMOV, JNE or ALL"}
        entry, base = CUSTOM[nm]
        mcr = MCDIR / f"{nm.lower()}.mcr"
        if not mcr.exists():
            return {"error": f"missing microcode file {mcr}"}
        image: list[int] = []
        micro_count = 0
        for ln in mcr.read_text(encoding="utf-8").splitlines():
            m = re.match(r"\s*mcr\s+[0-9A-Fa-f]+\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)", ln)
            if not m:
                continue
            micro_count += 1
            image += [int(x, 16) for x in m.groups()]   # B3 B2 B1 B0
        # stage the image, build a loader (MOV R1,base; R2,count; R3,entry; LDMC; RET)
        ENGINE.send(f"M {base:04X} " + " ".join(_hex4(w) for w in image))
        loader = [0x2C10, base, 0x2C20, micro_count, 0x2C30, entry, 0xD000, 0xAC00]
        ENGINE.send(f"M {LOADER_ADDR:04X} " + " ".join(_hex4(w) for w in loader))
        ENGINE.send(f"G {LOADER_ADDR:04X}")
        loaded.append({"instruction": nm, "entry": f"0x{entry:03X}", "microinstructions": micro_count})
    # restore R1/R2/R3 clobbered by the loader
    for r in ("R1", "R2", "R3"):
        if r in saved:
            ENGINE.send(f"R {r} {saved[r]:04X}")
    return {"loaded": loaded, "hint": "now assemble & run, e.g. JNE R2,R3,0008 (branch to IP+8 when R2 != R3)"}


@mcp.tool()
def raw_command(command: str) -> str:
    """Escape hatch: send a raw tec2_cli command line and return its exact text
    output. Supports the full monitor command set (A/U/D/R/G/T/P/TT/V/B/E/H/M/
    LOAD/SAVE/RESET ...). Use the structured tools above when possible."""
    return ENGINE.send(command)


if __name__ == "__main__":
    try:
        ENGINE.start()
    except Exception as exc:  # surface a clear error to the MCP client/log
        print(f"[tec2-mcp] startup error: {exc}", file=sys.stderr)
        sys.exit(1)
    mcp.run()
