# PS1 Emulator

An educational Sony PlayStation emulator written in modern C++.

The project is at an early stage. It currently has BIOS loading, the initial
PlayStation memory map, and a testable interpreter for the core MIPS R3000A
integer instruction set. GPU, DMA, timers, controllers, audio, CD-ROM support,
and complete exception handling are still to come.

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
./build/ps1-emulator --bios path/to/scph1001.bin --steps 100000
```

Add `--trace` to print the PC and machine word for every instruction. Use
`--help` to list all command-line options.
