#include "vm.hpp"

#include <unistd.h>

#include <iostream>
#include <stdexcept>
#include <string>
// #define DEBUG
#include "common/debug.hpp"
#include "common/defer.hpp"

namespace ether::vm {

VM::VM(const ir::IRProgram &program) : m_program(program) {
    m_call_stack.reserve(100);
    m_stack.reserve(1024);
}

void VM::push(Value val) { m_stack.push_back(val); }

Value VM::pop() {
    if (m_stack.empty()) {
        throw std::runtime_error("Stack underflow");
    }
    Value val = m_stack.back();
    m_stack.pop_back();
    return val;
}

Value VM::run(bool collect_stats) {
    const auto &program = m_program;
    std::string main_name = "main";
    if (!program.entry_points.contains(main_name)) {
        throw std::runtime_error("No 'main' function found");
    }
    m_ip = program.entry_points.at(main_name);
    m_call_stack.push_back({0, {}});  // Base frame (return to HALT)
    int64_t count = 0;
    defer(debug_msg("Instructions executed: " << count););

    while (m_ip < program.instructions.size()) {
        const auto &ins = program.instructions[m_ip++];
        count++;
        decltype(std::chrono::high_resolution_clock::now()) start;
        if (collect_stats) {
            start = std::chrono::high_resolution_clock::now();
        }

        switch (ins.op) {
            case ir::OpCode::PUSH_INT: {
                int val = std::get<int>(ins.operand);
                debug_msg("PUSH_INT: " << val);
                push(val);
                break;
            }

            case ir::OpCode::PUSH_STR: {
                std::string_view val = std::get<std::string>(ins.operand);
                debug_msg("PUSH_STR: " << val);
                push(val);
                break;
            }

            case ir::OpCode::STORE_VAR: {
                std::string_view name = std::get<std::string>(ins.operand);
                auto var = pop();
                debug_msg("STORE_VAR: " << name << " = " << var);
                m_call_stack.back().locals[name] = var;
                break;
            }

            case ir::OpCode::LOAD_VAR: {
                std::string_view name = std::get<std::string>(ins.operand);
                auto var = m_call_stack.back().locals.at(name);
                debug_msg("LOAD_VAR: " << name << " = " << var);
                push(var);
                break;
            }

            case ir::OpCode::ADD: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("ADD: " << a << " + " << b << " = " << a + b);
                push(a + b);
                break;
            }

            case ir::OpCode::SUB: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("SUB: " << a << " - " << b << " = " << a - b);
                push(a - b);
                break;
            }

            case ir::OpCode::MUL: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("MUL: " << a << " * " << b << " = " << a * b);
                push(a * b);
                break;
            }

            case ir::OpCode::DIV: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("DIV: " << a << " / " << b << " = " << a / b);
                push(a / b);
                break;
            }

            case ir::OpCode::SYS_WRITE: {
                Value val = pop();
                int fd = std::get<int>(pop());
                std::string s;
                if (std::holds_alternative<int>(val)) {
                    s = std::to_string(std::get<int>(val));
                } else {
                    s = std::get<std::string_view>(val);
                }
                debug_msg("SYS_WRITE: " << fd << " " << s);
                if (fd == 1) {
                    int written = write(1, s.data(), s.size());
                    push(written);
                } else {
                    push(-1);
                }
                break;
            }

            case ir::OpCode::CALL: {
                std::string_view name = std::get<std::string>(ins.operand);
                debug_msg("CALL: " << name);
                if (program.entry_points.contains(std::string(name))) {
                    m_call_stack.push_back({m_ip, {}});
                    m_ip = program.entry_points.at(std::string(name));
                } else {
                    throw std::runtime_error("Unknown function: " + std::string(name));
                }
                break;
            }

            case ir::OpCode::RET: {
                Value val = pop();
                debug_msg("RET: " << val);
                size_t ret_addr = m_call_stack.back().return_addr;
                m_call_stack.pop_back();
                if (m_call_stack.empty()) {
                    return val;
                }
                m_ip = ret_addr;
                push(val);
                break;
            }

            case ir::OpCode::LABEL:
                break;

            case ir::OpCode::JMP: {
                std::string_view name = std::get<std::string>(ins.operand);
                debug_msg("JMP: " << name);
                m_ip = program.entry_points.at(std::string(name));
                break;
            }

            case ir::OpCode::JZ: {
                int val = std::get<int>(pop());
                std::string_view name = std::get<std::string>(ins.operand);
                debug_msg("JZ: " << val << " " << name);
                if (val == 0) {
                    m_ip = program.entry_points.at(std::string(name));
                }
                break;
            }

            case ir::OpCode::CMP_EQ: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("CMP_EQ: " << a << " == " << b << " = " << (a == b ? 1 : 0));
                push(a == b ? 1 : 0);
                break;
            }

            case ir::OpCode::CMP_LE: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("CMP_LE: " << a << " <= " << b << " = " << (a <= b ? 1 : 0));
                push(a <= b ? 1 : 0);
                break;
            }

            case ir::OpCode::CMP_LT: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("CMP_LT: " << a << " < " << b << " = " << (a < b ? 1 : 0));
                push(a < b ? 1 : 0);
                break;
            }

            case ir::OpCode::CMP_GT: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("CMP_GT: " << a << " > " << b << " = " << (a > b ? 1 : 0));
                push(a > b ? 1 : 0);
                break;
            }

            case ir::OpCode::CMP_GE: {
                int b = std::get<int>(pop());
                int a = std::get<int>(pop());
                debug_msg("CMP_GE: " << a << " >= " << b << " = " << (a >= b ? 1 : 0));
                push(a >= b ? 1 : 0);
                break;
            }

            case ir::OpCode::HALT: {
                debug_msg("HALT");
                return pop();
            }

            default: {
                debug_msg("Unknown opcode: " << ins.op);
                throw std::runtime_error("Unknown opcode");
            }
        }

        if (collect_stats) {
            auto end = std::chrono::high_resolution_clock::now();
            auto &s = m_stats[ins.op];
            s.count++;
            s.total_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        }
    }

    return 0;
}

}  // namespace ether::vm
