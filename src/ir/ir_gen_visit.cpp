#include <stdexcept>

#include "ir/ir_gen.hpp"

namespace ether::ir_gen {

void IRGenerator::visit(const parser::StructDeclaration &node) {
    // Layout already collected
}
void IRGenerator::visit(const parser::Include &node) {}

void IRGenerator::visit(const parser::SizeofExpression &node) {
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(get_type_size(node.target_type));
}

void IRGenerator::visit(const parser::MemberAccessExpression &node) {
    LValueResolver resolver;
    resolver.gen = this;
    node.accept(resolver);

    uint8_t size = 1;
    if (node.type && node.type->kind == parser::DataType::Kind::Struct) {
        size = m_structs.at(node.type->struct_name).total_size;
    }

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::LOAD_GLOBAL);
            emit_uint16(resolver.slot);
            emit_byte(size);
        } else {
            emit_opcode(ir::OpCode::LOAD_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_byte(size);
        }
    } else {
        emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
        emit_int32(resolver.offset);
        emit_byte(size);
    }
}

void IRGenerator::visit(const parser::IndexExpression &node) {
    // Load the pointer
    node.object->accept(*this);

    // Load the index
    node.index->accept(*this);

    // The pointer is now at stack[-2] and index at stack[-1]
    // We need to compute: ptr + (index * element_size)
    // For now, we assume element size is 16 bytes (1 slot)
    // If the inner type is a struct, we need to multiply by its size

    uint16_t element_size = 1;
    if (node.object->type && node.object->type->kind == parser::DataType::Kind::Ptr && node.object->type->inner) {
        if (node.object->type->inner->kind == parser::DataType::Kind::Struct) {
            element_size = m_structs.at(node.object->type->inner->struct_name).total_size;
        }
    }

    // Multiply by 16 to convert slot offset to byte offset
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(16 * element_size);  // sizeof(Value)
    emit_opcode(ir::OpCode::MUL);

    // Add the byte offset to the pointer
    emit_opcode(ir::OpCode::ADD);

    // Load from the computed address (offset 0)
    emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
    emit_int32(0);

    // Determine the size of the element being loaded
    uint8_t load_size = 1;
    if (node.type && node.type->kind == parser::DataType::Kind::Struct) {
        load_size = m_structs.at(node.type->struct_name).total_size;
    }
    emit_byte(load_size);
}

void IRGenerator::visit(const parser::Function &func) {
    m_scopes.emplace_back();  // New scope for function

    // Define parameters in scope
    for (const auto &param : func.params) {
        define_var(param.name);
    }

    func.body->accept(*this);

    // Ensure the function always returns.
    // We check if the last statement is a return or a block ending in a return.
    auto ends_with_ret = [](const parser::Block &b) {
        struct RetCheck : parser::DefaultIgnoreConstASTVisitor {
            bool is_ret = false;
            const parser::Block *nested_block = nullptr;
            void visit(const parser::ReturnStatement &) override { is_ret = true; }
            void visit(const parser::Block &node) override { nested_block = &node; }
        };

        if (b.statements.empty()) return false;
        const parser::Statement *last = b.statements.back().get();

        RetCheck checker;
        last->accept(checker);
        if (checker.is_ret) return true;

        // Handle nested blocks at the end
        while (checker.nested_block) {
            const parser::Block *current = checker.nested_block;
            if (current->statements.empty()) return false;
            last = current->statements.back().get();
            checker = RetCheck();  // Reset
            last->accept(checker);
            if (checker.is_ret) return true;
        }
        return false;
    };

    if (!ends_with_ret(*func.body)) {
        // Only valid for void functions or implicit int return 0?
        // Assuming implicit return 0 for now if main, or void.
        emit_opcode(ir::OpCode::PUSH_I32);
        emit_int32(0);
        emit_opcode(ir::OpCode::RET);
        emit_byte(1);
    }

    // Record how many slots this function needs
    m_program.functions[func.name].num_slots = m_scopes.back().next_slot;
    m_scopes.pop_back();
}

void IRGenerator::visit(const parser::Block &block) {
    for (const auto &stmt : block.statements) {
        stmt->accept(*this);
    }
}

