#include "Assembler.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace asm101 {
namespace {
std::string trim(const std::string &s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? ""
                                      : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
[[noreturn]] void fail(const std::string &s) {
    throw std::runtime_error(s);
}
std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> out;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i < s.size() && s[i] == '[')
            ++depth;
        if (i < s.size() && s[i] == ']')
            --depth;
        if (i == s.size() || (s[i] == ',' && depth == 0)) {
            auto item = trim(s.substr(start, i - start));
            if (!item.empty())
                out.push_back(item);
            else if (!trim(s).empty())
                fail("Empty operand");
            start = i + 1;
        }
    }
    if (depth)
        fail("Unbalanced memory operand");
    return out;
}
uint64_t number(std::string s) {
    s = trim(s);
    if (!s.empty() && s[0] == '#')
        s = trim(s.substr(1));
    if (s.empty())
        fail("Expected a number");
    size_t used = 0;
    uint64_t n;
    try {
        bool neg = s[0] == '-';
        auto digits = s.substr((neg || s[0] == '+') ? 1 : 0);
        n = std::stoull(digits, &used,
                        digits.starts_with("0x") || digits.starts_with("0X") ? 16 : 10);
        if (used != digits.size())
            fail("Invalid number: " + s);
        if (neg) {
            if (n > (uint64_t(1) << 63))
                fail("Negative number exceeds 64 bits");
            n = uint64_t(0) - n;
        }
    } catch (const std::exception &) {
        fail("Invalid number: " + s);
    }
    return n;
}
int64_t signedNumber(const std::string &s) {
    return static_cast<int64_t>(number(s));
}
void count(const std::vector<std::string> &a, size_t n) {
    if (a.size() != n)
        fail("Expected " + std::to_string(n) + " operands");
}
struct Reg {
    uint32_t n;
    bool wide, sp, zero;
};
Reg reg(std::string s) {
    s = lower(trim(s));
    if (s == "sp" || s == "wsp")
        return {31, s == "sp", true, false};
    if (s == "xzr" || s == "wzr")
        return {31, s == "xzr", false, true};
    if (s == "fp")
        return {29, true, false, false};
    if (s == "lr")
        return {30, true, false, false};
    if (s.size() < 2 || (s[0] != 'x' && s[0] != 'w') ||
        !std::all_of(s.begin() + 1, s.end(), [](unsigned char c) {
            return std::isdigit(c);
        }))
        fail("Expected an ARM64 register, got: " + s);
    const auto n = number(s.substr(1));
    if (n > 30)
        fail("Use X0–X30, W0–W30, SP or XZR");
    return {uint32_t(n), s[0] == 'x', false, false};
}
void same(Reg a, Reg b) {
    if (a.wide != b.wide)
        fail("Register widths must match");
}
void noSp(Reg r) {
    if (r.sp)
        fail("SP is not allowed in this operand");
}
void noZero(Reg r) {
    if (r.zero)
        fail("Zero register is not allowed in this operand");
}
uint32_t sf(Reg r) {
    return r.wide ? 0x80000000u : 0;
}
int64_t relative(uint64_t target, uint64_t pc, int bits, int scale) {
    const auto delta = static_cast<int64_t>(target - pc);
    if (delta % scale || delta / scale < -(int64_t(1) << (bits - 1)) ||
        delta / scale >= (int64_t(1) << (bits - 1)))
        fail("Target is out of range or misaligned");
    return delta / scale;
}
struct SourceInstruction {
    int line;
    std::string op, tail, original;
};
struct Label {
    bool data;
    size_t offset;
};
uint32_t encode(const SourceInstruction &s, uint64_t pc,
                const std::map<std::string, uint64_t> &labels) {
    const auto &op = s.op;
    auto a = split(s.tail);
    auto target = [&](const std::string &value) -> uint64_t {
        auto found = labels.find(trim(value));
        return found == labels.end() ? number(value) : found->second;
    };
    auto shifted = [](const std::string &value, unsigned limit) -> unsigned {
        auto v = lower(trim(value));
        if (!v.starts_with("lsl "))
            fail("Only LSL shifts are supported here");
        const auto n = number(v.substr(4));
        if (n > limit)
            fail("Shift out of range");
        return unsigned(n);
    };
    if (op == "nop") {
        count(a, 0);
        return 0xd503201f;
    }
    if (op == ".inst") {
        count(a, 1);
        auto n = number(a[0]);
        if (n > 0xffffffffu)
            fail(".inst needs one 32-bit opcode");
        return uint32_t(n);
    }
    if (op == "brk" || op == "svc") {
        count(a, 1);
        auto n = number(a[0]);
        if (n > 65535)
            fail("Immediate must fit 16 bits");
        return (op == "brk" ? 0xd4200000u : 0xd4000001u) | uint32_t(n << 5);
    }
    if (op == "ret" || op == "br" || op == "blr") {
        if (op == "ret" && a.empty())
            a.push_back("x30");
        count(a, 1);
        auto r = reg(a[0]);
        noSp(r);
        noZero(r);
        if (!r.wide)
            fail("Branch register must be X-sized");
        return (op == "ret" ? 0xd65f0000u : op == "br" ? 0xd61f0000u : 0xd63f0000u) | r.n << 5;
    }
    if (op == "b" || op == "bl") {
        count(a, 1);
        auto imm = relative(target(a[0]), pc, 26, 4);
        return (op == "b" ? 0x14000000u : 0x94000000u) | (uint32_t(imm) & 0x03ffffffu);
    }
    if (op.starts_with("b.")) {
        count(a, 1);
        const std::map<std::string, unsigned> cond = {
            {"eq", 0},  {"ne", 1},  {"cs", 2},  {"hs", 2},  {"cc", 3}, {"lo", 3},
            {"mi", 4},  {"pl", 5},  {"vs", 6},  {"vc", 7},  {"hi", 8}, {"ls", 9},
            {"ge", 10}, {"lt", 11}, {"gt", 12}, {"le", 13}, {"al", 14}};
        auto it = cond.find(op.substr(2));
        if (it == cond.end())
            fail("Unknown branch condition");
        auto imm = relative(target(a[0]), pc, 19, 4);
        return 0x54000000u | ((uint32_t(imm) & 0x7ffffu) << 5) | it->second;
    }
    if (op == "cbz" || op == "cbnz") {
        count(a, 2);
        auto r = reg(a[0]);
        noSp(r);
        auto imm = relative(target(a[1]), pc, 19, 4);
        return sf(r) | 0x34000000u | (op == "cbnz" ? 0x1000000u : 0) |
               ((uint32_t(imm) & 0x7ffffu) << 5) | r.n;
    }
    if (op == "adr" || op == "adrp") {
        count(a, 2);
        auto d = reg(a[0]);
        noSp(d);
        if (!d.wide)
            fail("ADR needs an X register");
        auto imm = op == "adr" ? relative(target(a[1]), pc, 21, 1)
                               : relative(target(a[1]) & ~4095ull, pc & ~4095ull, 21, 4096);
        return (op == "adr" ? 0x10000000u : 0x90000000u) | (uint32_t(imm & 3) << 29) |
               ((uint32_t(imm) & 0x1ffffcu) << 3) | d.n;
    }
    if (op == "movz" || op == "movk" || op == "movn") {
        if (a.size() != 2 && a.size() != 3)
            fail("Expected register, immediate[, LSL #shift]");
        auto d = reg(a[0]);
        noSp(d);
        auto imm = number(a[1]);
        unsigned shift = a.size() == 3 ? shifted(a[2], d.wide ? 48 : 16) : 0;
        if (imm > 65535 || shift % 16)
            fail("Use a 16-bit immediate and a multiple-of-16 shift");
        return sf(d) |
               (op == "movz"   ? 0x52800000u
                : op == "movk" ? 0x72800000u
                               : 0x12800000u) |
               (shift / 16 << 21) | uint32_t(imm << 5) | d.n;
    }
    if (op == "mov") {
        count(a, 2);
        auto d = reg(a[0]);
        if (a[1].starts_with('#') || std::isdigit(static_cast<unsigned char>(a[1][0])) ||
            a[1][0] == '-') {
            noSp(d);
            auto imm = number(a[1]);
            if (!d.wide) {
                auto value = trim(a[1]);
                if (value.starts_with('#'))
                    value = trim(value.substr(1));
                bool negative = value.starts_with('-');
                if ((!negative && imm > 0xffffffffu) ||
                    (negative && static_cast<int64_t>(imm) < std::numeric_limits<int32_t>::min()))
                    fail("Immediate does not fit W register");
            }
            const auto mask = d.wide ? ~uint64_t(0) : uint64_t(0xffffffffu);
            imm &= mask;
            for (bool invert : {false, true}) {
                uint64_t value = invert ? (~imm & mask) : imm;
                for (unsigned shift = 0; shift < (d.wide ? 64u : 32u); shift += 16)
                    if ((value & ~(uint64_t(65535) << shift)) == 0)
                        return sf(d) | (invert ? 0x12800000u : 0x52800000u) | (shift / 16 << 21) |
                               uint32_t(((value >> shift) & 65535) << 5) | d.n;
            }
            fail("MOV immediate needs several instructions; use MOVZ followed by MOVK");
        }
        auto n = reg(a[1]);
        same(d, n);
        if (d.sp || n.sp) {
            noZero(d);
            noZero(n);
            return sf(d) | 0x11000000u | n.n << 5 | d.n;
        }
        return sf(d) | 0x2a0003e0u | n.n << 16 | d.n;
    }
    if (op == "add" || op == "adds" || op == "sub" || op == "subs" || op == "cmp" || op == "cmn") {
        bool compare = op == "cmp" || op == "cmn";
        bool sub = op == "sub" || op == "subs" || op == "cmp";
        bool flags = compare || op == "adds" || op == "subs";
        if (compare) {
            if (a.size() < 2)
                fail("CMP/CMN need two operands");
            auto n = reg(a[0]);
            a.insert(a.begin(), n.wide ? "xzr" : "wzr");
        }
        if (a.size() != 3 && a.size() != 4)
            fail("Expected three operands and optional LSL");
        auto d = reg(a[0]), n = reg(a[1]);
        same(d, n);
        uint32_t base = sf(d) | (sub ? 0x40000000u : 0) | (flags ? 0x20000000u : 0);
        if (a[2].starts_with('#')) {
            if (flags)
                noSp(d);
            else
                noZero(d);
            noZero(n);
            const auto imm = number(a[2]);
            const unsigned shift = a.size() == 4 ? shifted(a[3], 12) : 0;
            if (imm > 4095 || (shift != 0 && shift != 12))
                fail("Immediate must be 0–4095, optionally LSL #12");
            return base | 0x11000000u | (shift ? 1u << 22 : 0) | uint32_t(imm << 10) | n.n << 5 |
                   d.n;
        }
        auto m = reg(a[2]);
        same(d, m);
        noSp(d);
        noSp(n);
        noSp(m);
        unsigned shift = a.size() == 4 ? shifted(a[3], d.wide ? 63 : 31) : 0;
        return base | 0x0b000000u | m.n << 16 | shift << 10 | n.n << 5 | d.n;
    }
    if (op == "and" || op == "ands" || op == "orr" || op == "eor") {
        if (a.size() != 3 && a.size() != 4)
            fail("Expected three registers and optional LSL");
        auto d = reg(a[0]), n = reg(a[1]), m = reg(a[2]);
        same(d, n);
        same(d, m);
        noSp(d);
        noSp(n);
        noSp(m);
        unsigned shift = a.size() == 4 ? shifted(a[3], d.wide ? 63 : 31) : 0;
        uint32_t base = op == "and"    ? 0x0a000000u
                        : op == "ands" ? 0x6a000000u
                        : op == "orr"  ? 0x2a000000u
                                       : 0x4a000000u;
        return sf(d) | base | m.n << 16 | shift << 10 | n.n << 5 | d.n;
    }
    const bool pair = op == "stp" || op == "ldp";
    const bool scalar = op == "ldr" || op == "str" || op == "ldrb" || op == "strb" ||
                        op == "ldrh" || op == "strh" || op == "ldur" || op == "stur";
    if (pair || scalar) {
        const size_t mi = pair ? 2 : 1;
        if (a.size() != mi + 1 && a.size() != mi + 2)
            fail("Expected registers and a memory operand");
        auto d = reg(a[0]);
        noSp(d);
        Reg second{0, false, false, false};
        if (pair) {
            second = reg(a[1]);
            same(d, second);
            noSp(second);
        }
        auto mem = trim(a[mi]);
        bool pre = mem.ends_with('!'), post = a.size() == mi + 2;
        if (pre)
            mem.pop_back();
        if (mem.size() < 3 || mem.front() != '[' || mem.back() != ']' || (pre && post))
            fail("Use [Xn, #offset], [SP, #-16]! or [SP], #16");
        auto inside = split(mem.substr(1, mem.size() - 2));
        if (inside.empty() || inside.size() > 2 || (post && inside.size() != 1))
            fail("Use base + immediate addressing");
        auto n = reg(inside[0]);
        noZero(n);
        if (!n.wide)
            fail("Memory base must be X-sized or SP");
        int64_t offset = post                 ? signedNumber(a.back())
                         : inside.size() == 2 ? signedNumber(inside[1])
                                              : 0;
        bool load = op.starts_with('l');
        // Architecturally constrained-unpredictable writeback must never be assembled silently.
        if ((pre || post) && !n.sp && (n.n == d.n || (pair && n.n == second.n)))
            fail("Writeback base must differ from transfer registers");
        if (pair) {
            if (load && d.n == second.n)
                fail("LDP destination registers must differ");
            const int scale = d.wide ? 8 : 4;
            if (offset % scale || offset / scale < -64 || offset / scale > 63)
                fail("Pair offset must be a scaled signed 7-bit value");
            uint32_t base = d.wide ? 0xa9000000u : 0x29000000u;
            if (pre)
                base += 0x800000u;
            if (post)
                base -= 0x800000u;
            return base | (load ? 1u << 22 : 0) | ((uint32_t(offset / scale) & 127) << 15) |
                   second.n << 10 | n.n << 5 | d.n;
        }
        unsigned size = (op.ends_with('b') ? 0u : op.ends_with('h') ? 1u : d.wide ? 3u : 2u);
        if (size < 2 && d.wide)
            fail("Byte/halfword transfers use W registers");
        const bool unscaled = op == "ldur" || op == "stur";
        if (unscaled && (pre || post))
            fail("LDUR/STUR do not use writeback");
        if (!pre && !post && !unscaled && offset >= 0 && offset % (1 << size) == 0 &&
            (offset >> size) <= 4095)
            return (size << 30) | 0x39000000u | (load ? 1u << 22 : 0) |
                   uint32_t(offset >> size) << 10 | n.n << 5 | d.n;
        if (offset < -256 || offset > 255)
            fail("Unscaled/writeback offset must be −256…255");
        return (size << 30) | 0x38000000u | (load ? 1u << 22 : 0) |
               ((uint32_t(offset) & 511) << 12) |
               (pre    ? 0xc00u
                : post ? 0x400u
                       : 0) |
               n.n << 5 | d.n;
    }
    fail("Unsupported instruction: " + op +
         ". Use supported A64 syntax or .inst for a raw opcode.");
}
} // namespace

