# ASM101 v1.0

A native C++20 / Qt 6 Widgets visual debugger for NASM x86-64 Linux programs. Uses NASM, GNU ld and a real asynchronous GDB/MI2 session. The dark three-panel layout follows `mockup.png`; the RAM view follows the brief's two logical columns, with eight individually painted bytes per row. Flags remain visible below the registers.

## Build and run (Ubuntu 24.04)

```sh
sudo apt-get install build-essential cmake ninja-build qt6-base-dev nasm binutils gdb
cmake -S . -B build -G Ninja
cmake --build build
./build/asm101
```

The included `examples/example.asm` opens at startup. Open another `.asm` file with Ctrl+O. Save with Ctrl+S. Build (F7), Start (F5), then Step (F10) to execute exactly one machine instruction. Stop is Shift+F5. GDB stops at a temporary `_start` breakpoint before the first instruction. Source remains read-only while building or debugging. Unsaved edits can be saved or discarded when opening another file or closing.

Build saves the editor contents and runs `nasm -f elf64 -g -F dwarf` and `ld` asynchronously, using a temporary directory for object/executable artifacts. It never substitutes generated source. Rebuild after changing the source. NASM includes resolve relative to the source directory. The current version supports static, non-PIE ELF64 executables with `_start`.

## What the displays mean

- Registers contain actual GDB values, as 64-bit hex. RAX/RBX/RCX/RDX also show their low word, high byte and low byte (`AX   AH AL` through `DX   DH DL`) in decimal. The Unsigned/Signed toggle beside the register heading switches between unsigned values and two’s-complement signed values at each part’s own width (8 or 16 bits). Hexadecimal values and the process state stay unchanged. Green backgrounds mark changes since the preceding stop; RIP is yellow.
- CF, ZF, SF, OF come from EFLAGS/RFLAGS. Changed flags turn green until the next stop.
- The source gutter shows a yellow execution arrow and highlights the DWARF source line. Instruction stepping can remain on the same source line.
- Memory uses real ELF `.text`, `.rodata`, `.data`, `.bss` bounds and section colors per byte, including boundaries inside rows. Noncontiguous regions have an ellipsis separator. Addresses outside those sections are not synthesized as zeros.
- Variable labels from the executable’s ELF symbols appear above their first byte in `.data`, `.rodata` and `.bss`. Hover a label for its full name and address. Long labels are shortened to fit before the next label; aliases share a label. Only annotated rows gain height. Names require an executable with its symbol table intact.
- The stack panel is anchored at the lower right: ten rows of eight real bytes starting at RSP. It follows RSP after every instruction, including PUSH/POP, and highlights changes to previously observed bytes. Reads use one 80-byte block, with per-row fallback if part is unreadable. Unknown or unreadable bytes remain `??`.
- Yellow byte outlines identify the next instruction using GDB disassembly opcode length. Green underlines mark changed bytes in cached memory. `??` means unreadable or not yet fetched.
- Memory is fetched on demand in blocks of up to 256 bytes, split at section boundaries. Up to 128 cached pages are retained. Cached blocks refresh on stops; newly visited pages have no prior observation against which to mark changes. Scrolling stays in place when stepping. Large sections are represented as rows without allocating their entire contents.

Plain `MOV` instructions animate the transferred hexadecimal value between visible operands for 450 ms after GDB confirms the stop. Loads travel from RAM to the exact register byte range (including AL/AH/AX/EAX); stores travel back to RAM; immediates start at the source operand in the editor. Register-to-register moves are supported for displayed register families. The value is read from the actual destination after the instruction and checked against the original source register/immediate when available. Memory addressing supports absolute addresses and 64-bit base/index/displacement or RIP-relative addresses from GDB’s Intel disassembly. Other instruction variants or unavailable/offscreen endpoints skip the animation. Step stays disabled until completion; Stop, resizing or scrolling cancels the animation without executing another instruction. Ordinary register and memory change indicators remain visible. `ADD` also animates its second operand toward the first, prefixed with `+`. Its original operands are captured before stepping (with asynchronous GDB memory reads when needed); the animation only runs after the actual destination matches the addition at the correct operand width.

The status bar reports build and execution state; errors appear in native dialogs. Inferior terminal input/output is not presented in this first version. Stop terminates GDB and its inferior; there is no separate pause control. Source-level breakpoint management, editing process state, multithreaded and remote debugging are outside this version's scope.

## Verification

```sh
ctest --test-dir build --output-on-failure
```

The backend test assembles and links the fixed fixture in `tests/example.asm`, verifies the initial source mapping, registers, flags and memory, checks that RIP advances by the exact previous instruction length, verifies RAX = 10 then 15, verifies the memory write, and steps through process exit. The offscreen Qt GUI test exercises Build, Start, Step through exit, restart and Stop; it checks source highlighting, changed register/byte indicators and memory reset on restart. It writes a GUI capture to `/tmp/asm101-test.png`.

A further edge test checks two instructions mapped to one source line, changing ZF, initial and written `.bss`, all four ELF sections, and section colors across a boundary within one row.

The variable-label GUI test checks that `v1`, `v2`, `v3` sit above the actual `28 64 50` bytes, verifies the full-address tooltip and row heights, steps until `resultado` changes to `49`, and checks labels after restart. It also verifies MOV values, direction, indexed and RIP-relative decoding, partial registers, Step locking and cancellation with Stop. Screenshots are `/tmp/asm101-variables.png`, `/tmp/asm101-mov.png`, and `/tmp/asm101-add.png`.

The stack GUI test verifies all ten rows are visible, PUSH/POP values and addresses, writes via `[rsp]`, byte-change highlights, invalid RSP handling, and restart. Its screenshot is `/tmp/asm101-stack.png`.

GDB tests need permission to trace child processes; restricted containers/sandboxes may prevent this. Offscreen checks do not verify a physical desktop's font rendering, mouse-driven splitter resizing, file-dialog interaction, or classroom readability. Those require a manual desktop check.

Author: Luis Pinto · @xsoulp · 2026.
