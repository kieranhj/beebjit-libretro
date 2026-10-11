# beebjit-libretro

A [libretro](https://www.libretro.com/) core that wraps Chris Evans'
[beebjit](https://github.com/scarybeasts/beebjit), a very fast BBC Micro
emulator with an ARM64 and x64 JIT. It exists so that BBC Micro games and demos
run at full speed in RetroArch on low-powered ARM handhelds. The first target
is the Anbernic RG40XXV (Allwinner H700, quad Cortex-A53) running muOS, where
beebjit emulates a 50 Hz frame in about 10 ms, roughly twice real time, while
the cycle-exact b2 core managed three quarters speed on the same hardware.

This core was written with [Claude Code](https://claude.com/claude-code),
Anthropic's agentic coding tool, working from Kieran Connell's direction. The
emulator itself is entirely beebjit; this repository only adds the glue.

## What works

- BBC Model B (OS 1.2, BASIC 2, DFS 0.9, 8271) or Master 128 (MOS 3.20,
  BASIC 4, DFS, ADFS, 1770), chosen with the "Machine" core option. ROMs are
  embedded at build time from the beebjit tree. Set the option per game from
  RetroArch's quick menu for discs that need a Master.
- Disc images in any format beebjit reads: `.ssd`, `.dsd`, `.hfe`, `.adl`,
  `.adf`, `.fsd`, `.dfi`, `.scp`, `.rfi`. The disc is auto-booted with
  Shift+BREAK (core option).
- Sideways RAM in banks 4 to 7 by default (core option).
- A "6502 core" option to force the interpreter, mainly for debugging.
- Sound at 48 kHz, generated in lockstep with emulation.
- RetroPad buttons map to BBC keys, each configurable from the core options.
  Host keyboards work through RetroArch's keyboard passthrough.
- BREAK on RetroArch's reset.
- The picture's aspect ratio is a Beeb monitor's: the 640x256 area of MODEs
  0 to 2 is 5:4, and beebjit's border makes the whole 768x320 frame 6:5. The
  "Aspect ratio" option set to 4:3 stretches the frame to fill the screen.
  The frontend has to use the core's aspect ratio (RetroArch's "Core
  provided").

## Not yet

- Master Compact, MOS 3.50, and any BBC B variants other than the 8271 one.
- Save states, disc swapping, tapes, joystick on the analogue port.
- Anything other than Linux hosts. beebjit's JIT also needs a 64-bit host.

## How it works

beebjit runs the emulated machine on its own thread and already pauses that
thread at every vertical sync until the frontend acknowledges the frame. The
core uses that handshake as its lockstep: `retro_run()` releases the thread for
one frame, waits for the next vsync message, presents the frame and hands over
the sound queued since the last call. Two small pieces of beebjit's OS layer are
replaced so the frontend is the only clock:

- `os_time_libretro.c`: the real monotonic clock, but a no-op sleeper.
- `os_sound_libretro.c`: a non-blocking ring buffer instead of a sound device.

Everything else is unmodified beebjit, pulled in as a git submodule, with one
build-time substitution: `asm_platform_libretro.h` replaces beebjit's fixed
address constants on Linux ARM64. beebjit maps the emulated machine and its
JIT at fixed virtual addresses, and the stock low addresses collide with the
heap of a long-lived frontend such as RetroArch.

Two things worth knowing if you touch the core:

- `bbc_create()` keeps the OS ROM pointer it is given and re-reads it every
  time a Master pages HAZEL out. The buffer has to outlive `retro_load_game()`.
- The Master runs on beebjit's interpreter, because beebjit's JIT and inturbo
  cores are 6502-only. It still keeps up on the RG40XXV, but with less margin
  than the Model B.

Debug aids, all read from the environment: `BEEBJIT_LOG_VIDEO=1` traces ULA,
CRTC and paging registers; `BEEBJIT_LOG_INPUT=1` traces pad and key events;
`BEEBJIT_DUMP_FRAME=<path>` writes one frame; `BEEBJIT_DEBUG=1 BEEBJIT_PRINT=1`
prints every 6502 instruction; `BEEBJIT_LOG_FLAGS` and `BEEBJIT_OPT_FLAGS` pass
beebjit's own flag strings; `BEEBJIT_AUTOKEY_FRAME=<n>` presses Space at frame
n for benchmarks. `scripts/rt.c` is a minimal libretro host for running the
core headlessly on the device, and `scripts/bgra2png.py` converts its frames.

## Building

Native Linux build:

    git clone --recursive https://github.com/kieranhj/beebjit-libretro
    cd beebjit-libretro
    make

Cross-build for an aarch64 handheld from any host with
[Zig](https://ziglang.org/) and GNU make on the PATH, no cross toolchain or
sysroot needed:

    scripts/build-aarch64.sh clean

The output is `out/beebjit_libretro.so` plus the `.info` file. See
`device/muos/README.md` for installing on muOS.

`scripts/test-core.sh` and `scripts/bench-core.sh` run a core headlessly on the
device over SSH and report whether it survived, its CPU use, and its speed
relative to real time. `scripts/dev.py` is a tiny paramiko helper for devices
whose SFTP subsystem cannot write under `/opt`.

## Licence

GPL version 3, the same as beebjit. See `LICENSE`. beebjit is copyright Chris
Evans; the ROM images embedded at build time come from the beebjit tree and are
copyright Acorn Computers.
