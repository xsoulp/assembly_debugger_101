Native C++20 / Qt 6 visual assembly debugger for Linux x86-64.

This update adds a new processor and instruction-step icon to the desktop launcher and application window, and simplifies the README installation and build instructions.

Features include instruction stepping through GDB/MI, a NASM source editor, registers and flags, signed/unsigned decimal register parts, ELF variable labels, MOV/ADD animations, and an RSP-following 80-byte stack view.

The Debian package targets **Ubuntu 24.04 amd64** and installs the desktop launcher, example source and runtime dependency declarations. The `v1.0.1` tag identifies the current release source, including installation-path and read-only-example handling, build dependency guidance, and an application screenshot in the README.

Open a terminal in the folder containing the downloaded `.deb` and run:

```sh
sudo apt install ./*.deb
```

Launch **ASM101** from the applications menu or run `asm101`. Build the example, Start, then Step. If the packaged example is read-only, Save/Build asks where to save your editable copy.

Author: Luis Pinto · @xsoulp · 2026.
