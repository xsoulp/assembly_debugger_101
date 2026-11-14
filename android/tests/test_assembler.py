#!/usr/bin/env python3
"""Compare the assembler's output against LLVM's AArch64 assembler and linker."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "app/src/main/cpp"


class AssemblerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="asm101-assembler-")
        cls.work = Path(cls.temp.name)
        cls.driver = cls.work / "assembler"
        subprocess.run(["c++", "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-I", str(CPP), str(ROOT / "tests/assembler_driver.cpp"),
                        str(CPP / "Assembler.cpp"), "-o", str(cls.driver)], check=True)
        cls.clang = shutil.which("clang-18") or shutil.which("clang")
        cls.objcopy = shutil.which("llvm-objcopy-18") or shutil.which("llvm-objcopy")
        cls.linker = os.environ.get("ASM101_LLD") or shutil.which("ld.lld")
        if not all((cls.clang, cls.objcopy, cls.linker)):
            raise RuntimeError("Install LLVM clang/objcopy/lld, or set ASM101_LLD to the NDK ld.lld")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def assemble(self, source):
        return subprocess.run([str(self.driver)], input=source.encode(), capture_output=True)

    def compare(self, source):
        result = self.assemble(source)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        assembly = self.work / "program.s"
        obj = self.work / "program.o"
        elf = self.work / "program.elf"
        binary = self.work / "program.bin"
        assembly.write_text(source)
        subprocess.run([self.clang, "--target=aarch64-linux-gnu", "-c", str(assembly), "-o", str(obj)], check=True)
        subprocess.run([self.linker, "-m", "aarch64elf", "-Ttext=0x100000", "-Tdata=0x110000",
                        "--entry=_start", str(obj), "-o", str(elf)], check=True)
        subprocess.run([self.objcopy, "--dump-section=.text=" + str(binary), str(elf)], check=True)
        expected = binary.read_bytes()
        self.assertEqual(result.stdout, expected, "Emitted ARM64 opcodes differ from LLVM")

    def test_move_and_arithmetic(self):
        self.compare(""".text
.global _start
_start:
mov x0, #10
mov w1, #65535
mov x2, #-1
mov x3, #0x10000
mov x4, #0xff00000000000000
mov x5, x4
mov w6, w5
mov x29, sp
mov sp, x29
movz x7, #0x1234, lsl #48
movk x7, #0xabcd, lsl #32
movn w8, #123, lsl #16
add x0, x0, #5
adds w1, w1, #4095
sub sp, sp, #16
sub x10, sp, #16
subs xzr, x0, #1
cmp x0, #5
cmn w0, #1
add x11, x0, x1, lsl #3
sub w12, w1, w2, lsl #2
cmp x11, x12
and x13, x11, x12
ands w13, w11, w12
orr x13, xzr, x12, lsl #4
eor x14, x14, x14
add x15, x16, #1, lsl #12
nop
""")

    def test_memory_and_stack(self):
        self.compare(""".text
.global _start
_start:
ldr x0, [x9]
str x0, [x9, #32760]
ldr w1, [x9, #16380]
strb w2, [x9, #4095]
ldrh w3, [x9, #8190]
ldrb w4, [x9]
strh w5, [x9, #2]
ldr x6, [x9, #-8]
ldur w6, [x9, #-255]
stur x6, [x9, #255]
str x0, [sp, #-16]!
ldr x1, [sp], #16
stp x29, x30, [sp, #-16]!
ldp x29, x30, [sp], #16
stp w1, w2, [sp, #252]
ldp x3, x4, [sp, #-512]
str xzr, [sp]
ldr wzr, [sp]
""")

    def test_branches_and_symbols(self):
        self.compare(""".data
value: .quad 10
result: .quad 0
.text
.global _start
_start:
adr x9, value
adrp x10, result
b forward
back:
subs x0, x0, #1
b.ne back
b.eq forward
b.hs forward
b.lo forward
b.ge forward
b.lt forward
cbz x1, forward
cbnz w2, back
bl forward
br x9
blr x10
ret
ret x11
forward:
brk #0x101
svc #0
.inst 0xd503201f
nop
""")

    def test_shipped_examples(self):
        for source in sorted((ROOT / "app/src/main/assets/examples").glob("*.s")):
            with self.subTest(example=source.name):
                self.compare(source.read_text())

    def test_invalid_operands_are_rejected(self):
        invalid = ["mov x0, #0x12345678", "mov x31, #1", "mov sp, xzr", "mov x0, w1",
                   "mov w0, #-2147483649", "mov x0, #-18446744073709551615",
                   "add x0, x1, #4096", "add x0, xzr, #1", "add x0, sp, x1", "ldr x0, [w1]",
                   "ldr x0, [xzr]", "ldr x0, [x0], #8", "ldp x0, x0, [sp]", "strb x0, [x1]",
                   "stp x0, x1, [sp, #7]", "movk w0, #1, lsl #32", "b missing", "unknown x0",
                   ".data\nvalue: .quad 1\n.text\n_start: .word 1", "nop\n_start: nop\n_start: nop"]
        for source in invalid:
            with self.subTest(source=source):
                result = self.assemble(source)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"Line ", result.stderr)

    def test_limits_and_entry(self):
        for source in ["", ".data\nv: .zero 4097\n.text\nnop", ".data\n_start: .quad 0\n.text\nnop",
                       ".text\nnop\n_start:", ".text\n" + "nop\n" * 4097]:
            with self.subTest(source=source[:60]):
                self.assertNotEqual(self.assemble(source).returncode, 0)

    def test_data_widths(self):
        for directive, minimum, maximum in [(".byte",-128,255),(".hword",-32768,65535),
                                             (".word",-2147483648,4294967295)]:
            for value in (minimum,maximum):
                self.assertEqual(self.assemble(f".data\nv: {directive} {value}\n.text\nnop\n").returncode,0)
            for value in (minimum-1,maximum+1):
                self.assertNotEqual(self.assemble(f".data\nv: {directive} {value}\n.text\nnop\n").returncode,0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
