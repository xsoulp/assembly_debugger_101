# ASM101 1.2.0

Native x86-64 assembly debugging for Ubuntu 24.04 amd64, using real NASM, ld and GDB.

## New features

- **Program arguments:** right-click Start or press **F11** to enter arguments. Quotes group text, empty arguments are supported, and values are passed literally. The real Linux initial stack contains `argc` and `argv`.
- **Scrollable, resizable stack:** inspect addresses above and below RSP in a window extending 64 KiB each way from the initial stack pointer. Drag the RAM/STACK divider to resize the panel. Memory loads on demand.
- **RSP focus and precise scrolling:** the stack centers on RSP at Start and when it changes. Each wheel notch or scrollbar arrow click moves one row. Click **stack (RSP)** or **base (RBP)** in the heading to jump to that pointer. Larger, bold byte labels keep both pointers readable.
- **Stdout panel:** view live program output at the bottom left. **F9** or **View → Stdout** hides/restores it; Clear empties the viewer. Capture continues while hidden, output survives exit/Stop, and a new Start clears it. UTF-8 and writes without a newline are supported. This is a stdout-only viewer; interactive stdin, stderr and terminal escape processing are not included.
- **Per-byte ASCII:** click a RAM or stack byte to toggle hex/ASCII, such as `41` ↔ `A`. Hover for its address, hex, decimal and ASCII interpretation. This changes only the display. Control characters use visible markers/escapes; a new run restores hex.
- **More stack transfers:** animate word/qword PUSH/POP between registers, memory and stack, including RSP-relative operands and R8–R15. The register panel now includes all 16 general-purpose registers.

## Download and install

Download `asm101_1.2.0-1_amd64.deb` and optionally verify it with `SHA256SUMS`.

```sh
sudo apt install ./asm101_1.2.0-1_amd64.deb
```

Launch **ASM101** from the applications menu or run `asm101`.

Build **F7** → Start **F5** → Step **F10**; Stop is **Shift+F5**.

This desktop release comes from `main`. Android development remains on the separate `android` branch.

## Validation

The release build runs nine integration tests covering real GDB execution, memory/symbols, stack transfers and pointer labels, program arguments, stdout capture and per-byte ASCII interaction.
