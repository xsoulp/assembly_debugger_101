# ASM101 for Android

A native ARM64 assembly editor and visual debugger for an ordinary Android
device, without root. This is a separate Android application on the `android`
branch; the desktop Qt application stays in the repository root.

[Download the signed release APK](https://github.com/xsoulp/assembly_debugger_101/raw/refs/heads/android/android/apk/asm101-0.1.0-release.apk)
· [APK checksums and validation report](apk/)

**Status:** initial implementation. The APK and its instrumentation test APK
build successfully. Assembler output is checked against LLVM. Native execution
still needs validation on a physical ARM64 Android device; building an APK alone
does not prove that a particular vendor's kernel permits tracing.

## What runs

The assembler emits AArch64 machine instructions. A JNI backend maps the code
read/execute and data read/write, forks a child under the app's own UID, and uses
`PTRACE_TRACEME`, `PTRACE_SINGLESTEP`, `PTRACE_GETREGSET`/`NT_PRSTATUS`, and
`PTRACE_PEEKDATA` to execute and inspect it. There is no interpreter, emulated CPU,
GDB dependency, remote server, Termux dependency, or root command.

The parent initializes X0–X30 to zero, SP to a guarded, 16-byte-aligned native
stack, and PC to `_start` (or the first instruction). Every later displayed
register value comes from the stopped child. The tracer lives on one persistent
worker thread. Stop kills and reaps only that child. Faults stop the session and
preserve its last captured values. Backgrounding the app stops an active child.

Android's AOSP policy permits an ordinary app to
[trace its own domain](https://android.googlesource.com/platform/system/sepolicy/+/android16-release/private/untrusted_app_all.te#106)
and [use executable anonymous memory](https://android.googlesource.com/platform/system/sepolicy/+/android16-release/private/app.te#192).
The implementation uses anonymous code mappings rather than executing a generated
file from the writable app directory, which
[Android restricts](https://developer.android.com/about/versions/10/behavior-changes-10#execute-permission).
Device-specific denials are reported as errors; there is no simulation fallback.

## Use

Requires **64-bit ARM Android 9 or later** (`arm64-v8a` in
`ro.product.cpu.abilist`). A 64-bit CPU running a 32-bit Android userspace cannot
load this APK. x86 Android and ARM32 are not supported in this version.

1. Open ASM101. The variables/stack example is already loaded.
2. **Build**, then **Start**, then **Step** for one hardware instruction.
3. Use **Source**, **Registers**, **Memory**, and **Stack**. Landscape places
   source beside the other views. Green highlights mark changes between stops.
4. The menu includes open/save through Android's document picker, three examples,
   light/dark mode, and a description of native debugging.

The app also declares an Android TV launcher entry and optional touchscreen
support. Its buttons and tabs support D-pad focus. Hardware/ABI requirements are
the same on a TV. File picker availability depends on the TV firmware.

Registers include X0–X30, SP, PC, PSTATE, and NZCV flags. The register list also
shows unsigned decimal Xn and Wn values. Memory shows every declared variable,
the first 256 code bytes, and a fixed 80-byte stack window around the initial SP.
Unreadable bytes are `??`. A program which moves SP outside that window can
still be stepped, but the stack view does not follow it in this first version.

## Assembly syntax and limits

Use A64 syntax, not desktop NASM/x86 syntax. Examples are bundled in
[`app/src/main/assets/examples`](app/src/main/assets/examples).

```asm
.data
value:  .quad 10
result: .quad 0
.text
.global _start
_start:
    adr x9, value
    ldr x0, [x9]
    add x0, x0, #5
    str x0, [x9]
    str x0, [sp, #-16]!
    ldr x1, [sp], #16
    adr x9, result
    str x1, [x9]
```

The included assembler is an explicitly limited, two-pass assembler:

- `mov`, `movz`, `movk`, `movn`; `add`, `adds`, `sub`, `subs`, `cmp`, `cmn`;
  register `and`, `ands`, `orr`, `eor`; supported optional `lsl` operands.
- `adr`, `adrp`; `ldr`, `str`, `ldrb`, `strb`, `ldrh`, `strh`, `ldur`, `stur`;
  `stp`, `ldp`; immediate offset, pre-index and post-index addressing.
- `b`, `bl`, `b.<condition>`, `cbz`, `cbnz`, `br`, `blr`, `ret`, `nop`, `brk`, `svc`.
- `.text`, `.data`, `.bss`, `.section`, `.global`, `.globl`; `.quad`, `.word`,
  `.hword`, `.byte`, `.zero`, `.space`, `.balign`, `.align` in data. `.bss` shares
  the data mapping. `.align` takes a power-of-two exponent, as in GNU AArch64.
- `.inst` emits one raw 32-bit instruction for other A64 instructions. It is
  executed by the CPU, not interpreted by the app.

Source is limited to 128 KiB, code to 4096 instructions, and data to 4096 bytes.
Instructions expand to exactly one 4-byte A64 instruction. A `mov` immediate
which needs multiple instructions reports an error; use `movz`/`movk` explicitly.
Unsupported syntax, unresolved labels, invalid register widths, and overflowing
offsets report line-numbered errors.

The debugger stops when PC reaches the end of the source, leaves the assembled
program, hits a breakpoint, faults, exits, or times out. Each step has a two-second
watchdog so a blocking syscall cannot hang the UI. It debugs programs assembled
inside this app; attaching to another application and loading arbitrary ELF files
are outside this version's scope. SIMD/FP register display, breakpoints, a Run
button, and the desktop's flying transfer overlays are not yet included.

## Build

Use JDK 17 or newer (JDK 21 tested), Android SDK platform/build-tools 36, and
Android NDK 27.3.13750724. No external Android libraries are required by the app.
The wrapper pins Gradle 9.6.0 and checks its distribution SHA-256.

```sh
cd android
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.3.13750724"
./gradlew assembleDebug assembleDebugAndroidTest lintDebug
```

Alternatively supply `-Pasm101.ndkPath=/path/to/android-ndk-r27d`. The `preBuild`
task compiles JNI with the NDK directly, avoiding a separate SDK CMake dependency.
The current native build script supports Linux and macOS hosts. The library and
APK support 16 KiB native page alignment.

Outputs:

- `app/build/outputs/apk/debug/app-debug.apk`
- `app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`

To build a signed release, first run `python3 scripts/create-release-key.py`,
then `./gradlew assembleRelease lintRelease`. The key and its password remain
in the ignored `android/.signing/` directory. Keep a private backup to sign
compatible updates; they must never be committed. Without a local key, Gradle
produces an unsigned release APK. The published release has debugging disabled.

## Validate on hardware

Connect a physical ARM64 device with USB/wireless debugging enabled and authorize
ADB. For a TV, first use `adb connect TV_IP:5555` if its firmware supports that
ADB connection mode. Check `adb shell getprop ro.product.cpu.abilist` before
installing. Do not substitute an emulator for native execution verification.

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w \
  com.xsoulp.asm101.test/com.xsoulp.asm101.NativeDebuggerInstrumentation
adb shell am start -n com.xsoulp.asm101/.MainActivity
```

The instrumentation test reads actual kernel registers and child memory. It checks
loads/stores, register arithmetic, SP changes and stack values, final variables,
NZCV, loops, function calls, restart/reset, SIGSEGV, process exit, and cleanup.

Host assembler checks compare emitted opcodes against LLVM assembly/link output:

```sh
python3 tests/test_assembler.py
# If lld is not installed on PATH:
ASM101_LLD="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/ld.lld" \
  python3 tests/test_assembler.py
```

These tests verify encodings; they do not claim to validate CPU execution on the
host. Install LLVM clang, objcopy, and lld to run them.
