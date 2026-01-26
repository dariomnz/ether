#include "vm.hpp"

#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "common/debug.hpp"
#include "common/defer.hpp"

namespace ether::vm {

VM::VM(const ir::IRProgram &program) : program_(program) {
    m_stack.reserve(1024 * 10);
    m_call_stack.reserve(1000);
}

Value VM::run(bool collect_stats) {
    const auto &program = program_;
    const uint8_t *code = program.bytecode.data();
    m_ip = program.main_addr;

    // Initial frame for main
    m_call_stack.push_back({0, 0});

    // Pre-allocate slots for main if any (IR generator should tell us how many)
    auto main_it = program.functions.find("main");
    if (main_it != program.functions.end()) {
        m_stack.resize(main_it->second.num_slots);
    }

#define READ_BYTE()   code[m_ip++]
#define READ_INT()    (*(int32_t *)&code[(m_ip += 4) - 4])
#define READ_UINT32() (*(uint32_t *)&code[(m_ip += 4) - 4])

    while (m_ip < program.bytecode.size()) {
        uint8_t op_byte = READ_BYTE();
        ir::OpCode op = static_cast<ir::OpCode>(op_byte);

        decltype(std::chrono::high_resolution_clock::now()) start;
        if (collect_stats) {
            start = std::chrono::high_resolution_clock::now();
        }

        switch (op) {
            case ir::OpCode::PUSH_INT: {
                int32_t val = READ_INT();
                push(Value(val));
                break;
            }

            case ir::OpCode::PUSH_STR: {
                uint32_t string_id = READ_UINT32();
                push(Value(std::string_view(program.string_pool[string_id])));
                break;
            }

            case ir::OpCode::STORE_VAR: {
                uint8_t slot = READ_BYTE();
                m_stack[m_call_stack.back().stack_base + slot] = pop();
                break;
            }

            case ir::OpCode::LOAD_VAR: {
                uint8_t slot = READ_BYTE();
                push(m_stack[m_call_stack.back().stack_base + slot]);
                break;
            }

            case ir::OpCode::ADD: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a + b));
                break;
            }

            case ir::OpCode::SUB: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a - b));
                break;
            }

            case ir::OpCode::MUL: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a * b));
                break;
            }

            case ir::OpCode::DIV: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a / b));
                break;
            }

            case ir::OpCode::SYS_WRITE: {
                Value val = pop();
                int32_t fd = pop().as.i32;
                std::string s;
                if (val.type == ValueType::Int) {
                    s = std::to_string(val.as.i32);
                } else {
                    s = val.as_string();
                }
                if (fd == 1) {
                    write(1, s.data(), s.size());
                    push(Value((int32_t)s.size()));
                } else {
                    push(Value(-1));
                }
                break;
            }

            case ir::OpCode::CALL: {
                uint32_t target_addr = READ_UINT32();
                const auto &info = program.addr_to_info.at(target_addr);
                uint8_t num_params = info.num_params;
                uint8_t num_slots = info.num_slots;

                // Params are already on stack. Base starts at first param.
                size_t base = m_stack.size() - num_params;
                m_call_stack.push_back({m_ip, base});
                // Add remaining slots
                if (num_slots > num_params) {
                    m_stack.resize(base + num_slots);
                }
                m_ip = target_addr;
                break;
            }

            case ir::OpCode::RET: {
                Value res = pop();
                size_t ret_addr = m_call_stack.back().return_addr;
                size_t stack_base = m_call_stack.back().stack_base;
                m_call_stack.pop_back();

                if (m_call_stack.empty()) return res;

                m_stack.resize(stack_base);  // Shrink to where params were
                m_ip = ret_addr;
                push(res);
                break;
            }

            case ir::OpCode::JMP: {
                m_ip = READ_UINT32();
                break;
            }

            case ir::OpCode::JZ: {
                int32_t condition = pop().as.i32;
                uint32_t target = READ_UINT32();
                if (condition == 0) {
                    m_ip = target;
                }
                break;
            }

            case ir::OpCode::CMP_EQ: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a == b ? 1 : 0));
                break;
            }

            case ir::OpCode::CMP_LE: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a <= b ? 1 : 0));
                break;
            }

            case ir::OpCode::CMP_LT: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a < b ? 1 : 0));
                break;
            }

            case ir::OpCode::CMP_GT: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a > b ? 1 : 0));
                break;
            }

            case ir::OpCode::CMP_GE: {
                int32_t b = pop().as.i32;
                int32_t a = pop().as.i32;
                push(Value(a >= b ? 1 : 0));
                break;
            }

            case ir::OpCode::HALT:
                return pop();

            default:
                throw std::runtime_error("Unsupported opcode in new VM");
        }

        if (collect_stats) {
            auto end = std::chrono::high_resolution_clock::now();
            auto &s = m_stats[op];
            s.count++;
            s.total_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        }
    }
    return Value(0);
}

}  // namespace ether::vm
