#ifndef ETHER_VM_HPP
#define ETHER_VM_HPP

#include <liburing.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ir/ir.hpp"

namespace ether::vm {

enum class ValueType : uint8_t { Int, String, Ptr };

struct Value {
    ValueType type;
    uint32_t str_len;     // for string_view support
    union {
        int32_t i32;
        const char* str;  // string_view representation
        void* ptr;        // raw pointer for I/O buffers
    } as;

    Value() : type(ValueType::Int) { as.i32 = 0; }
    Value(int32_t v) : type(ValueType::Int) { as.i32 = v; }
    Value(std::string_view v) : type(ValueType::String) {
        as.str = v.data();
        str_len = static_cast<uint32_t>(v.size());
    }

    std::string_view as_string() const { return std::string_view(as.str, str_len); }
};

inline std::ostream& operator<<(std::ostream& os, const Value& val) {
    if (val.type == ValueType::Int) {
        os << val.as.i32;
    } else {
        os << val.as_string();
    }
    return os;
}

struct OpCodeStats {
    uint64_t count = 0;
    std::chrono::nanoseconds total_time{0};
};

struct CallFrame {
    size_t return_addr;
    size_t stack_base;  // offset in m_stack where locals start
    uint8_t num_fixed_params;
    uint8_t num_args_passed;
};

struct Coroutine {
    uint32_t id;
    int32_t waiting_for_id = -1;  // ID of coroutine we are awaiting
    bool waiting_for_io = false;  // Flag for I/O wait
    std::vector<Value> stack;
    std::vector<CallFrame> call_stack;
    size_t ip;
    Value result;
    struct __kernel_timespec timeout;
    bool finished = false;

    friend std::ostream& operator<<(std::ostream& os, const Coroutine& coro) {
        os << "Coroutine " << coro.id << " at IP " << coro.ip;
        if (coro.waiting_for_id != -1) {
            os << " (awaiting " << coro.waiting_for_id << ")";
        }
        if (coro.waiting_for_io) {
            os << " (waiting for IO)";
        }
        if (coro.finished) {
            os << " (finished)";
        }
        return os;
    }
};

class VM {
   public:
    explicit VM(const ir::IRProgram& program);
    ~VM();
    Value run(bool collect_stats = false);

    const std::unordered_map<ir::OpCode, OpCodeStats>& get_stats() const { return m_stats; }

   private:
    const ir::IRProgram& program_;
    std::vector<std::unique_ptr<Coroutine>> m_coroutines;
    std::vector<Value> m_globals;
    size_t m_current_coro = 0;
    uint32_t m_next_coro_id = 1;
    std::unordered_map<uint32_t, Value> m_finished_coros;
    std::unordered_map<ir::OpCode, OpCodeStats> m_stats;

    struct io_uring m_ring;

    void handle_io_completion();
    void submit_syscall(Coroutine& coro, uint8_t num_args);

    inline void push(Value val) { m_coroutines[m_current_coro]->stack.push_back(val); }
    inline Value pop() {
        auto& coro = m_coroutines[m_current_coro];
        Value val = coro->stack.back();
        coro->stack.pop_back();
        return val;
    }
};

}  // namespace ether::vm

#endif  // ETHER_VM_HPP
