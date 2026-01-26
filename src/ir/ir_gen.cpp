#include "ir_gen.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ether::ir_gen {

ir::IRProgram IRGenerator::generate(const parser::Program &ast) {
    m_program.instructions.clear();
    m_program.entry_points.clear();
    for (const auto &func : ast.functions) {
        m_program.entry_points[std::string(func->name)] = m_program.instructions.size();
        visit_function(*func);
    }
    m_program.instructions.push_back({ir::OpCode::HALT, 0});
    return std::move(m_program);
}

void IRGenerator::visit_function(const parser::Function &func) {
    m_program.instructions.push_back({ir::OpCode::LABEL, std::string(func.name)});
    // Store params into variables (reverse order from stack)
    for (auto it = func.params.rbegin(); it != func.params.rend(); ++it) {
        m_program.instructions.push_back({ir::OpCode::STORE_VAR, std::string(*it)});
    }
    visit_block(*func.body);
}

void IRGenerator::visit_block(const parser::Block &block) {
    for (const auto &stmt : block.statements) {
        visit_statement(*stmt);
    }
}

void IRGenerator::visit_statement(const parser::Statement &stmt) {
    if (auto ret = dynamic_cast<const parser::ReturnStatement *>(&stmt)) {
        visit_expression(*ret->expr);
        m_program.instructions.push_back({ir::OpCode::RET, 0});
    } else if (auto decl = dynamic_cast<const parser::VariableDeclaration *>(&stmt)) {
        if (decl->init) {
            visit_expression(*decl->init);
            m_program.instructions.push_back({ir::OpCode::STORE_VAR, decl->name});
        }
    } else if (auto expr_stmt = dynamic_cast<const parser::ExpressionStatement *>(&stmt)) {
        visit_expression(*expr_stmt->expr);
    } else if (auto if_stmt = dynamic_cast<const parser::IfStatement *>(&stmt)) {
        static int label_count = 0;
        std::string else_label = "else_" + std::to_string(label_count);
        std::string end_label = "endif_" + std::to_string(label_count);
        label_count++;

        visit_expression(*if_stmt->condition);
        m_program.instructions.push_back({ir::OpCode::JZ, else_label});

        visit_block(*if_stmt->then_branch);
        m_program.instructions.push_back({ir::OpCode::JMP, end_label});

        m_program.entry_points[else_label] = m_program.instructions.size();
        m_program.instructions.push_back({ir::OpCode::LABEL, else_label});
        if (if_stmt->else_branch) {
            visit_block(*if_stmt->else_branch);
        }

        m_program.entry_points[end_label] = m_program.instructions.size();
        m_program.instructions.push_back({ir::OpCode::LABEL, end_label});
    } else {
        throw std::runtime_error("Unknown statement type in IR generation");
    }
}

void IRGenerator::visit_expression(const parser::Expression &expr) {
    if (auto lit = dynamic_cast<const parser::IntegerLiteral *>(&expr)) {
        m_program.instructions.push_back({ir::OpCode::PUSH_INT, lit->value});
    } else if (auto var = dynamic_cast<const parser::VariableExpression *>(&expr)) {
        m_program.instructions.push_back({ir::OpCode::LOAD_VAR, var->name});
    } else if (auto bin = dynamic_cast<const parser::BinaryExpression *>(&expr)) {
        visit_expression(*bin->left);
        visit_expression(*bin->right);
        switch (bin->op) {
            case parser::BinaryExpression::Op::Add:
                m_program.instructions.push_back({ir::OpCode::ADD, 0});
                break;
            case parser::BinaryExpression::Op::Sub:
                m_program.instructions.push_back({ir::OpCode::SUB, 0});
                break;
            case parser::BinaryExpression::Op::Mul:
                m_program.instructions.push_back({ir::OpCode::MUL, 0});
                break;
            case parser::BinaryExpression::Op::Div:
                m_program.instructions.push_back({ir::OpCode::DIV, 0});
                break;
            case parser::BinaryExpression::Op::Eq:
                m_program.instructions.push_back({ir::OpCode::CMP_EQ, 0});
                break;
            case parser::BinaryExpression::Op::Leq:
                m_program.instructions.push_back({ir::OpCode::CMP_LE, 0});
                break;
            case parser::BinaryExpression::Op::Less:
                m_program.instructions.push_back({ir::OpCode::CMP_LT, 0});
                break;
            case parser::BinaryExpression::Op::Gt:
                m_program.instructions.push_back({ir::OpCode::CMP_GT, 0});
                break;
            case parser::BinaryExpression::Op::Geq:
                m_program.instructions.push_back({ir::OpCode::CMP_GE, 0});
                break;
        }
    } else if (auto str = dynamic_cast<const parser::StringLiteral *>(&expr)) {
        m_program.instructions.push_back({ir::OpCode::PUSH_STR, str->value});
    } else if (auto call = dynamic_cast<const parser::FunctionCall *>(&expr)) {
        for (const auto &arg : call->args) {
            visit_expression(*arg);
        }
        if (call->name == "write") {
            m_program.instructions.push_back({ir::OpCode::SYS_WRITE, 0});
        } else if (call->name == "open") {
            m_program.instructions.push_back({ir::OpCode::SYS_OPEN, 0});
        } else if (call->name == "read") {
            m_program.instructions.push_back({ir::OpCode::SYS_READ, 0});
        } else if (call->name == "close") {
            m_program.instructions.push_back({ir::OpCode::SYS_CLOSE, 0});
        } else {
            m_program.instructions.push_back({ir::OpCode::CALL, std::string(call->name)});
        }
    } else {
        throw std::runtime_error("Unknown expression type in IR generation");
    }
}

}  // namespace ether::ir_gen
