#include "vm.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

// #define DEBUG
#include "common/debug.hpp"
#include "common/defer.hpp"

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

                case ir::OpCode::LOAD_GLOBAL: {
                    uint16_t slot = READ_UINT16();
                    push(m_globals[slot]);
                    break;
                }

                case ir::OpCode::STORE_GLOBAL: {
                    uint16_t slot = READ_UINT16();
                    m_globals[slot] = pop();
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

                    m_coroutines.push_back(std::move(new_coro));
                    push(Value((int32_t)new_id));
                    m_current_coro = m_coroutines.size() - 1;
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
                    uint8_t offset = READ_BYTE();
                    Value ptr_val = pop();
                    if (!ptr_val.as.ptr) throw std::runtime_error("Null pointer dereference");
                    Value *ptr = (Value *)ptr_val.as.ptr;
                    push(ptr[offset]);
                    break;
                }

                case ir::OpCode::STORE_PTR_OFFSET: {
                    uint8_t offset = READ_BYTE();
                    Value ptr_val = pop();
                    Value val = pop();
                    if (!ptr_val.as.ptr) throw std::runtime_error("Null pointer dereference");
                    Value *ptr = (Value *)ptr_val.as.ptr;
                    ptr[offset] = val;
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

void VM::handle_io_completion() {
    struct io_uring_cqe *cqe;
    while (io_uring_peek_cqe(&m_ring, &cqe) == 0) {
        uint32_t coro_id = (uint32_t)(uintptr_t)io_uring_cqe_get_data(cqe);
        int32_t res = cqe->res;
        io_uring_cqe_seen(&m_ring, cqe);

        for (auto &coro : m_coroutines) {
            if (coro->id == coro_id) {
                coro->stack.push_back(Value(res));
                coro->waiting_for_io = false;
                if (coro->ip == 0xFFFFFFFF) {
                    // It was a spawned native call, mark it as finished
                    coro->result = res;
                    coro->finished = true;
                }
                break;
            }
        }
    }
}

void VM::submit_syscall(Coroutine &coro, uint8_t num_args) {
    auto &stack = coro.stack;
    std::vector<Value> args(num_args);
    for (int i = num_args - 1; i >= 0; --i) {
        args[i] = stack.back();
        stack.pop_back();
    }

    if (args.empty()) {
        coro.stack.push_back(Value(-1));
        return;
    }

    int64_t id = args[0].i64_value();

    switch (id) {
        case 10: {  // PRINTF
            if (args.size() < 2 || args[1].type != ValueType::String) {
                throw std::runtime_error("printf requires at least a format string argument");
            }

            std::string_view fmt = args[1].as_string();
            size_t arg_idx = 2;
            for (size_t i = 0; i < fmt.size(); ++i) {
                if (fmt[i] == '%' && i + 1 < fmt.size()) {
                    i++;
                    if (fmt[i] == 'd') {
                        if (arg_idx < args.size() &&
                            (args[arg_idx].type == ValueType::I64 || args[arg_idx].type == ValueType::I32 ||
                             args[arg_idx].type == ValueType::I16 || args[arg_idx].type == ValueType::I8)) {
                            std::cout << args[arg_idx++].i64_value();
                        } else {
                            std::cout << "%d";
                        }
                    } else if (fmt[i] == 's') {
                        if (arg_idx < args.size() && args[arg_idx].type == ValueType::String) {
                            std::cout << args[arg_idx++].as_string();
                        } else {
                            std::cout << "%s";
                        }
                    } else if (fmt[i] == 'p') {
                        if (arg_idx < args.size() && args[arg_idx].type == ValueType::Ptr) {
                            std::cout << args[arg_idx++].as.ptr;
                        } else {
                            std::cout << "%p";
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
            std::cout << std::flush;
            coro.stack.push_back(Value(0));
            return;
        }

        case 11: {  // MALLOC
            int32_t size = (int32_t)args[1].i64_value();
            void *ptr = malloc(size);
            Value res;
            res.type = ValueType::Ptr;
            res.as.ptr = ptr;
            coro.stack.push_back(res);
            return;
        }

        case 12: {  // FREE
            Value ptr_val = args[1];
            if (ptr_val.type == ValueType::Ptr) {
                free(ptr_val.as.ptr);
            }
            coro.stack.push_back(Value(0));
            return;
        }

        case 13: {  // SOCKET
            int domain = (int)args[1].i64_value();
            int type = (int)args[2].i64_value();
            int protocol = (int)args[3].i64_value();
            int fd = socket(domain, type, protocol);
            coro.stack.push_back(Value(fd));
            return;
        }
        case 14: {  // BIND
            int fd = (int)args[1].i64_value();
            int port = (int)args[2].i64_value();
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = INADDR_ANY;
            int res = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
            coro.stack.push_back(Value(res));
            return;
        }
        case 15: {  // LISTEN
            int fd = (int)args[1].i64_value();
            int backlog = (int)args[2].i64_value();
            int res = listen(fd, backlog);
            coro.stack.push_back(Value(res));
            return;
        }

        default:
            break;  // Continue to async syscalls
    }

    // Async I/O syscalls
    struct io_uring_sqe *sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        coro.stack.push_back(Value(-1));
        return;
    }

    switch (id) {
        case 0: {  // OPEN
            const char *path = args[2].as.str;
            int flags = (int)args[3].i64_value();
            int mode = (int)args[4].i64_value();
            io_uring_prep_openat(sqe, AT_FDCWD, path, flags, mode);
            break;
        }
        case 1: {  // READ
            int fd = (int)args[1].i64_value();
            void *buf = args[2].as.ptr;
            int size = (int)args[3].i64_value();
            io_uring_prep_read(sqe, fd, buf, size, 0);
            break;
        }
        case 2: {  // WRITE
            int fd = (int)args[1].i64_value();
            const char *buf = (args[2].type == ValueType::String) ? args[2].as.str : (const char *)args[2].as.ptr;
            int size = (int)args[3].i64_value();
            io_uring_prep_write(sqe, fd, buf, size, 0);
            break;
        }
        case 3: {  // CLOSE
            int fd = (int)args[1].i64_value();
            io_uring_prep_close(sqe, fd);
            break;
        }
        case 4: {  // SLEEP
            int32_t ms = (int32_t)args[1].i64_value();
            coro.timeout.tv_sec = ms / 1000;
            coro.timeout.tv_nsec = (ms % 1000) * 1000000;
            io_uring_prep_timeout(sqe, &coro.timeout, 0, 0);
            break;
        }
        case 5: {  // ACCEPT
            int fd = (int)args[1].i64_value();
            io_uring_prep_accept(sqe, fd, NULL, NULL, 0);
            break;
        }
        case 6: {  // CONNECT
            int fd = (int)args[1].i64_value();
            std::string ip_str(args[2].as_string());
            int port = (int)args[3].i64_value();

            coro.io_buffer.resize(sizeof(struct sockaddr_in));
            struct sockaddr_in *addr = (struct sockaddr_in *)coro.io_buffer.data();
            memset(addr, 0, sizeof(struct sockaddr_in));
            addr->sin_family = AF_INET;
            addr->sin_port = htons(port);
            inet_pton(AF_INET, ip_str.c_str(), &addr->sin_addr);

            io_uring_prep_connect(sqe, fd, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
            break;
        }
        case 7: {  // SEND
            int fd = (int)args[1].i64_value();
            const void *buf = (args[2].type == ValueType::String) ? (const void *)args[2].as.str : args[2].as.ptr;
            int len = (int)args[3].i64_value();
            int flags = (int)args[4].i64_value();
            io_uring_prep_send(sqe, fd, buf, len, flags);
            break;
        }
        case 8: {  // RECV
            int fd = (int)args[1].i64_value();
            void *buf = args[2].as.ptr;
            int len = (int)args[3].i64_value();
            int flags = (int)args[4].i64_value();
            io_uring_prep_recv(sqe, fd, buf, len, flags);
            break;
        }
        default:
            coro.stack.push_back(Value(-2));
            return;
    }

    io_uring_sqe_set_data(sqe, (void *)(uintptr_t)coro.id);
    io_uring_submit(&m_ring);
    coro.waiting_for_io = true;
}

}  // namespace ether::vm
