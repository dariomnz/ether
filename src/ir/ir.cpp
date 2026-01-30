#include "ir.hpp"

namespace ether::ir {

std::ostream &operator<<(std::ostream &os, OpCode op) {
    static const std::unordered_map<OpCode, std::string_view> op_to_str = {
        {OpCode::PUSH_I64, "PUSH_I64"},
        {OpCode::PUSH_I32, "PUSH_I32"},
        {OpCode::PUSH_I16, "PUSH_I16"},
        {OpCode::PUSH_I8, "PUSH_I8"},
        {OpCode::LOAD_VAR, "LOAD_VAR"},
        {OpCode::STORE_VAR, "STORE_VAR"},
        {OpCode::ADD, "ADD"},
        {OpCode::SUB, "SUB"},
        {OpCode::MUL, "MUL"},
        {OpCode::DIV, "DIV"},
        {OpCode::RET, "RET"},
        {OpCode::HALT, "HALT"},
        {OpCode::PUSH_STR, "PUSH_STR"},
        {OpCode::SYSCALL, "SYSCALL"},
        {OpCode::CALL, "CALL"},
        {OpCode::JMP, "JMP"},
        {OpCode::JZ, "JZ"},
        {OpCode::CMP_EQ, "CMP_EQ"},
        {OpCode::CMP_LE, "CMP_LE"},
        {OpCode::CMP_LT, "CMP_LT"},
        {OpCode::CMP_GT, "CMP_GT"},
        {OpCode::CMP_GE, "CMP_GE"},
        {OpCode::SPAWN, "SPAWN"},
        {OpCode::YIELD, "YIELD"},
        {OpCode::AWAIT, "AWAIT"},
        {OpCode::POP, "POP"},
        {OpCode::PUSH_VARARGS, "PUSH_VARARGS"},
        {OpCode::LOAD_GLOBAL, "LOAD_GLOBAL"},
        {OpCode::STORE_GLOBAL, "STORE_GLOBAL"},
        {OpCode::LOAD_PTR_OFFSET, "LOAD_PTR_OFFSET"},
        {OpCode::STORE_PTR_OFFSET, "STORE_PTR_OFFSET"},
    };
    auto it = op_to_str.find(op);
    if (it != op_to_str.end()) {
        os << it->second;
    } else {
        os << "UNKNOWN";
    }
    return os;
}

}  // namespace ether::ir
