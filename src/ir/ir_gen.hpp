#ifndef ETHER_IR_GEN_HPP
#define ETHER_IR_GEN_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir.hpp"
#include "parser/ast.hpp"

namespace ether::ir_gen {

class IRGenerator {
   public:
    ir::IRProgram generate(const parser::Program &ast);

   private:
    ir::IRProgram m_program;

    // Tracking for bytecode generation
    struct Symbol {
        uint8_t slot;
    };
    struct Scope {
        std::unordered_map<std::string, Symbol> variables;
        uint8_t next_slot = 0;
    };
    std::vector<Scope> m_scopes;

    void visit_function(const parser::Function &func);
    void visit_block(const parser::Block &block);
    void visit_statement(const parser::Statement &stmt);
    void visit_expression(const parser::Expression &expr);

    // Helpers
    void emit_byte(uint8_t byte) { m_program.bytecode.push_back(byte); }
    void emit_opcode(ir::OpCode op) { emit_byte(static_cast<uint8_t>(op)); }
    void emit_int(int32_t val);
    void emit_uint32(uint32_t val);

    uint32_t get_string_id(const std::string &str);
    uint8_t get_var_slot(const std::string &name);
    void define_var(const std::string &name);

    struct JumpPlaceholder {
        size_t pos;
    };
    JumpPlaceholder emit_jump(ir::OpCode op);
    void patch_jump(JumpPlaceholder jp, uint32_t target);
};

}  // namespace ether::ir_gen

#endif  // ETHER_IR_GEN_HPP
