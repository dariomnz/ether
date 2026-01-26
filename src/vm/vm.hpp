#ifndef ETHER_VM_HPP
#define ETHER_VM_HPP

#include <chrono>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "ir/ir.hpp"

namespace ether::vm {

using Value = std::variant<int, std::string_view>;

inline std::ostream& operator<<(std::ostream& os, const Value& val) {
    if (std::holds_alternative<int>(val)) {
        os << std::get<int>(val);
    } else {
        os << std::get<std::string_view>(val);
    }
    return os;
}

struct OpCodeStats {
    uint64_t count = 0;
    std::chrono::nanoseconds total_time{0};
};

struct CallFrame {
    size_t return_addr;
    std::unordered_map<std::string_view, Value> locals;
};

class VM {
   public:
    explicit VM(const ir::IRProgram& program);
    Value run(bool collect_stats = false);

    const std::unordered_map<ir::OpCode, OpCodeStats>& get_stats() const { return m_stats; }

   private:
    const ir::IRProgram& m_program;
    std::vector<Value> m_stack;
    std::vector<CallFrame> m_call_stack;
    size_t m_ip = 0;
    std::unordered_map<ir::OpCode, OpCodeStats> m_stats;

    void push(Value val);
    Value pop();
};

}  // namespace ether::vm

#endif  // ETHER_VM_HPP
