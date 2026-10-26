Native C++20 / Qt 6 visual assembly debugger for Linux x86-64.

Features include instruction stepping through GDB/MI, a NASM source editor, registers and flags, signed/unsigned decimal register parts, ELF variable labels, MOV/ADD animations, and an RSP-following 80-byte stack view.

The Debian package targets **Ubuntu 24.04 amd64** and installs the desktop launcher, example source and runtime dependency declarations. The `v1.0` tag identifies the current release source, including installation-path and read-only-example handling, build dependency guidance, and an application screenshot in the README.

Install the downloaded package with:

```sh
asm101_install_dir=$(mktemp -d /tmp/asm101-install.XXXXXX)
chmod 755 "$asm101_install_dir"
install -m 644 ./asm101_1.0-1_amd64.deb "$asm101_install_dir/"
sudo apt install "$asm101_install_dir/asm101_1.0-1_amd64.deb"
```

Launch **ASM101** from the applications menu or run `asm101`. Build the example, Start, then Step. If the packaged example is read-only, Save/Build asks where to save your editable copy.

Author: Luis Pinto · @xsoulp · 2026.
