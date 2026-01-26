#ifndef ETHER_IR_HPP
#define ETHER_IR_HPP

#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ether::ir {

enum class OpCode : uint8_t {
    PUSH_INT,   // [uint8_t] [int32_t val]
    LOAD_VAR,   // [uint8_t] [uint8_t slot]
    STORE_VAR,  // [uint8_t] [uint8_t slot]
    ADD,        // [uint8_t]
    SUB,        // [uint8_t]
    MUL,        // [uint8_t]
    DIV,        // [uint8_t]
    RET,        // [uint8_t]
    HALT,       // [uint8_t]
    PUSH_STR,   // [uint8_t] [uint32_t string_id]
    SYS_OPEN,   // ... same as before
    SYS_READ,
    SYS_WRITE,
    SYS_CLOSE,
    CALL,   // [uint8_t] [uint32_t func_addr] - For simplicity, let's use direct address after linking
    LABEL,  // Only temporary
    JMP,    // [uint8_t] [int32_t target_addr]
    JZ,     // [uint8_t] [int32_t target_addr]
    CMP_EQ,
    CMP_LE,
    CMP_LT,
    CMP_GT,
    CMP_GE
};

struct IRProgram {
    std::vector<uint8_t> bytecode;
    std::vector<std::string> string_pool;

    struct FunctionInfo {
        size_t entry_addr;
        uint8_t num_params;
        uint8_t num_slots;
    };
    std::unordered_map<std::string, FunctionInfo> functions;
    std::unordered_map<size_t, FunctionInfo> addr_to_info;  // Fast lookup
    size_t main_addr = 0;
};

std::ostream &operator<<(std::ostream &os, OpCode op);

}  // namespace ether::ir

#endif  // ETHER_IR_HPP