Program assemble(const std::string &source, uint64_t codeBase, uint64_t dataBase) {
    if (source.size() > 128 * 1024)
        fail("Source exceeds 128 KiB");
    Program p;
    std::map<std::string, Label> labels;
    std::vector<SourceInstruction> instructions;
    bool dataSection = false;
    std::istringstream lines(source);
    std::string line;
    int lineNumber = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        try {
            auto end = std::min(line.find("//"), line.find(';'));
            auto statement = trim(line.substr(0, end));
            if (statement.empty())
                continue;
            auto colon = statement.find(':');
            if (colon != std::string::npos) {
                auto name = trim(statement.substr(0, colon));
                if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])) ||
                    !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                        return std::isalnum(c) || c == '_' || c == '.';
                    }))
                    fail("Invalid label");
                if (!labels
                         .emplace(name, Label{dataSection, dataSection ? p.data.size()
                                                                       : instructions.size() * 4})
                         .second)
                    fail("Duplicate label: " + name);
                statement = trim(statement.substr(colon + 1));
                if (statement.empty())
                    continue;
            }
            auto space = statement.find_first_of(" \t");
            auto op = lower(statement.substr(0, space));
            auto tail = space == std::string::npos ? "" : trim(statement.substr(space + 1));
            if (op == ".text" || op == ".data" || op == ".bss" || op == ".section") {
                auto section = op == ".section" ? lower(tail) : op;
                if (section != ".text" && section != ".data" && section != ".bss")
                    fail("Supported sections: .text, .data, .bss");
                dataSection = section != ".text";
                continue;
            }
            if (op == ".global" || op == ".globl")
                continue;
            if (dataSection) {
                auto values = split(tail);
                if (values.empty())
                    fail("Data directive needs a value");
                if (op == ".zero" || op == ".space" || op == ".balign" || op == ".align") {
                    count(values, 1);
                    auto n = number(values[0]);
                    if (op == ".align") {
                        if (n > 12)
                            fail("Alignment too large");
                        n = 1ull << n;
                    }
                    if (n > 4096)
                        fail("Data directive exceeds 4096 bytes");
                    if (op == ".balign" || op == ".align") {
                        if (!n || (n & (n - 1)))
                            fail("Alignment must be a power of two");
                        n = (n - p.data.size() % n) % n;
                    }
                    p.data.insert(p.data.end(), n, uint8_t(0));
                } else {
                    int bytes = op == ".quad" || op == "dq"    ? 8
                                : op == ".word" || op == "dd"  ? 4
                                : op == ".hword" || op == "dw" ? 2
                                : op == ".byte" || op == "db"  ? 1
                                                               : 0;
                    if (!bytes)
                        fail("Unsupported data directive: " + op);
                    for (const auto &v : values) {
                        auto n = number(v);
                        if (bytes < 8) {
                            bool negative = !trim(v).empty() && trim(v).front() == '-';
                            if ((!negative && n > ((1ull << (bytes * 8)) - 1)) ||
                                (negative &&
                                 static_cast<int64_t>(n) < -(int64_t(1) << (bytes * 8 - 1))))
                                fail("Data value does not fit");
                        }
                        for (int b = 0; b < bytes; ++b)
                            p.data.push_back(uint8_t(n >> (b * 8)));
                    }
                }
                if (p.data.size() > 4096)
                    fail("Data section exceeds 4096 bytes");
            } else {
                if (op.starts_with('.') && op != ".inst")
                    fail("Unsupported text directive: " + op);
                instructions.push_back({lineNumber, op, tail, statement});
                if (instructions.size() > 4096)
                    fail("Program exceeds 4096 instructions");
            }
        } catch (const std::exception &e) {
            fail("Line " + std::to_string(lineNumber) + ": " + e.what());
        }
    }
    if (instructions.empty())
        fail("No instructions in .text");
    std::map<std::string, uint64_t> addresses;
    for (const auto &[name, label] : labels) {
        auto address = (label.data ? dataBase : codeBase) + label.offset;
        p.symbols.push_back({name, address, label.data});
        addresses[name] = address;
    }
    p.entry = codeBase;
    if (auto start = labels.find("_start"); start != labels.end()) {
        if (start->second.data || start->second.offset >= instructions.size() * 4)
            fail("_start must name an instruction");
        p.entry = codeBase + start->second.offset;
    }
    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto &s = instructions[i];
        try {
            p.instructions.push_back({encode(s, codeBase + i * 4, addresses), s.line, s.original});
        } catch (const std::exception &e) {
            fail("Line " + std::to_string(s.line) + ": " + e.what());
        }
    }
    return p;
}
} // namespace asm101
