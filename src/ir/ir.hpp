#ifndef ETHER_IR_HPP
#define ETHER_IR_HPP

#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ether::ir {

constexpr int OpCodeSlotSize = sizeof(uint16_t);
constexpr int OpCodeAddrSize = sizeof(uint32_t);
constexpr int OpCodeOffsetSize = sizeof(int32_t);
constexpr int OpCodeArgCountSize = sizeof(uint8_t);

enum class OpCode : uint8_t {
    PUSH_I64,          // [uint8_t opcode] [i64 value] (9 bytes)
    PUSH_I32,          // [uint8_t opcode] [i32 value] (5 bytes)
    PUSH_I16,          // [uint8_t opcode] [i16 value] (3 bytes)
    PUSH_I8,           // [uint8_t opcode] [i8 value] (2 bytes)
    PUSH_F64,          // [uint8_t opcode] [f64 value] (9 bytes)
    PUSH_F32,          // [uint8_t opcode] [f32 value] (5 bytes)
    LOAD_VAR,          // [uint8_t opcode] [u16 slot] (3 bytes)
    STORE_VAR,         // [uint8_t opcode] [u16 slot] (3 bytes)
    ADD,               // [uint8_t opcode] (1 byte)
    SUB,               // [uint8_t opcode] (1 byte)
    MUL,               // [uint8_t opcode] (1 byte)
    DIV,               // [uint8_t opcode] (1 byte)
    ADD_F,             // [uint8_t opcode] (1 byte)
    SUB_F,             // [uint8_t opcode] (1 byte)
    MUL_F,             // [uint8_t opcode] (1 byte)
    DIV_F,             // [uint8_t opcode] (1 byte)
    RET,               // [uint8_t opcode] (2 byte)
    HALT,              // [uint8_t opcode] (1 byte)
    PUSH_STR,          // [uint8_t opcode] [u32 string_id] (5 bytes)
    STR_GET,           // [uint8_t opcode] (1 byte) -- pops (string, index) -> pushes i8
    STR_SET,           // [uint8_t opcode] (1 byte) -- pops (value, string, index), mutates
    ARR_ALLOC,         // [uint8_t opcode] [u32 count] [u32 elem_slots]
    STRUCT_ALLOC,      // [uint8_t opcode] [u32 slots]
    SYSCALL,           // [uint8_t opcode] [u8 num_args] (2 bytes)
    CALL,              // [uint8_t opcode] [u32 target_addr] [u8 num_args] (6 bytes)
    JMP,               // [uint8_t opcode] [u32 target_addr] (5 bytes)
    JZ,                // [uint8_t opcode] [u32 target_addr] (5 bytes)
    CMP_EQ,            // [uint8_t opcode] (1 byte)
    CMP_LE,            // [uint8_t opcode] (1 byte)
    CMP_LT,            // [uint8_t opcode] (1 byte)
    CMP_GT,            // [uint8_t opcode] (1 byte)
    CMP_GE,            // [uint8_t opcode] (1 byte)
    CMP_EQ_F,          // [uint8_t opcode] (1 byte)
    CMP_LE_F,          // [uint8_t opcode] (1 byte)
    CMP_LT_F,          // [uint8_t opcode] (1 byte)
    CMP_GT_F,          // [uint8_t opcode] (1 byte)
    CMP_GE_F,          // [uint8_t opcode] (1 byte)
    SPAWN,             // [uint8_t opcode] [u32 target_addr] [u8 num_args] (6 bytes)
    YIELD,             // [uint8_t opcode] (1 byte)
    AWAIT,             // [uint8_t opcode] (1 byte)
    POP,               // [uint8_t opcode] (1 byte)
    PUSH_VARARGS,      // [uint8_t opcode] (1 byte)
    LOAD_GLOBAL,       // [uint8_t opcode] [u16 slot] (3 bytes)
    STORE_GLOBAL,      // [uint8_t opcode] [u16 slot] (3 bytes)
    LOAD_PTR_OFFSET,   // [uint8_t opcode] [i32 offset] (5 bytes)
    STORE_PTR_OFFSET,  // [uint8_t opcode] [i32 offset] (5 bytes)
};

struct IRProgram {
    std::vector<uint8_t> bytecode;
    std::vector<std::string> string_pool;

    struct FunctionInfo {
        size_t entry_addr;
        uint8_t num_params;
        uint32_t num_slots;
    };
    std::unordered_map<std::string, FunctionInfo> functions;
    std::unordered_map<size_t, FunctionInfo> addr_to_info;  // Fast lookup
    size_t main_addr = 0;
    uint32_t num_globals = 0;
};

std::ostream &operator<<(std::ostream &os, OpCode op);

}  // namespace ether::ir

#endif  // ETHER_IR_HPP