void IRGenerator::visit(const parser::ReturnStatement &node) {
    node.expr->accept(*this);
    uint8_t size = 1;
    if (node.expr->type && node.expr->type->kind == parser::DataType::Kind::Struct) {
        // Ensure struct exists in map
        if (m_structs.contains(node.expr->type->struct_name)) {
            size = m_structs.at(node.expr->type->struct_name).total_size;
        }
    }
    emit_opcode(ir::OpCode::RET);
    emit_byte(size);
}

void IRGenerator::visit(const parser::VariableDeclaration &node) {
    if (node.init) {
        node.init->accept(*this);
    }
    uint16_t size = 1;
    if (node.type.kind == parser::DataType::Kind::Struct) {
        size = m_structs.at(node.type.struct_name).total_size;
    }
    define_var(node.name, size);
    if (node.init) {
        Symbol s = get_var_symbol(node.name);
        if (s.is_global) {
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(s.slot);
            emit_byte(s.size);
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)s.slot);
            emit_byte(s.size);
        }
    }
}

void IRGenerator::visit(const parser::ExpressionStatement &node) { node.expr->accept(*this); }

void IRGenerator::visit(const parser::IfStatement &node) {
    node.condition->accept(*this);
    auto jump_to_else = emit_jump(ir::OpCode::JZ);

    node.then_branch->accept(*this);
    auto jump_to_end = emit_jump(ir::OpCode::JMP);

    patch_jump(jump_to_else, m_program.bytecode.size());
    if (node.else_branch) {
        node.else_branch->accept(*this);
    }
    patch_jump(jump_to_end, m_program.bytecode.size());
}

void IRGenerator::visit(const parser::ForStatement &node) {
    if (node.init) node.init->accept(*this);

    size_t start_label = m_program.bytecode.size();
    JumpPlaceholder jump_to_exit{0};

    if (node.condition) {
        node.condition->accept(*this);
        jump_to_exit = emit_jump(ir::OpCode::JZ);
    }

    node.body->accept(*this);

    if (node.increment) {
        node.increment->accept(*this);
    }

    emit_opcode(ir::OpCode::JMP);
    emit_uint32((uint32_t)start_label);

    if (node.condition) {
        patch_jump(jump_to_exit, m_program.bytecode.size());
    }
}

void IRGenerator::visit(const parser::YieldStatement &node) { emit_opcode(ir::OpCode::YIELD); }

void IRGenerator::visit(const parser::IntegerLiteral &node) {
    if (node.type && node.type->kind == parser::DataType::Kind::I64) {
        emit_opcode(ir::OpCode::PUSH_I64);
        emit_int64(node.value);
    } else if (node.type && node.type->kind == parser::DataType::Kind::I16) {
        emit_opcode(ir::OpCode::PUSH_I16);
        emit_int16((int16_t)node.value);
    } else if (node.type && node.type->kind == parser::DataType::Kind::I8) {
        emit_opcode(ir::OpCode::PUSH_I8);
        emit_int8((int8_t)node.value);
    } else {
        emit_opcode(ir::OpCode::PUSH_I32);
        emit_int32((int32_t)node.value);
    }
}

void IRGenerator::visit(const parser::VariableExpression &node) {
    Symbol s = get_var_symbol(node.name);

    if (s.is_global) {
        emit_opcode(ir::OpCode::LOAD_GLOBAL);
        emit_uint16(s.slot);
        emit_byte(s.size);
    } else {
        emit_opcode(ir::OpCode::LOAD_VAR);
        emit_byte((uint8_t)s.slot);
        emit_byte(s.size);
    }
}

void IRGenerator::visit(const parser::AssignmentExpression &node) {
    node.value->accept(*this);  // Push value to store

    LValueResolver resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    uint8_t size = 1;
    if (node.lvalue && node.lvalue->type && node.lvalue->type->kind == parser::DataType::Kind::Struct) {
        size = m_structs.at(node.lvalue->type->struct_name).total_size;
    }

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(resolver.slot);
            emit_byte(size);
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_byte(size);
        }
    } else {
        emit_opcode(ir::OpCode::STORE_PTR_OFFSET);
        emit_int32(resolver.offset);
        emit_byte(size);
    }
}

