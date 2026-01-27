#include "ir_gen.hpp"

#include <cstring>
#include <stdexcept>

namespace ether::ir_gen {

ir::IRProgram IRGenerator::generate(const parser::Program &ast) {
    m_program.bytecode.clear();
    m_program.string_pool.clear();
    m_program.functions.clear();

    // First pass: collect function signatures and names
    for (const auto &func : ast.functions) {
        m_program.functions[func->name] = {0, (uint8_t)func->params.size(), 0};
    }

    // Second pass: generate code
    for (const auto &func : ast.functions) {
        m_program.functions[func->name].entry_addr = m_program.bytecode.size();
        if (func->name == "main") m_program.main_addr = m_program.bytecode.size();
        visit_function(*func);
    }

    emit_opcode(ir::OpCode::HALT);

    for (const auto &[name, info] : m_program.functions) {
        m_program.addr_to_info[info.entry_addr] = info;
    }

    return std::move(m_program);
}

void IRGenerator::visit_function(const parser::Function &func) {
    m_scopes.emplace_back();  // New scope for function

    // Define parameters in scope
    for (const auto &param : func.params) {
        define_var(param.name);
    }

    visit_block(*func.body);

    // Ensure the function always returns.
    // We check if the last statement is a return or a block ending in a return.
    auto ends_with_ret = [](const parser::Block &b) {
        auto is_ret = [](const parser::Statement *s) -> bool {
            return dynamic_cast<const parser::ReturnStatement *>(s) != nullptr;
        };

        if (b.statements.empty()) return false;
        const parser::Statement *last = b.statements.back().get();
        if (is_ret(last)) return true;

        // Handle nested blocks at the end
        while (auto nested = dynamic_cast<const parser::Block *>(last)) {
            if (nested->statements.empty()) return false;
            last = nested->statements.back().get();
            if (is_ret(last)) return true;
        }
        return false;
    };

    if (!ends_with_ret(*func.body)) {
        emit_opcode(ir::OpCode::PUSH_INT);
        emit_int(0);
        emit_opcode(ir::OpCode::RET);
    }

    // Record how many slots this function needs
    m_program.functions[func.name].num_slots = m_scopes.back().next_slot;
    m_scopes.pop_back();
}

void IRGenerator::visit_block(const parser::Block &block) {
    for (const auto &stmt : block.statements) {
        visit_statement(*stmt);
    }
}

void IRGenerator::visit_statement(const parser::Statement &stmt) {
    if (auto ret = dynamic_cast<const parser::ReturnStatement *>(&stmt)) {
        visit_expression(*ret->expr);
        emit_opcode(ir::OpCode::RET);
    } else if (auto decl = dynamic_cast<const parser::VariableDeclaration *>(&stmt)) {
        if (decl->init) {
            visit_expression(*decl->init);
        } else {
            emit_opcode(ir::OpCode::PUSH_INT);
            emit_int(0);
        }
        define_var(decl->name);
        emit_opcode(ir::OpCode::STORE_VAR);
        emit_byte(get_var_slot(decl->name));
    } else if (auto expr_stmt = dynamic_cast<const parser::ExpressionStatement *>(&stmt)) {
        visit_expression(*expr_stmt->expr);
    } else if (auto if_stmt = dynamic_cast<const parser::IfStatement *>(&stmt)) {
        visit_expression(*if_stmt->condition);
        auto jump_to_else = emit_jump(ir::OpCode::JZ);

        visit_block(*if_stmt->then_branch);
        auto jump_to_end = emit_jump(ir::OpCode::JMP);

        patch_jump(jump_to_else, m_program.bytecode.size());
        if (if_stmt->else_branch) {
            visit_block(*if_stmt->else_branch);
        }
        patch_jump(jump_to_end, m_program.bytecode.size());
    } else if (auto for_stmt = dynamic_cast<const parser::ForStatement *>(&stmt)) {
        if (for_stmt->init) visit_statement(*for_stmt->init);

        size_t start_label = m_program.bytecode.size();
        JumpPlaceholder jump_to_exit{0};

        if (for_stmt->condition) {
            visit_expression(*for_stmt->condition);
            jump_to_exit = emit_jump(ir::OpCode::JZ);
        }

        visit_block(*for_stmt->body);

        if (for_stmt->increment) {
            visit_expression(*for_stmt->increment);
        }

        emit_opcode(ir::OpCode::JMP);
        emit_uint32((uint32_t)start_label);

        if (for_stmt->condition) {
            patch_jump(jump_to_exit, m_program.bytecode.size());
        }
    } else if (auto yield_stmt = dynamic_cast<const parser::YieldStatement *>(&stmt)) {
        emit_opcode(ir::OpCode::YIELD);
    }
}

void IRGenerator::visit_expression(const parser::Expression &expr) {
    if (auto lit = dynamic_cast<const parser::IntegerLiteral *>(&expr)) {
        emit_opcode(ir::OpCode::PUSH_INT);
        emit_int(lit->value);
    } else if (auto var = dynamic_cast<const parser::VariableExpression *>(&expr)) {
        emit_opcode(ir::OpCode::LOAD_VAR);
        emit_byte(get_var_slot(var->name));
    } else if (auto assign = dynamic_cast<const parser::AssignmentExpression *>(&expr)) {
        visit_expression(*assign->value);
        emit_opcode(ir::OpCode::STORE_VAR);
        emit_byte(get_var_slot(assign->lvalue->name));
    } else if (auto inc = dynamic_cast<const parser::IncrementExpression *>(&expr)) {
        emit_opcode(ir::OpCode::LOAD_VAR);
        emit_byte(get_var_slot(inc->lvalue->name));
        emit_opcode(ir::OpCode::PUSH_INT);
        emit_int(1);
        emit_opcode(ir::OpCode::ADD);
        emit_opcode(ir::OpCode::STORE_VAR);
        emit_byte(get_var_slot(inc->lvalue->name));
        // Push result for expression consistency
        emit_opcode(ir::OpCode::LOAD_VAR);
        emit_byte(get_var_slot(inc->lvalue->name));
    } else if (auto dec = dynamic_cast<const parser::DecrementExpression *>(&expr)) {
        emit_opcode(ir::OpCode::LOAD_VAR);
        emit_byte(get_var_slot(dec->lvalue->name));
        emit_opcode(ir::OpCode::PUSH_INT);
        emit_int(1);
        emit_opcode(ir::OpCode::SUB);
        emit_opcode(ir::OpCode::STORE_VAR);
        emit_byte(get_var_slot(dec->lvalue->name));
        // Push result for expression consistency
        emit_opcode(ir::OpCode::LOAD_VAR);
        emit_byte(get_var_slot(dec->lvalue->name));
    } else if (auto await_expr = dynamic_cast<const parser::AwaitExpression *>(&expr)) {
        visit_expression(*await_expr->expr);
        emit_opcode(ir::OpCode::AWAIT);
    } else if (auto spawn_expr = dynamic_cast<const parser::SpawnExpression *>(&expr)) {
        // Push arguments
        for (const auto &arg : spawn_expr->call->args) {
            visit_expression(*arg);
        }
        emit_opcode(ir::OpCode::SPAWN);
        emit_uint32((uint32_t)m_program.functions.at(spawn_expr->call->name).entry_addr);
    } else if (auto bin = dynamic_cast<const parser::BinaryExpression *>(&expr)) {
        visit_expression(*bin->left);
        visit_expression(*bin->right);
        switch (bin->op) {
            case parser::BinaryExpression::Op::Add:
                emit_opcode(ir::OpCode::ADD);
                break;
            case parser::BinaryExpression::Op::Sub:
                emit_opcode(ir::OpCode::SUB);
                break;
            case parser::BinaryExpression::Op::Mul:
                emit_opcode(ir::OpCode::MUL);
                break;
            case parser::BinaryExpression::Op::Div:
                emit_opcode(ir::OpCode::DIV);
                break;
            case parser::BinaryExpression::Op::Leq:
                emit_opcode(ir::OpCode::CMP_LE);
                break;
            case parser::BinaryExpression::Op::Less:
                emit_opcode(ir::OpCode::CMP_LT);
                break;
            case parser::BinaryExpression::Op::Eq:
                emit_opcode(ir::OpCode::CMP_EQ);
                break;
            case parser::BinaryExpression::Op::Gt:
                emit_opcode(ir::OpCode::CMP_GT);
                break;
            case parser::BinaryExpression::Op::Geq:
                emit_opcode(ir::OpCode::CMP_GE);
                break;
            default:
                throw std::runtime_error("Unsupported binary op in refactored IR Gen");
        }
    } else if (auto str = dynamic_cast<const parser::StringLiteral *>(&expr)) {
        emit_opcode(ir::OpCode::PUSH_STR);
        emit_uint32(get_string_id(str->value));
    } else if (auto call = dynamic_cast<const parser::FunctionCall *>(&expr)) {
        for (const auto &arg : call->args) {
            visit_expression(*arg);
        }
        if (call->name == "write") {
            emit_opcode(ir::OpCode::SYS_WRITE);
        } else if (call->name == "printf") {
            emit_opcode(ir::OpCode::SYS_PRINTF);
            emit_byte((uint8_t)call->args.size());
        } else {
            emit_opcode(ir::OpCode::CALL);
            emit_uint32((uint32_t)m_program.functions.at(call->name).entry_addr);
        }
    } else {
        throw std::runtime_error("Unknown expression type in IR generation");
    }
}

void IRGenerator::emit_int(int32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, 4);
    for (int i = 0; i < 4; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_uint32(uint32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, 4);
    for (int i = 0; i < 4; ++i) emit_byte(bytes[i]);
}

uint32_t IRGenerator::get_string_id(const std::string &str) {
    for (uint32_t i = 0; i < m_program.string_pool.size(); ++i) {
        if (m_program.string_pool[i] == str) return i;
    }
    m_program.string_pool.push_back(str);
    return (uint32_t)m_program.string_pool.size() - 1;
}

uint8_t IRGenerator::get_var_slot(const std::string &name) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->variables.contains(name)) return it->variables.at(name).slot;
    }
    throw std::runtime_error("Undefined variable: " + name);
}

void IRGenerator::define_var(const std::string &name) {
    if (m_scopes.empty()) throw std::runtime_error("Variable defined outside scope");
    uint8_t slot = m_scopes.back().next_slot++;
    m_scopes.back().variables[name] = {slot};
}

IRGenerator::JumpPlaceholder IRGenerator::emit_jump(ir::OpCode op) {
    emit_opcode(op);
    size_t pos = m_program.bytecode.size();
    emit_uint32(0);  // placeholder
    return {pos};
}

void IRGenerator::patch_jump(JumpPlaceholder jp, uint32_t target) {
    std::memcpy(&m_program.bytecode[jp.pos], &target, 4);
}

}  // namespace ether::ir_gen
