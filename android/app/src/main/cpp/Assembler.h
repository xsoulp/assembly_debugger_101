#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace asm101 {
struct Symbol {
    std::string name;
    uint64_t address;
    bool data;
};
struct Instruction {
    uint32_t word;
    int line;
    std::string source;
};
struct Program {
    std::vector<Instruction> instructions;
    std::vector<uint8_t> data;
    std::vector<Symbol> symbols;
    uint64_t entry = 0;
};
// An assembler only: emitted A64 opcodes execute on the device's physical CPU.
Program assemble(const std::string &source, uint64_t codeBase, uint64_t dataBase);
} // namespace asm101
