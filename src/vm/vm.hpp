#ifndef ETHER_VM_HPP
#define ETHER_VM_HPP

#include <chrono>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ir/ir.hpp"

namespace ether::vm {

enum class ValueType : uint8_t { Int, String };

struct Value {
    ValueType type;
    uint32_t str_len;     // for string_view support
    union {
        int32_t i32;
        const char* str;  // string_view representation
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
};

struct Coroutine {
    std::vector<Value> stack;
    std::vector<CallFrame> call_stack;
    size_t ip;
    bool finished = false;
};

class VM {
   public:
    explicit VM(const ir::IRProgram& program);
    Value run(bool collect_stats = false);

    const std::unordered_map<ir::OpCode, OpCodeStats>& get_stats() const { return m_stats; }

   private:
    const ir::IRProgram& program_;
    std::vector<Coroutine> m_coroutines;
    size_t m_current_coro = 0;
    std::unordered_map<ir::OpCode, OpCodeStats> m_stats;

    inline void push(Value val) { m_coroutines[m_current_coro].stack.push_back(val); }
    inline Value pop() {
        auto& coro = m_coroutines[m_current_coro];
        Value val = coro.stack.back();
        coro.stack.pop_back();
        return val;
    }
};

}  // namespace ether::vm

#endif  // ETHER_VM_HPP
