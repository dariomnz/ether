#include "ir.hpp"

namespace ether::ir {

std::ostream &operator<<(std::ostream &os, OpCode op) {
    static const std::unordered_map<OpCode, std::string_view> op_to_str = {
        {OpCode::PUSH_I64, "PUSH_I64"},
        {OpCode::PUSH_I32, "PUSH_I32"},
        {OpCode::PUSH_I16, "PUSH_I16"},
        {OpCode::PUSH_I8, "PUSH_I8"},
        {OpCode::PUSH_F64, "PUSH_F64"},
        {OpCode::PUSH_F32, "PUSH_F32"},
        {OpCode::LOAD_VAR, "LOAD_VAR"},
        {OpCode::STORE_VAR, "STORE_VAR"},
        {OpCode::ADD, "ADD"},
        {OpCode::SUB, "SUB"},
        {OpCode::MUL, "MUL"},
        {OpCode::DIV, "DIV"},
        {OpCode::ADD_F, "ADD_F"},
        {OpCode::SUB_F, "SUB_F"},
        {OpCode::MUL_F, "MUL_F"},
        {OpCode::DIV_F, "DIV_F"},
        {OpCode::RET, "RET"},
        {OpCode::HALT, "HALT"},
        {OpCode::PUSH_STR, "PUSH_STR"},
        {OpCode::STR_GET, "STR_GET"},
        {OpCode::STR_SET, "STR_SET"},
        {OpCode::ARR_ALLOC, "ARR_ALLOC"},
        {OpCode::SYSCALL, "SYSCALL"},
        {OpCode::CALL, "CALL"},
        {OpCode::JMP, "JMP"},
        {OpCode::JZ, "JZ"},
        {OpCode::CMP_EQ, "CMP_EQ"},
        {OpCode::CMP_LE, "CMP_LE"},
        {OpCode::CMP_LT, "CMP_LT"},
        {OpCode::CMP_GT, "CMP_GT"},
        {OpCode::CMP_GE, "CMP_GE"},
        {OpCode::CMP_EQ_F, "CMP_EQ_F"},
        {OpCode::CMP_LE_F, "CMP_LE_F"},
        {OpCode::CMP_LT_F, "CMP_LT_F"},
        {OpCode::CMP_GT_F, "CMP_GT_F"},
        {OpCode::CMP_GE_F, "CMP_GE_F"},
        {OpCode::SPAWN, "SPAWN"},
        {OpCode::YIELD, "YIELD"},
        {OpCode::AWAIT, "AWAIT"},
        {OpCode::POP, "POP"},
        {OpCode::PUSH_VARARGS, "PUSH_VARARGS"},
        {OpCode::LOAD_GLOBAL, "LOAD_GLOBAL"},
        {OpCode::STORE_GLOBAL, "STORE_GLOBAL"},
        {OpCode::LOAD_PTR_OFFSET, "LOAD_PTR_OFFSET"},
        {OpCode::STORE_PTR_OFFSET, "STORE_PTR_OFFSET"},
        {OpCode::LEA_STACK, "LEA_STACK"},
        {OpCode::LEA_GLOBAL, "LEA_GLOBAL"},
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
