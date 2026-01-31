#include "vm.hpp"

#include <memory>
#include <stdexcept>

// #define DEBUG
#include "common/debug.hpp"

namespace ether::vm {

VM::VM(const ir::IRProgram &program) : program_(program) {
    m_globals.resize(program.num_globals, Value(0));
    // Initial coroutine for main
    auto main_coro = std::make_unique<Coroutine>();
    main_coro->id = 0;  // Main is always ID 0
    main_coro->ip = program.main_addr;
    main_coro->call_stack.push_back({0, 0, 0, 0});

    // Pre-allocate slots for main
    auto main_it = program.functions.find("main");
    if (main_it != program.functions.end()) {
        main_coro->stack.resize(main_it->second.num_slots);
    }
    // Reserve large capacity to prevent pointer invalidation
    main_coro->stack.reserve(65536);

    m_coroutines.push_back(std::move(main_coro));

    if (io_uring_queue_init(32, &m_ring, 0) < 0) {
        throw std::runtime_error("Failed to initialize io_uring");
    }
}

VM::~VM() { io_uring_queue_exit(&m_ring); }

Value VM::run(bool collect_stats) {
    const auto &program = program_;
    const uint8_t *code = program.bytecode.data();
    Value main_result(0);

    while (!m_coroutines.empty()) {
        m_current_coro %= m_coroutines.size();
        debug_msg("Current coroutine: " << *m_coroutines[m_current_coro]);

        if (m_coroutines[m_current_coro]->finished) {
            uint32_t finished_id = m_coroutines[m_current_coro]->id;
            Value res = m_coroutines[m_current_coro]->result;

            // Push result to anyone waiting for this ID (optimization)
            bool found_waiter = false;
            for (auto &coro : m_coroutines) {
                if (coro->waiting_for_id == (int32_t)finished_id) {
                    coro->stack.push_back(res);
                    coro->waiting_for_id = -1;
                    found_waiter = true;
                }
            }

            if (!found_waiter) {
                m_finished_coros[finished_id] = res;
            }

            m_coroutines.erase(m_coroutines.begin() + m_current_coro);
            if (m_coroutines.empty()) break;
            continue;
        }

        if (m_coroutines[m_current_coro]->waiting_for_id != -1) {
            uint32_t target_id = (uint32_t)m_coroutines[m_current_coro]->waiting_for_id;
            if (m_finished_coros.contains(target_id)) {
                m_coroutines[m_current_coro]->stack.push_back(m_finished_coros[target_id]);
                m_coroutines[m_current_coro]->waiting_for_id = -1;
                m_finished_coros.erase(target_id);
            } else {
                m_current_coro++;
                continue;
            }
        }

        if (m_coroutines[m_current_coro]->waiting_for_io) {
            handle_io_completion();
        }

        if (m_coroutines[m_current_coro]->finished) {
            continue;
        }

        if (m_coroutines[m_current_coro]->waiting_for_io) {
            bool can_progress = false;
            for (const auto &coro : m_coroutines) {
                if (coro->finished) {
                    can_progress = true;
                    break;
                }
                if (coro->waiting_for_id != -1) {
                    if (m_finished_coros.contains(coro->waiting_for_id)) {
                        can_progress = true;
                        break;
                    }
                } else if (!coro->waiting_for_io) {
                    can_progress = true;
                    break;
                }
            }

            if (!can_progress) {
                struct io_uring_cqe *cqe;
                io_uring_wait_cqe(&m_ring, &cqe);
                handle_io_completion();
            }

            if (m_coroutines[m_current_coro]->finished || m_coroutines[m_current_coro]->waiting_for_io) {
                if (m_coroutines[m_current_coro]->waiting_for_io) {
                    m_current_coro++;
                }
                continue;
            }
        }

#define CUR_CORO()    (*m_coroutines[m_current_coro])
#define READ_BYTE()   code[CUR_CORO().ip++]
#define READ_I64()    (*(int64_t *)&code[(CUR_CORO().ip += 8) - 8])
#define READ_I32()    (*(int32_t *)&code[(CUR_CORO().ip += 4) - 4])
#define READ_I16()    (*(int16_t *)&code[(CUR_CORO().ip += 2) - 2])
#define READ_I8()     ((int8_t)code[CUR_CORO().ip++])
#define READ_UINT32() (*(uint32_t *)&code[(CUR_CORO().ip += 4) - 4])
#define READ_UINT16() (*(uint16_t *)&code[(CUR_CORO().ip += 2) - 2])
#define READ_F64()    (*(double *)&code[(CUR_CORO().ip += 8) - 8])
#define READ_F32()    (*(float *)&code[(CUR_CORO().ip += 4) - 4])

        // Execute instructions until yield or termination
        bool yielded = false;
        while (!yielded) {
            if (CUR_CORO().ip == 0xFFFFFFFF) {
                submit_syscall(CUR_CORO(), CUR_CORO().call_stack.back().num_args_passed);
                yielded = true;
                break;
            }

            if (CUR_CORO().ip >= program.bytecode.size()) break;

            uint8_t op_byte = READ_BYTE();
            ir::OpCode op = static_cast<ir::OpCode>(op_byte);

            decltype(std::chrono::high_resolution_clock::now()) start;
            if (collect_stats) {
                start = std::chrono::high_resolution_clock::now();
            }

            switch (op) {
                case ir::OpCode::PUSH_I64: {
                    push(Value(READ_I64()));
                    break;
                }
                case ir::OpCode::PUSH_I32: {
                    push(Value(READ_I32()));
                    break;
                }
                case ir::OpCode::PUSH_I16: {
                    push(Value(READ_I16()));
                    break;
                }
                case ir::OpCode::PUSH_I8: {
                    push(Value(READ_I8()));
                    break;
                }
                case ir::OpCode::PUSH_F64: {
                    push(Value(READ_F64()));
                    break;
                }
                case ir::OpCode::PUSH_F32: {
                    push(Value(READ_F32()));
                    break;
                }

                case ir::OpCode::PUSH_STR: {
                    uint32_t string_id = READ_UINT32();
                    push(Value(std::string_view(program.string_pool[string_id])));
                    break;
                }

                case ir::OpCode::STORE_VAR: {
                    uint16_t slot = READ_UINT16();
                    uint8_t size = READ_BYTE();
                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;
                    size_t base = call_stack.back().stack_base + slot;

                    // Store in reverse order because we are popping from stack
                    for (int i = size - 1; i >= 0; --i) {
                        stack[base + i] = stack.back();
                        stack.pop_back();
                    }
                    break;
                }

                case ir::OpCode::LOAD_GLOBAL: {
                    uint16_t slot = READ_UINT16();
                    uint8_t size = READ_BYTE();
                    for (uint8_t i = 0; i < size; ++i) {
                        push(m_globals[slot + i]);
                    }
                    break;
                }

                case ir::OpCode::STORE_GLOBAL: {
                    uint16_t slot = READ_UINT16();
                    uint8_t size = READ_BYTE();
                    // Store in reverse order
                    for (int i = size - 1; i >= 0; --i) {
                        m_globals[slot + i] = pop();
                    }
                    break;
                }

                case ir::OpCode::LOAD_VAR: {
                    uint16_t slot = READ_UINT16();
                    uint8_t size = READ_BYTE();
                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;
                    size_t base = call_stack.back().stack_base + slot;
                    for (uint8_t i = 0; i < size; ++i) {
                        stack.push_back(stack[base + i]);
                    }
                    break;
                }

                case ir::OpCode::ADD: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a + b));
                    break;
                }

                case ir::OpCode::SUB: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a - b));
                    break;
                }

                case ir::OpCode::MUL: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a * b));
                    break;
                }

                case ir::OpCode::DIV: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a / b));
                    break;
                }

                case ir::OpCode::ADD_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a + b));
                    break;
                }

                case ir::OpCode::SUB_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a - b));
                    break;
                }

                case ir::OpCode::MUL_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a * b));
                    break;
                }

                case ir::OpCode::DIV_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a / b));
                    break;
                }

                case ir::OpCode::SYSCALL: {
                    uint8_t ir_num_args = READ_BYTE();
                    uint8_t num_args_passed = ir_num_args;
                    if (ir_num_args & 0x80) {
                        uint8_t fixed = (ir_num_args & 0x7F) - 1;
                        auto &frame = CUR_CORO().call_stack.back();
                        uint8_t num_varargs = frame.num_args_passed - frame.num_fixed_params;
                        num_args_passed = fixed + num_varargs;
                    }
                    // std::cout << "DEBUG: SYSCALL " << (int)num_args_passed << " (ir=" << (int)ir_num_args << ")" <<
                    // std::endl;
                    auto &coro = CUR_CORO();
                    submit_syscall(coro, num_args_passed);
                    if (coro.waiting_for_io) {
                        yielded = true;
                    }
                    break;
                }

                case ir::OpCode::CALL: {
                    uint32_t target_addr = READ_UINT32();
                    uint8_t ir_num_args = READ_BYTE();
                    uint8_t num_args_passed = ir_num_args;
                    if (ir_num_args & 0x80) {
                        uint8_t fixed = (ir_num_args & 0x7F) - 1;
                        auto &frame = CUR_CORO().call_stack.back();
                        uint8_t num_varargs = frame.num_args_passed - frame.num_fixed_params;
                        num_args_passed = fixed + num_varargs;
                    }

                    const auto &info = program.addr_to_info.at(target_addr);
                    uint8_t num_params = info.num_params;
                    uint8_t num_slots = info.num_slots;

                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;
                    size_t base = stack.size() - num_args_passed;
                    call_stack.push_back({CUR_CORO().ip, base, num_params, num_args_passed});

                    if (num_slots > num_args_passed) {
                        stack.resize(base + num_slots);
                    }
                    CUR_CORO().ip = target_addr;
                    break;
                }

                case ir::OpCode::RET: {
                    uint8_t size = READ_BYTE();
                    auto &stack = CUR_CORO().stack;
                    auto &call_stack = CUR_CORO().call_stack;

                    // Extract return values
                    std::vector<Value> results;
                    if (size > 0) {
                        results.resize(size);
                        // Pop in reverse order
                        for (int i = size - 1; i >= 0; --i) {
                            results[i] = stack.back();
                            stack.pop_back();
                        }
                    } else {
                        // Default return value if size is 0?
                        // The original code popped back anyway.
                        // Assuming RET always returns something or nothing if size 0.
                        // Actually void functions might return 0 size.
                        // Original code: Value res = stack.back(); stack.pop_back(); (This implies 1 value always)

                        // If size is 0, we don't return anything.
                        // But wait, the original code always returned a value (even for void?).
                        // If the function returns void, IR gen emits PUSH 0; RET. So size 1.
                        // So usually size >= 1.
                    }

                    // Value res = stack.back();
                    // stack.pop_back();

                    size_t ret_addr = call_stack.back().return_addr;
                    size_t stack_base = call_stack.back().stack_base;
                    call_stack.pop_back();

                    if (call_stack.empty()) {
                        if (m_current_coro == 0 && !results.empty())
                            main_result = results.back();  // Or first? Main usually returns 1 value.
                        if (!results.empty()) CUR_CORO().result = results.back();  // Coroutine result usually 1 value?
                        CUR_CORO().finished = true;
                        yielded = true;
                    } else {
                        stack.resize(stack_base);  // Restore stack frame
                        CUR_CORO().ip = ret_addr;
                        // Push results back
                        for (const auto &val : results) {
                            stack.push_back(val);
                        }
                    }
                    break;
                }

                case ir::OpCode::JMP: {
                    CUR_CORO().ip = READ_UINT32();
                    break;
                }

                case ir::OpCode::JZ: {
                    int64_t condition = pop().i64_value();
                    uint32_t target = READ_UINT32();
                    if (condition == 0) {
                        CUR_CORO().ip = target;
                    }
                    break;
                }

                case ir::OpCode::CMP_EQ: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a == b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_LE: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a <= b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_LT: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a < b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_GT: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a > b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_GE: {
                    int64_t b = pop().i64_value();
                    int64_t a = pop().i64_value();
                    push(Value(a >= b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_EQ_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a == b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_LE_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a <= b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_LT_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a < b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_GT_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a > b ? 1 : 0));
                    break;
                }

                case ir::OpCode::CMP_GE_F: {
                    double b = pop().f64_value();
                    double a = pop().f64_value();
                    push(Value(a >= b ? 1 : 0));
                    break;
                }

                case ir::OpCode::SPAWN: {
                    uint32_t target_addr = READ_UINT32();
                    uint8_t ir_num_args = READ_BYTE();
                    uint8_t num_args_passed = ir_num_args;
                    if (ir_num_args & 0x80) {
                        uint8_t fixed = (ir_num_args & 0x7F) - 1;
                        auto &frame = CUR_CORO().call_stack.back();
                        uint8_t num_varargs = frame.num_args_passed - frame.num_fixed_params;
                        num_args_passed = fixed + num_varargs;
                    }
                    uint8_t num_params;
                    uint8_t num_slots;

                    if (target_addr == 0xFFFFFFFF) {
                        num_params = num_args_passed;
                        num_slots = num_args_passed;
                    } else {
                        const auto &info = program.addr_to_info.at(target_addr);
                        num_params = info.num_params;
                        num_slots = info.num_slots;
                    }

                    auto new_coro = std::make_unique<Coroutine>();
                    uint32_t new_id = m_next_coro_id++;
                    new_coro->id = new_id;
                    new_coro->ip = target_addr;
                    new_coro->call_stack.push_back({0, 0, num_params, num_args_passed});

                    new_coro->stack.resize(num_args_passed);
                    auto &stack = CUR_CORO().stack;
                    for (int i = num_args_passed - 1; i >= 0; --i) {
                        new_coro->stack[i] = stack.back();
                        stack.pop_back();
                    }
                    if (num_slots > num_args_passed) {
                        new_coro->stack.resize(num_slots);
                    }
                    new_coro->stack.reserve(65536);  // Prevent pointer invalidation

                    m_coroutines.push_back(std::move(new_coro));
                    push(Value((int32_t)new_id));
                    m_current_coro = m_coroutines.size() - 1;
                    break;
                }

                case ir::OpCode::LEA_STACK: {
                    uint16_t slot = READ_UINT16();
                    size_t base = CUR_CORO().call_stack.back().stack_base;
                    // Taking address of stack element
                    // WARNING: Unsafe if stack reallocates. We reserve 64k to mitigate.
                    Value *ptr = &CUR_CORO().stack[base + slot];
                    push(Value(ptr));
                    break;
                }

                case ir::OpCode::LEA_GLOBAL: {
                    uint16_t slot = READ_UINT16();
                    Value *ptr = &m_globals[slot];
                    push(Value(ptr));
                    break;
                }

                case ir::OpCode::YIELD: {
                    yielded = true;
                    break;
                }

                case ir::OpCode::AWAIT: {
                    int32_t target_id = (int32_t)pop().i64_value();
                    if (m_finished_coros.contains((uint32_t)target_id)) {
                        push(m_finished_coros[(uint32_t)target_id]);
                        m_finished_coros.erase((uint32_t)target_id);
                    } else {
                        CUR_CORO().waiting_for_id = target_id;
                        yielded = true;
                    }
                    break;
                }

                case ir::OpCode::LOAD_PTR_OFFSET: {
                    int32_t offset = READ_I32();
                    uint8_t size = READ_BYTE();
                    Value ptr_val = pop();
                    void *ptr_addr;

                    // Handle both Ptr and I64 types (for pointer arithmetic)
                    if (ptr_val.type == ValueType::Ptr) {
                        ptr_addr = ptr_val.as.ptr;
                    } else {
                        // Treat i64 as pointer address
                        ptr_addr = (void *)(intptr_t)ptr_val.i64_value();
                    }

                    if (!ptr_addr) throw std::runtime_error("Null pointer dereference");
                    Value *ptr = (Value *)ptr_addr;
                    for (uint8_t i = 0; i < size; ++i) {
                        push(ptr[offset + i]);
                    }
                    break;
                }

                case ir::OpCode::STORE_PTR_OFFSET: {
                    int32_t offset = READ_I32();
                    uint8_t size = READ_BYTE();
                    Value ptr_val = pop();  // Pointer is popped AFTER values?
                    // Wait, standard convention is: push value, push pointer, STORE.
                    // But STORE_PTR_OFFSET popped ptr then val.
                    // If multi-slot, we pushed field1, field2. Then Pointer.
                    // Stack: [f1, f2, ptr].
                    // Pop ptr.
                    // Pop f2. Store at offset+1.
                    // Pop f1. Store at offset+0.

                    void *ptr_addr;

                    // Handle both Ptr and I64 types (for pointer arithmetic)
                    if (ptr_val.type == ValueType::Ptr) {
                        ptr_addr = ptr_val.as.ptr;
                    } else {
                        // Treat i64 as pointer address
                        ptr_addr = (void *)(intptr_t)ptr_val.i64_value();
                    }

                    if (!ptr_addr) throw std::runtime_error("Null pointer dereference");
                    Value *ptr = (Value *)ptr_addr;

                    for (int i = size - 1; i >= 0; --i) {
                        ptr[offset + i] = pop();
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

                case ir::OpCode::PUSH_VARARGS: {
                    auto &frame = CUR_CORO().call_stack.back();
                    auto &stack = CUR_CORO().stack;
                    uint8_t num_varargs = frame.num_args_passed - frame.num_fixed_params;
                    for (uint8_t i = 0; i < num_varargs; ++i) {
                        stack.push_back(stack[frame.stack_base + frame.num_fixed_params + i]);
                    }
                    break;
                }

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
