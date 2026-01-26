#ifndef ETHER_IR_GEN_HPP
#define ETHER_IR_GEN_HPP

#include <memory>

#include "ir.hpp"
#include "parser/ast.hpp"

namespace ether::ir_gen {

class IRGenerator {
   public:
    ir::IRProgram generate(const parser::Program &ast);

   private:
    ir::IRProgram m_program;

    void visit_function(const parser::Function &func);
    void visit_block(const parser::Block &block);
    void visit_statement(const parser::Statement &stmt);
    void visit_expression(const parser::Expression &expr);
};

}  // namespace ether::ir_gen

#endif  // ETHER_IR_GEN_HPP
