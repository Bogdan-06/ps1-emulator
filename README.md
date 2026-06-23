# PS1 Emulator

An educational Sony PlayStation emulator written in modern C++.

The project is at an early stage. The initial target is a testable interpreter
for the MIPS R3000A CPU, followed by GPU, DMA, timers, controllers, audio, and
CD-ROM support.

## Requirements

- A C++20 compiler
- GNU Make
- A legally obtained 512 KiB PlayStation BIOS dump

ROMs and BIOS files are intentionally ignored by Git and are not distributed
with this project.

## Build

```sh
make
make test
```

## Run

```sh
./build/ps1-emulator --bios path/to/scph1001.bin
```

Use `--help` to list the available command-line options.
