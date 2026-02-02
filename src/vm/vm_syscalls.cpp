#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "vm.hpp"

namespace ether::vm {

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
                coro->pending_args.clear();
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
                    int precision = -1;
                    if (fmt[i] == '.') {
                        i++;
                        while (fmt[i] >= '0' && fmt[i] <= '9') {
                            if (precision == -1) {
                                precision = 0;
                            }
                            precision = precision * 10 + (fmt[i] - '0');
                            i++;
                        }
                    }
                    if (fmt[i] == 'd') {
                        if (arg_idx < args.size() &&
                            (args[arg_idx].type == ValueType::I64 || args[arg_idx].type == ValueType::I32 ||
                             args[arg_idx].type == ValueType::I16 || args[arg_idx].type == ValueType::I8)) {
                            std::cout << args[arg_idx++].i64_value();
                        } else {
                            std::cout << "%d";
                        }
                    } else if (fmt[i] == 'f') {
                        if (arg_idx < args.size() && args[arg_idx].type == ValueType::F64 ||
                            args[arg_idx].type == ValueType::F32) {
                            if (precision != -1) {
                                std::cout << std::fixed << std::setprecision(precision);
                            }
                            std::cout << args[arg_idx++].f64_value();
                        } else {
                            std::cout << "%f";
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

        case 16: {  // STRLEN
            if (args.size() < 2 || args[1].type != ValueType::String) {
                throw std::runtime_error("strlen requires a string argument");
            }
            coro.stack.push_back(Value((int32_t)args[1].str_len));
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

    coro.pending_args = std::move(args);
    auto &args_ref = coro.pending_args;

    switch (id) {
        case 0: {  // OPEN
            const char *path = args_ref[2].as.str;
            int flags = (int)args_ref[3].i64_value();
            int mode = (int)args_ref[4].i64_value();
            io_uring_prep_openat(sqe, AT_FDCWD, path, flags, mode);
            break;
        }
        case 1: {  // READ
            int fd = (int)args_ref[1].i64_value();
            void *buf = args_ref[2].as.ptr;
            int size = (int)args_ref[3].i64_value();
            io_uring_prep_read(sqe, fd, buf, size, 0);
            break;
        }
        case 2: {  // WRITE
            int fd = (int)args_ref[1].i64_value();
            const char *buf =
                (args_ref[2].type == ValueType::String) ? args_ref[2].as.str : (const char *)args_ref[2].as.ptr;
            int size = (int)args_ref[3].i64_value();
            io_uring_prep_write(sqe, fd, buf, size, 0);
            break;
        }
        case 3: {  // CLOSE
            int fd = (int)args_ref[1].i64_value();
            io_uring_prep_close(sqe, fd);
            break;
        }
        case 4: {  // SLEEP
            int32_t ms = (int32_t)args_ref[1].i64_value();
            coro.timeout.tv_sec = ms / 1000;
            coro.timeout.tv_nsec = (ms % 1000) * 1000000;
            io_uring_prep_timeout(sqe, &coro.timeout, 0, 0);
            break;
        }
        case 5: {  // ACCEPT
            int fd = (int)args_ref[1].i64_value();
            io_uring_prep_accept(sqe, fd, NULL, NULL, 0);
            break;
        }
        case 6: {  // CONNECT
            int fd = (int)args_ref[1].i64_value();
            std::string ip_str(args_ref[2].as_string());
            int port = (int)args_ref[3].i64_value();

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
            int fd = (int)args_ref[1].i64_value();
            const void *buf =
                (args_ref[2].type == ValueType::String) ? (const void *)args_ref[2].as.str : args_ref[2].as.ptr;
            int len = (int)args_ref[3].i64_value();
            int flags = (int)args_ref[4].i64_value();
            io_uring_prep_send(sqe, fd, buf, len, flags);
            break;
        }
        case 8: {  // RECV
            int fd = (int)args_ref[1].i64_value();
            void *buf = args_ref[2].as.ptr;
            int len = (int)args_ref[3].i64_value();
            int flags = (int)args_ref[4].i64_value();
            io_uring_prep_recv(sqe, fd, buf, len, flags);
            break;
        }
        default:
            coro.stack.push_back(Value(-2));
            coro.pending_args.clear();
            return;
    }

    io_uring_sqe_set_data(sqe, (void *)(uintptr_t)coro.id);
    io_uring_submit(&m_ring);
    coro.waiting_for_io = true;
}

}  // namespace ether::vm
