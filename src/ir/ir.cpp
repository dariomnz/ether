#include "ir.hpp"

namespace ether::ir {

std::ostream &operator<<(std::ostream &os, OpCode op) {
    switch (op) {
        case OpCode::PUSH_INT:
            os << "PUSH_INT";
            break;
        case OpCode::LOAD_VAR:
            os << "LOAD_VAR";
            break;
        case OpCode::STORE_VAR:
            os << "STORE_VAR";
            break;
        case OpCode::ADD:
            os << "ADD";
            break;
        case OpCode::SUB:
            os << "SUB";
            break;
        case OpCode::MUL:
            os << "MUL";
            break;
        case OpCode::DIV:
            os << "DIV";
            break;
        case OpCode::RET:
            os << "RET";
            break;
        case OpCode::HALT:
            os << "HALT";
            break;
        case OpCode::PUSH_STR:
            os << "PUSH_STR";
            break;
        case OpCode::SYS_OPEN:
            os << "SYS_OPEN";
            break;
        case OpCode::SYS_READ:
            os << "SYS_READ";
            break;
        case OpCode::SYS_WRITE:
            os << "SYS_WRITE";
            break;
        case OpCode::SYS_CLOSE:
            os << "SYS_CLOSE";
            break;
        case OpCode::SYS_PRINTF:
            os << "SYS_PRINTF";
            break;
        case OpCode::CALL:
            os << "CALL";
            break;
        case OpCode::JMP:
            os << "JMP";
            break;
        case OpCode::JZ:
            os << "JZ";
            break;
        case OpCode::CMP_EQ:
            os << "CMP_EQ";
            break;
        case OpCode::CMP_LE:
            os << "CMP_LE";
            break;
        case OpCode::CMP_LT:
            os << "CMP_LT";
            break;
        case OpCode::CMP_GT:
            os << "CMP_GT";
            break;
        case OpCode::CMP_GE:
            os << "CMP_GE";
            break;
        case OpCode::SPAWN:
            os << "SPAWN";
            break;
        case OpCode::YIELD:
            os << "YIELD";
            break;
        case OpCode::AWAIT:
            os << "AWAIT";
            break;
        case OpCode::POP:
            os << "POP";
            break;
    }
    return os;
}

}  // namespace ether::ir
