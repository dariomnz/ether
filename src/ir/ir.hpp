#ifndef ETHER_IR_HPP
#define ETHER_IR_HPP

#include <iostream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ether::ir {

enum class OpCode {
    PUSH_INT,
    LOAD_VAR,
    STORE_VAR,
    ADD,
    SUB,
    MUL,
    DIV,
    RET,
    HALT,
    PUSH_STR,
    SYS_OPEN,
    SYS_READ,
    SYS_WRITE,
    SYS_CLOSE,
    CALL,
    LABEL,
    JMP,
    JZ,
    CMP_EQ,
    CMP_LE,
    CMP_LT,
    CMP_GT,
    CMP_GE
};

struct Instruction {
    OpCode op;
    std::variant<int, std::string> operand;
};

struct IRProgram {
    std::vector<Instruction> instructions;
    std::unordered_map<std::string, size_t> entry_points;
};

std::ostream &operator<<(std::ostream &os, OpCode op);

}  // namespace ether::ir

#endif  // ETHER_IR_HPP