void IRGenerator::visit(const parser::IncrementExpression &node) {
    node.lvalue->accept(*this);  // Push current value
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(1);
    emit_opcode(ir::OpCode::ADD);

    LValueResolver resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(resolver.slot);
            emit_byte(1);  // Scalar hardcoded
            emit_opcode(ir::OpCode::LOAD_GLOBAL);
            emit_uint16(resolver.slot);
            emit_byte(1);  // Scalar hardcoded
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_byte(1);  // Scalar hardcoded
            emit_opcode(ir::OpCode::LOAD_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_byte(1);  // Scalar hardcoded
        }
    } else {
        emit_opcode(ir::OpCode::STORE_PTR_OFFSET);
        emit_int32(resolver.offset);
        emit_byte(1);  // Scalar hardcoded
        // Reload for result
        node.lvalue->accept(*this);
    }
}

void IRGenerator::visit(const parser::DecrementExpression &node) {
    node.lvalue->accept(*this);
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(1);
    emit_opcode(ir::OpCode::SUB);

    LValueResolver resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(resolver.slot);
            emit_byte(1);  // Scalar hardcoded
            emit_opcode(ir::OpCode::LOAD_GLOBAL);
            emit_uint16(resolver.slot);
            emit_byte(1);  // Scalar hardcoded
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_byte(1);  // Scalar hardcoded
            emit_opcode(ir::OpCode::LOAD_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_byte(1);  // Scalar hardcoded
        }
    } else {
        emit_opcode(ir::OpCode::STORE_PTR_OFFSET);
        emit_int32(resolver.offset);
        emit_byte(1);  // Scalar hardcoded
        node.lvalue->accept(*this);
    }
}

void IRGenerator::visit(const parser::AwaitExpression &node) {
    node.expr->accept(*this);
    emit_opcode(ir::OpCode::AWAIT);
}

void IRGenerator::visit(const parser::SpawnExpression &node) {
    // Push arguments
    for (const auto &arg : node.call->args) {
        arg->accept(*this);
    }
    emit_opcode(ir::OpCode::SPAWN);
    if (node.call->name == "syscall") {
        emit_uint32(0xFFFFFFFF);
    } else {
        m_call_patches.push_back({m_program.bytecode.size(), node.call->name});
        emit_uint32(0);
    }
    uint8_t num_args = (uint8_t)node.call->args.size();
    if (!node.call->args.empty() && dynamic_cast<const parser::VarargExpression *>(node.call->args.back().get())) {
        num_args |= 0x80;
    }
    emit_byte(num_args);
}

void IRGenerator::visit(const parser::BinaryExpression &node) {
    node.left->accept(*this);
    node.right->accept(*this);
    switch (node.op) {
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
}

void IRGenerator::visit(const parser::StringLiteral &node) {
    emit_opcode(ir::OpCode::PUSH_STR);
    emit_uint32(get_string_id(node.value));
}

void IRGenerator::visit(const parser::FunctionCall &node) {
    uint8_t total_slots = 0;
    for (const auto &arg : node.args) {
        arg->accept(*this);
        // Calculate size of argument
        uint8_t size = 1;
        if (arg->type && arg->type->kind == parser::DataType::Kind::Struct) {
            size = m_structs.at(arg->type->struct_name).total_size;
        }
        total_slots += size;
    }

    // We pass total_slots as the number of args passed
    uint8_t num_args = total_slots;

    if (!node.args.empty() && dynamic_cast<const parser::VarargExpression *>(node.args.back().get())) {
        num_args |= 0x80;
    }
    if (node.name == "syscall") {
        emit_opcode(ir::OpCode::SYSCALL);
        emit_byte(num_args);
    } else {
        emit_opcode(ir::OpCode::CALL);
        m_call_patches.push_back({m_program.bytecode.size(), node.name});
        emit_uint32(0);
        emit_byte(num_args);
    }
}

void IRGenerator::visit(const parser::VarargExpression &node) { emit_opcode(ir::OpCode::PUSH_VARARGS); }

}  // namespace ether::ir_gen
