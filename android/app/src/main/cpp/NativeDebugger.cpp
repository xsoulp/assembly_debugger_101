#include "NativeDebugger.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__aarch64__)
#include <asm/ptrace.h>
#include <elf.h>
#endif

namespace asm101 {
namespace {
[[noreturn]] void systemError(const std::string &what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}
std::string hex(uint64_t n) {
    std::ostringstream s;
    s << "0x" << std::hex << std::setfill('0') << std::setw(16) << n;
    return s.str();
}
std::string quote(const std::string &s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c >= 32)
            out += char(c);
    }
    return out + "\"";
}
} // namespace
NativeDebugger::~NativeDebugger() {
    stop();
    release();
}
void NativeDebugger::release() {
    if (mapping)
        munmap(mapping, mappingSize);
    if (stackMapping)
        munmap(stackMapping, stackMappingSize);
    mapping = stackMapping = nullptr;
    program = {};
    haveRegisters = false;
    codeBytes.clear();
    dataBytes.clear();
    stackBytes.clear();
}
void NativeDebugger::build(const std::string &source) {
    stop();
    release();
    pageSize = size_t(sysconf(_SC_PAGESIZE));
    codeReserve = (16384 + 4 + pageSize - 1) / pageSize * pageSize;
    mappingSize = codeReserve + pageSize;
    mapping =
        mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        mapping = nullptr;
        systemError("Allocate program");
    }
    try {
        const auto code = reinterpret_cast<uint64_t>(mapping);
        program = assemble(source, code, code + codeReserve);
        auto words = static_cast<uint32_t *>(mapping);
        for (size_t i = 0; i < program.instructions.size(); ++i)
            words[i] = program.instructions[i].word;
        words[program.instructions.size()] = 0xd4202020u; // BRK sentinel beyond the user's code.
        std::memcpy(static_cast<char *>(mapping) + codeReserve, program.data.data(),
                    program.data.size());
        __builtin___clear_cache(static_cast<char *>(mapping),
                                static_cast<char *>(mapping) + codeReserve);
        if (mprotect(mapping, codeReserve, PROT_READ | PROT_EXEC) != 0)
            systemError("Enable native code execution");
        stackMappingSize = 65536 + 2 * pageSize;
        stackMapping =
            mmap(nullptr, stackMappingSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stackMapping == MAP_FAILED) {
            stackMapping = nullptr;
            systemError("Allocate stack");
        }
        if (mprotect(static_cast<char *>(stackMapping) + pageSize, 65536, PROT_READ | PROT_WRITE) !=
            0)
            systemError("Enable stack");
        initialSp = reinterpret_cast<uint64_t>(stackMapping) + pageSize + 65536 - 16;
        status = "Built " + std::to_string(program.instructions.size()) +
                 " native A64 instructions · Start";
    } catch (...) {
        release();
        throw;
    }
}
int NativeDebugger::waitStopped() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    int result = 0;
    for (;;) {
        auto pid = waitpid(child, &result, WNOHANG | __WALL);
        if (pid == child)
            return result;
        if (pid < 0 && errno != EINTR)
            systemError("Wait for debuggee");
        if (std::chrono::steady_clock::now() >= deadline) {
            stop();
            throw std::runtime_error("Instruction timed out; the child was stopped. Blocking "
                                     "system calls cannot be stepped indefinitely.");
        }
        struct timespec pause {
            0, 1000000
        };
        nanosleep(&pause, nullptr);
    }
}
void NativeDebugger::start() {
#if !defined(__aarch64__)
    throw std::runtime_error("Native debugging requires a physical ARM64 Android device");
#else
    if (!mapping)
        throw std::runtime_error("Build before starting");
    stop();
    // Each run begins with the compiled data; the child receives private COW pages.
    std::memset(static_cast<char *>(mapping) + codeReserve, 0, pageSize);
    std::memcpy(static_cast<char *>(mapping) + codeReserve, program.data.data(),
                program.data.size());
    std::memset(static_cast<char *>(stackMapping) + pageSize, 0, 65536);
    haveRegisters = false;
    codeBytes.clear();
    dataBytes.clear();
    stackBytes.clear();
    const pid_t parent = getpid();
    child = fork();
    if (child < 0)
        systemError("Fork native debuggee");
    if (child == 0) {
        // No ART, JNI, allocation or C++ runtime calls in the post-fork child.
        if (syscall(__NR_prctl, PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) < 0)
            syscall(__NR_exit_group, 125);
        if (syscall(__NR_getppid) != parent)
            syscall(__NR_exit_group, 125);
        if (syscall(__NR_prctl, PR_SET_DUMPABLE, 1, 0, 0, 0) < 0)
            syscall(__NR_exit_group, 125);
        if (syscall(__NR_ptrace, PTRACE_TRACEME, 0, 0, 0) < 0)
            syscall(__NR_exit_group, 126);
        const long pid = syscall(__NR_getpid);
        syscall(__NR_tgkill, pid, pid, SIGSTOP);
        syscall(__NR_exit_group, 127); // PC is redirected to the assembled code while stopped.
        __builtin_unreachable();
    }
    try {
        int result = waitStopped();
        if (!WIFSTOPPED(result)) {
            child = -1;
            throw std::runtime_error(
                "Android denied native child tracing (ptrace). No simulated results are used.");
        }
        if (ptrace(PTRACE_SETOPTIONS, child, nullptr, reinterpret_cast<void *>(PTRACE_O_EXITKILL)) <
            0)
            systemError("Configure native tracing");
        user_pt_regs regs{};
        struct iovec io {
            &regs, sizeof(regs)
        };
        if (ptrace(PTRACE_GETREGSET, child, reinterpret_cast<void *>(NT_PRSTATUS), &io) < 0)
            systemError("Read ARM64 register set");
        for (auto &r : regs.regs)
            r = 0;
        regs.sp = initialSp;
        regs.pc = program.entry;
        // Preserve kernel-required EL0 bits; clear NZCV and the single-step state.
        regs.pstate &= ~uint64_t(0xf0200000u);
        if (ptrace(PTRACE_SETREGSET, child, reinterpret_cast<void *>(NT_PRSTATUS), &io) < 0)
            systemError("Set ARM64 entry point");
        status = "Paused at _start · Step executes one real CPU instruction";
        capture();
    } catch (...) {
        stop();
        throw;
    }
#endif
}
void NativeDebugger::step() {
#if !defined(__aarch64__)
    throw std::runtime_error("Native debugging requires a physical ARM64 Android device");
#else
    if (child <= 0)
        throw std::runtime_error("Start before stepping");
    try {
        if (ptrace(PTRACE_SINGLESTEP, child, nullptr, nullptr) < 0)
            systemError("Hardware single-step");
        int result = waitStopped();
        if (WIFEXITED(result) || WIFSIGNALED(result)) {
            child = -1;
            status = WIFEXITED(result)
                         ? "Program exited with code " + std::to_string(WEXITSTATUS(result))
                         : "Program terminated by signal " + std::to_string(WTERMSIG(result));
            return;
        }
        if (!WIFSTOPPED(result))
            throw std::runtime_error("Unexpected native process state");
        capture();
        const auto code = reinterpret_cast<uint64_t>(mapping);
        const auto pc = registers[32];
        if (WSTOPSIG(result) != SIGTRAP) {
            stop();
            status = "Stopped on signal " + std::to_string(WSTOPSIG(result)) +
                     " · inspect registers and memory";
            return;
        }
        siginfo_t info{};
        if (ptrace(PTRACE_GETSIGINFO, child, nullptr, &info) < 0)
            systemError("Read stop reason");
        if (info.si_code != TRAP_TRACE) {
            stop();
            status = "Stopped on a breakpoint trap · final registers and memory preserved";
            return;
        }
        if (pc == code + program.instructions.size() * 4) {
            stop();
            status = "Program complete · final registers and memory preserved";
        } else if (pc < code || pc >= code + program.instructions.size() * 4 || (pc - code) % 4) {
            stop();
            status = "Execution left the assembled program · session stopped";
        } else {
            status = "Paused · real ARM64 registers and memory · Step to continue";
        }
    } catch (...) {
        stop();
        throw;
    }
#endif
}
void NativeDebugger::stop() {
    if (child > 0) {
        const pid_t pid = child;
        child = -1;
        // SIGKILL wakes a ptrace-stop; wait/reap only our own child.
        kill(pid, SIGKILL);
        int result;
        while (waitpid(pid, &result, __WALL) < 0 && errno == EINTR) {
        }
        status = "Stopped · last captured values preserved";
    }
}
std::vector<int> NativeDebugger::read(uint64_t address, size_t count) {
    std::vector<int> bytes(count, -1);
    for (size_t i = 0; i < count; i += sizeof(long)) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, child, reinterpret_cast<void *>(address + i), nullptr);
        if (word == -1 && errno)
            continue;
        for (size_t n = 0; n < sizeof(long) && i + n < count; ++n)
            bytes[i + n] = int((static_cast<unsigned long>(word) >> (n * 8)) & 255);
    }
    return bytes;
}
void NativeDebugger::capture() {
#if defined(__aarch64__)
    user_pt_regs regs{};
    struct iovec io {
        &regs, sizeof(regs)
    };
    if (ptrace(PTRACE_GETREGSET, child, reinterpret_cast<void *>(NT_PRSTATUS), &io) < 0)
        systemError("Read stopped registers");
    for (int i = 0; i < 31; ++i)
        registers[i] = regs.regs[i];
    registers[31] = regs.sp;
    registers[32] = regs.pc;
    registers[33] = regs.pstate;
    haveRegisters = true;
    codeBytes = read(reinterpret_cast<uint64_t>(mapping),
                     std::min(size_t(256), program.instructions.size() * 4));
    const size_t dataCount = (program.data.size() + 7) / 8 * 8;
    dataBytes = read(reinterpret_cast<uint64_t>(mapping) + codeReserve, dataCount);
    stackBytes = read(initialSp - 72, 80);
#endif
}
std::string NativeDebugger::snapshot() {
    std::ostringstream out;
    out << "{\"built\":" << (mapping ? "true" : "false")
        << ",\"active\":" << (child > 0 ? "true" : "false")
        << ",\"pid\":" << (child > 0 ? child : 0) << ",\"status\":" << quote(status)
        << ",\"architecture\":\"AArch64\",\"registers\":[";
    if (haveRegisters)
        for (size_t i = 0; i < registers.size(); ++i) {
            if (i)
                out << ',';
            out << quote(hex(registers[i]));
        }
    out << "],\"line\":";
    const auto code = reinterpret_cast<uint64_t>(mapping);
    auto pc = haveRegisters ? registers[32] : 0;
    bool atInstruction = mapping && haveRegisters && pc >= code &&
                         pc < code + program.instructions.size() * 4 && (pc - code) % 4 == 0;
    out << (atInstruction ? program.instructions[(pc - code) / 4].line : 0);
    out << ",\"instruction\":"
        << quote(atInstruction ? program.instructions[(pc - code) / 4].source : "")
        << ",\"memory\":[";
    bool comma = false;
    auto rows = [&](const std::string &section, uint64_t address, const std::vector<int> &bytes) {
        for (size_t i = 0; i < bytes.size(); i += 8) {
            if (comma)
                out << ',';
            comma = true;
            out << "{\"section\":" << quote(section) << ",\"address\":" << quote(hex(address + i))
                << ",\"labels\":[";
            bool labelComma = false;
            for (const auto &s : program.symbols)
                if (s.address >= address + i && s.address < address + i + 8 &&
                    s.data == (section == ".data")) {
                    if (labelComma)
                        out << ',';
                    labelComma = true;
                    out << "{\"name\":" << quote(s.name)
                        << ",\"offset\":" << s.address - address - i << '}';
                }
            out << "],\"bytes\":[";
            for (size_t n = i; n < i + 8; ++n) {
                if (n != i)
                    out << ',';
                out << (n < bytes.size() ? bytes[n] : -1);
            }
            out << "]}";
        }
    };
    if (mapping) {
        std::vector<int> preview = codeBytes;
        if (!haveRegisters)
            for (const auto &instruction : program.instructions)
                for (int b = 0; b < 4; ++b)
                    preview.push_back((instruction.word >> (b * 8)) & 255);
        // All variable bytes are shown; code bytes are limited to a compact preview.
        if (preview.size() > 256)
            preview.resize(256);
        rows(".text", code, preview);
        if (haveRegisters)
            rows(".data", code + codeReserve, dataBytes);
        else {
            std::vector<int> initial(program.data.begin(), program.data.end());
            rows(".data", code + codeReserve, initial);
        }
        if (haveRegisters)
            rows("stack", initialSp - 72, stackBytes);
    }
    out << "]}";
    return out.str();
}
} // namespace asm101
