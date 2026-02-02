#include "ir/disassembler.hpp"

#include <iomanip>
#include <iostream>
#include <unordered_map>

#include "common/iter.hpp"

namespace ether::ir {

static void printLiteral(const std::string_view &texto) {
    std::cout << "\"";
    for (char c : texto) {
        switch (c) {
            case '\n':
                std::cout << "\\n";
                break;
            case '\t':
                std::cout << "\\t";
                break;
            case '\r':
                std::cout << "\\r";
                break;
            case '\\':
                std::cout << "\\\\";
                break;
            default:
                std::cout << c;
                break;
        }
    }
    std::cout << "\"";
}

void disassemble(const IRProgram &program) {
    std::cout << "Bytecode Size: " << program.bytecode.size() << " bytes" << std::endl;
    std::cout << "String Pool Size: " << program.string_pool.size() << " entries" << std::endl;
    std::cout << "Functions:" << std::endl;
    for_each_sorted(
        program.functions, [](const auto &a, const auto &b) { return a.second.entry_addr > b.second.entry_addr; },
        [&](const auto &pair) {
            const auto &[name, info] = pair;
            std::cout << "  " << std::left << std::setw(20) << name << " @ " << std::left << std::setw(10)
                      << info.entry_addr << " (Params: " << std::left << std::setw(2) << (int)info.num_params
                      << ", Slots: " << std::left << std::setw(2) << (int)info.num_slots << ")" << std::endl;
        });
    std::cout << "\nBytecode Disassembly:" << std::endl;
    const uint8_t *code = program.bytecode.data();
    size_t ip = 0;

    // Create a mapping from address to function name/info for display
    std::unordered_map<size_t, std::pair<std::string, IRProgram::FunctionInfo>> addr_to_func;
    for (const auto &[name, info] : program.functions) {
        addr_to_func[info.entry_addr] = {name, info};
    }

    while (ip < program.bytecode.size()) {
        size_t addr = ip;

        // Check if this address is a function entry point
        if (addr_to_func.contains(addr)) {
            const auto &[name, info] = addr_to_func.at(addr);
            std::cout << "\n<function: " << name << "> (params: " << (int)info.num_params
                      << ", slots: " << (int)info.num_slots << ")" << std::endl;
        }

        uint8_t op_byte = code[ip++];
        OpCode op = static_cast<OpCode>(op_byte);

        std::cout << std::right << std::setw(4) << addr << ": " << std::left << std::setw(20) << op;

        switch (op) {
            case OpCode::PUSH_I64: {
                int64_t val = *(int64_t *)&code[ip];
                ip += 8;
                std::cout << val;
                break;
            }
            case OpCode::PUSH_I32: {
                int32_t val = *(int32_t *)&code[ip];
                ip += 4;
                std::cout << val;
                break;
            }
            case OpCode::PUSH_I16: {
                int16_t val = *(int16_t *)&code[ip];
                ip += 2;
                std::cout << val;
                break;
            }
            case OpCode::PUSH_I8: {
                int8_t val = (int8_t)code[ip++];
                std::cout << (int)val;
                break;
            }
            case OpCode::PUSH_F64: {
                double val = *(double *)&code[ip];
                ip += 8;
                std::cout << val;
                break;
            }
            case OpCode::PUSH_F32: {
                float val = *(float *)&code[ip];
                ip += 4;
                std::cout << val;
                break;
            }
            case OpCode::PUSH_STR: {
                uint32_t id = *(uint32_t *)&code[ip];
                ip += 4;
                printLiteral(program.string_pool[id]);
                break;
            }
            case OpCode::STR_GET:
            case OpCode::STR_SET: {
                break;
            }
            case OpCode::ARR_ALLOC: {
                uint32_t slots = *(uint32_t *)&code[ip];
                ip += 4;
                std::cout << "slots " << slots;
                break;
            }
            case OpCode::STORE_VAR:
            case OpCode::LOAD_VAR: {
                uint16_t slot = *(uint16_t *)&code[ip];
                ip += 2;
                uint8_t size = code[ip++];
                std::cout << "slot " << (int)slot << " size " << (int)size;
                break;
            }
            case OpCode::STORE_GLOBAL:
            case OpCode::LOAD_GLOBAL: {
                uint16_t slot = *(uint16_t *)&code[ip];
                ip += 2;
                uint8_t size = code[ip++];
                std::cout << "global_slot " << (int)slot << " size " << (int)size;
                break;
            }
            case OpCode::LEA_STACK: {
                uint16_t slot = *(uint16_t *)&code[ip];
                ip += 2;
                std::cout << "slot " << (int)slot;
                break;
            }
            case OpCode::LEA_GLOBAL: {
                uint16_t slot = *(uint16_t *)&code[ip];
                ip += 2;
                std::cout << "global_slot " << (int)slot;
                break;
            }
            case OpCode::LOAD_PTR_OFFSET:
            case OpCode::STORE_PTR_OFFSET: {
                uint32_t offset = *(uint32_t *)&code[ip];
                ip += 4;
                uint8_t size = code[ip++];
                std::cout << "offset " << (int)offset << " size " << (int)size;
                break;
            }
            case OpCode::SYSCALL: {
                uint8_t num_args = code[ip++];
                std::cout << "args ";
                if (num_args & 0x80) {
                    std::cout << (num_args & 0x7F) << " (variadic)";
                } else {
                    std::cout << (int)num_args;
                }
                break;
            }
            case OpCode::RET: {
                uint8_t size = code[ip++];
                std::cout << "size " << (int)size;
                break;
            }
            case OpCode::CALL:
            case OpCode::SPAWN: {
                uint32_t target = *(uint32_t *)&code[ip];
                ip += 4;
                uint8_t num_args = code[ip++];
                std::cout << "addr " << target << " args ";
                if (num_args & 0x80) {
                    std::cout << (num_args & 0x7F) << " (variadic)";
                } else {
                    std::cout << (int)num_args;
                }
                if (addr_to_func.contains(target)) {
                    std::cout << " <" << addr_to_func.at(target).first << ">";
                }
                break;
            }
            case OpCode::YIELD:
            case OpCode::AWAIT:
            case OpCode::PUSH_VARARGS: {
                break;
            }
            case OpCode::JMP:
            case OpCode::JZ: {
                uint32_t target = *(uint32_t *)&code[ip];
                ip += 4;
                std::cout << "addr " << target;
                break;
            }
            default:
                break;
        }
        std::cout << std::endl;
    }
}

}  // namespace ether::ir
