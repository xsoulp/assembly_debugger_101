#pragma once
#include "Assembler.h"
#include <array>
#include <sys/types.h>

namespace asm101 {
class NativeDebugger {
public:
    ~NativeDebugger();
    void build(const std::string &source);
    void start();
    void step();
    void stop();
    std::string snapshot();

private:
    Program program;
    void *mapping = nullptr, *stackMapping = nullptr;
    size_t mappingSize = 0, codeReserve = 0, stackMappingSize = 0, pageSize = 0;
    uint64_t initialSp = 0;
    pid_t child = -1;
    bool haveRegisters = false;
    std::array<uint64_t, 34> registers{};
    std::vector<int> codeBytes, dataBytes, stackBytes;
    std::string status = "Edit ARM64 source, then Build";
    void release();
    int waitStopped();
    void capture();
    std::vector<int> read(uint64_t address, size_t count);
};
} // namespace asm101
