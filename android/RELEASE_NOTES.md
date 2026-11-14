# ASM101 Android 0.1.0

Initial Android preview for **ARM64 devices running Android 9 or newer**.

Download **asm101-0.1.0-release.apk** below and install it on your device.
The APK is signed, has app debugging disabled, and requires no root access.

- ARM64 assembly editor with Build, Start, Step, and Stop.
- Native child-process debugging through Linux ptrace; no CPU emulation.
- Views for source, X0–X30, SP, PC, PSTATE/NZCV, variables, memory, and stack.
- Examples for variables/stack, loops/flags, and function calls.
- Light/dark themes, landscape layout, and Android TV launcher/D-pad support.

Use **Build → Start → Step** with the default example.

This version includes a documented subset of A64 assembly syntax and line-numbered
errors for unsupported instructions. It does not load arbitrary ELF files or
attach to other applications.

Release/debug builds, Android lint, APK signature/alignment checks, and seven host
assembler tests passed. **Execution on a physical Android device is still awaiting
validation.** Device-specific tracing restrictions are reported as errors.

`SHA256SUMS` verifies the APK. `verification.json` records the completed checks.
Full source and instructions are in the `android` branch under `android/`.
