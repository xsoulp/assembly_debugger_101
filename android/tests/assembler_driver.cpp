#include "Assembler.h"
#include <iostream>
#include <iterator>

int main() {
    const std::string source{std::istreambuf_iterator<char>(std::cin), {}};
    try {
        auto program = asm101::assemble(source, 0x100000, 0x110000);
        for (const auto &instruction : program.instructions) {
            uint32_t word = instruction.word;
            std::cout.write(reinterpret_cast<const char *>(&word), sizeof(word));
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
