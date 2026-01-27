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
    // Initial coroutine for main
    Coroutine main_coro;
    main_coro.id = 0;  // Main is always ID 0
    main_coro.ip = program.main_addr;
    main_coro.call_stack.push_back({0, 0});

    // Pre-allocate slots for main
    auto main_it = program.functions.find("main");
    if (main_it != program.functions.end()) {
        main_coro.stack.resize(main_it->second.num_slots);
    }

    m_coroutines.push_back(std::move(main_coro));
}

Value VM::run(bool collect_stats) {
    const auto &program = program_;
    const uint8_t *code = program.bytecode.data();
    Value main_result(0);

    while (!m_coroutines.empty()) {
        m_current_coro %= m_coroutines.size();

        if (m_coroutines[m_current_coro].finished) {
            m_finished_coros[m_coroutines[m_current_coro].id] = m_coroutines[m_current_coro].result;
            m_coroutines.erase(m_coroutines.begin() + m_current_coro);
            if (m_coroutines.empty()) break;
            continue;
        }

        if (m_coroutines[m_current_coro].waiting_for_id != -1) {
            uint32_t target_id = (uint32_t)m_coroutines[m_current_coro].waiting_for_id;
            if (m_finished_coros.contains(target_id)) {
                m_coroutines[m_current_coro].stack.push_back(m_finished_coros[target_id]);
                m_coroutines[m_current_coro].waiting_for_id = -1;
            } else {
                m_current_coro++;
                continue;
            }
        }

#define CUR_CORO()    m_coroutines[m_current_coro]
#define READ_BYTE()   code[CUR_CORO().ip++]
#define READ_INT()    (*(int32_t *)&code[(CUR_CORO().ip += 4) - 4])
#define READ_UINT32() (*(uint32_t *)&code[(CUR_CORO().ip += 4) - 4])

        // Execute instructions until yield or termination
        bool yielded = false;
        while (!yielded && CUR_CORO().ip < program.bytecode.size()) {
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
                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;
                    stack[call_stack.back().stack_base + slot] = stack.back();
                    stack.pop_back();
                    break;
                }

                case ir::OpCode::LOAD_VAR: {
                    uint8_t slot = READ_BYTE();
                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;
                    stack.push_back(stack[call_stack.back().stack_base + slot]);
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

                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;
                    size_t base = stack.size() - num_params;
                    call_stack.push_back({CUR_CORO().ip, base});
                    if (num_slots > num_params) {
                        stack.resize(base + num_slots);
                    }
                    CUR_CORO().ip = target_addr;
                    break;
                }

                case ir::OpCode::RET: {
                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;
                    Value res = stack.back();
                    stack.pop_back();
                    size_t ret_addr = call_stack.back().return_addr;
                    size_t stack_base = call_stack.back().stack_base;
                    call_stack.pop_back();

                    if (call_stack.empty()) {
                        if (m_current_coro == 0) main_result = res;
                        CUR_CORO().result = res;
                        CUR_CORO().finished = true;
                        yielded = true;
                    } else {
                        stack.resize(stack_base);
                        CUR_CORO().ip = ret_addr;
                        stack.push_back(res);
                    }
                    break;
                }

                case ir::OpCode::JMP: {
                    CUR_CORO().ip = READ_UINT32();
                    break;
                }

                case ir::OpCode::JZ: {
                    int32_t condition = pop().as.i32;
                    uint32_t target = READ_UINT32();
                    if (condition == 0) {
                        CUR_CORO().ip = target;
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

                case ir::OpCode::SYS_PRINTF: {
                    uint8_t num_args = READ_BYTE();
                    std::vector<Value> args(num_args);
                    for (int i = num_args - 1; i >= 0; --i) {
                        args[i] = pop();
                    }

                    if (args.empty() || args[0].type != ValueType::String) {
                        throw std::runtime_error("printf requires at least a format string argument");
                    }

                    std::string_view fmt = args[0].as_string();
                    size_t arg_idx = 1;
                    for (size_t i = 0; i < fmt.size(); ++i) {
                        if (fmt[i] == '%' && i + 1 < fmt.size()) {
                            i++;
                            if (fmt[i] == 'd') {
                                if (arg_idx < args.size() && args[arg_idx].type == ValueType::Int) {
                                    std::cout << args[arg_idx++].as.i32;
                                } else {
                                    std::cout << "%d";
                                }
                            } else if (fmt[i] == 's') {
                                if (arg_idx < args.size() && args[arg_idx].type == ValueType::String) {
                                    std::cout << args[arg_idx++].as_string();
                                } else {
                                    std::cout << "%s";
                                }
                            } else {
                                std::cout << '%' << fmt[i];
                            }
                        } else if (fmt[i] == '\\' && i + 1 < fmt.size()) {
                            i++;
                            if (fmt[i] == 'n')
                                std::cout << '\n';
                            else if (fmt[i] == 't')
                                std::cout << '\t';
                            else
                                std::cout << '\\' << fmt[i];
                        } else {
                            std::cout << fmt[i];
                        }
                    }
                    push(Value(0));
                    break;
                }

                case ir::OpCode::SPAWN: {
                    uint32_t target_addr = READ_UINT32();
                    const auto &info = program.addr_to_info.at(target_addr);
                    uint8_t num_params = info.num_params;
                    uint8_t num_slots = info.num_slots;

                    Coroutine new_coro;
                    uint32_t new_id = m_next_coro_id++;
                    new_coro.id = new_id;
                    new_coro.ip = target_addr;
                    new_coro.call_stack.push_back({0, 0});

                    new_coro.stack.resize(num_params);
                    auto &stack = CUR_CORO().stack;
                    for (int i = num_params - 1; i >= 0; --i) {
                        new_coro.stack[i] = stack.back();
                        stack.pop_back();
                    }
                    if (num_slots > num_params) {
                        new_coro.stack.resize(num_slots);
                    }

                    m_coroutines.push_back(std::move(new_coro));
                    push(Value((int32_t)new_id));
                    break;
                }

                case ir::OpCode::YIELD: {
                    yielded = true;
                    break;
                }

                case ir::OpCode::AWAIT: {
                    int32_t target_id = pop().as.i32;
                    if (m_finished_coros.contains((uint32_t)target_id)) {
                        push(m_finished_coros[(uint32_t)target_id]);
                    } else {
                        CUR_CORO().waiting_for_id = target_id;
                        yielded = true;
                    }
                    break;
                }

                case ir::OpCode::POP: {
                    pop();
                    break;
                }

                case ir::OpCode::HALT:
                    if (m_current_coro == 0) main_result = CUR_CORO().stack.back();
                    CUR_CORO().result = CUR_CORO().stack.back();
                    CUR_CORO().finished = true;
                    yielded = true;
                    break;

                default:
                    throw std::runtime_error("Unsupported opcode");
            }

            if (collect_stats) {
                auto end = std::chrono::high_resolution_clock::now();
                auto &s = m_stats[op];
                s.count++;
                s.total_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            }
        }

        if (yielded) {
            m_current_coro++;
        }
    }
    return main_result;
}

}  // namespace ether::vm
