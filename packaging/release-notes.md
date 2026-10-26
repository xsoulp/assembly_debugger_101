Native C++20 / Qt 6 visual assembly debugger for Linux x86-64.

This update registers ASM101 in the applications menu when the Debian package is installed or upgraded. It refreshes the launcher and icon caches automatically and uses an absolute executable path without a TryExec visibility check. Removal refreshes the caches too.

The package includes PNG icons at 48, 64, 128 and 256 pixels alongside the SVG icon, so application menus can display the icon without an SVG loader.

It includes the fixed ten-row stack view with the initial RSP at the bottom, the latest example with stack writes and a function call, and the updated application icon.

Features include instruction stepping through GDB/MI, a NASM source editor, registers and flags, signed/unsigned decimal register parts, ELF variable labels, MOV/ADD animations, and a fixed 80-byte stack view.

The Debian package targets **Ubuntu 24.04 amd64** and installs the desktop launcher, example source and runtime dependency declarations. The `v1.0.3` tag identifies the current release source, including installation-path and read-only-example handling, build dependency guidance, and an application screenshot in the README.

Open a terminal in the folder containing the downloaded `.deb` and run:

```sh
sudo apt install ./*.deb
```

Launch **ASM101** from the applications menu or run `asm101`. Build the example, Start, then Step. If the packaged example is read-only, Save/Build asks where to save your editable copy.

Author: Luis Pinto · @xsoulp · 2026.
