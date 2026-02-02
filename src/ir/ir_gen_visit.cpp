#include <stdexcept>

#include "ir/ir_gen.hpp"

namespace ether::ir_gen {

void IRGenerator::visit(const parser::StructDeclaration &node) {
    // Layout already collected
}
void IRGenerator::visit(const parser::Include &node) {}

void IRGenerator::visit(const parser::SizeofExpression &node) { emit_push_i32(get_type_size(node.target_type)); }

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
            emit_load_global(resolver.slot, size);
        } else {
            emit_load_var(resolver.slot, size);
        }
    } else {
        emit_load_ptr_offset(resolver.offset, size);
    }
}

void IRGenerator::visit(const parser::IndexExpression &node) {
    if (node.object->type && node.object->type->kind == parser::DataType::Kind::String) {
        node.object->accept(*this);
        node.index->accept(*this);
        emit_str_get();
        return;
    }

    // Load pointer
    node.object->accept(*this);

    // Load index
    node.index->accept(*this);

    // The pointer is now at stack[-2] and index at stack[-1]
    // We need to compute: ptr + (index * element_size)
    // For now, we assume element size is 16 bytes (1 slot)
    // If the inner type is a struct, we need to multiply by its size

    uint16_t element_size = 1;
    if (node.object->type) {
        if (node.object->type->kind == parser::DataType::Kind::Ptr && node.object->type->inner) {
            if (node.object->type->inner->kind == parser::DataType::Kind::Struct) {
                element_size = m_structs.at(node.object->type->inner->struct_name).total_size;
            }
        } else if (node.object->type->kind == parser::DataType::Kind::Array && node.object->type->inner) {
            if (node.object->type->inner->kind == parser::DataType::Kind::Struct) {
                element_size = m_structs.at(node.object->type->inner->struct_name).total_size;
            }
        }
    }

    // Multiply by 16 to convert slot offset to byte offset
    emit_push_i32(16 * element_size);  // sizeof(Value)
    emit_mul();

    // Add byte offset to pointer
    emit_add();

    // Determine size of element being loaded
    uint8_t load_size = 1;
    if (node.type && node.type->kind == parser::DataType::Kind::Struct) {
        load_size = m_structs.at(node.type->struct_name).total_size;
    }

    // Load from the computed address (offset 0)
    emit_load_ptr_offset(0, load_size);
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
        emit_push_i32(0);
        emit_ret(1);
    }

    // Record how many slots this function needs
    std::string func_name = func.name;
    if (!func.struct_name.empty()) {
        func_name = func.struct_name + "::" + func_name;
    }
    m_program.functions[func_name].num_slots = m_scopes.back().next_slot;
    m_scopes.pop_back();
}

void IRGenerator::visit(const parser::Block &block) {
    for (const auto &stmt : block.statements) {
        stmt->accept(*this);
    }
}

void IRGenerator::visit(const parser::ReturnStatement &node) {
    // If we are returning an array that is a direct variable, we need to load its content.
    // The default visitor for a variable expression of array type is to push its address (LEA).
    if (node.expr->type && node.expr->type->kind == parser::DataType::Kind::Array) {
        if (auto* var_expr = dynamic_cast<const parser::VariableExpression*>(node.expr.get())) {
            Symbol s = get_var_symbol(var_expr->name);
            uint8_t size = get_type_size(*var_expr->type) / 16;
            if (s.is_global) {
                emit_load_global(s.slot, size);
            } else {
                emit_load_var(s.slot, size);
            }
        } else {
            node.expr->accept(*this);
        }
    } else {
        node.expr->accept(*this);
    }

    uint8_t size = 1;
    if (node.expr->type) {
        if (node.expr->type->kind == parser::DataType::Kind::Struct) {
            // Ensure struct exists in map
            if (m_structs.contains(node.expr->type->struct_name)) {
                size = m_structs.at(node.expr->type->struct_name).total_size;
            }
        } else if (node.expr->type->kind == parser::DataType::Kind::Array) {
            size = 1;
        }
    }
    emit_ret(size);
}

void IRGenerator::visit(const parser::VariableDeclaration &node) {
    uint16_t size = 1;
    if (node.type.kind == parser::DataType::Kind::Struct) {
        size = m_structs.at(node.type.struct_name).total_size;
    }
    define_var(node.name, size);

    if (node.init) {
        node.init->accept(*this);
    } else if (node.type.kind == parser::DataType::Kind::Array) {
        uint32_t slots = (uint32_t)(get_type_size(node.type) / 16);
        emit_arr_alloc(slots);
    }

    if (node.init || node.type.kind == parser::DataType::Kind::Array) {
        Symbol s = get_var_symbol(node.name);
        if (s.is_global) {
            emit_store_global(s.slot, s.size);
        } else {
            emit_store_var(s.slot, s.size);
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

    emit_jump(ir::OpCode::JMP, (uint32_t)start_label);

    if (node.condition) {
        patch_jump(jump_to_exit, m_program.bytecode.size());
    }
}

void IRGenerator::visit(const parser::YieldStatement &node) { emit_yield(); }

void IRGenerator::visit(const parser::IntegerLiteral &node) {
    if (node.type && node.type->kind == parser::DataType::Kind::I64) {
        emit_push_i64(node.value);
    } else if (node.type && node.type->kind == parser::DataType::Kind::I16) {
        emit_push_i16((int16_t)node.value);
    } else if (node.type && node.type->kind == parser::DataType::Kind::I8) {
        emit_push_i8((int8_t)node.value);
    } else {
        emit_push_i32((int32_t)node.value);
    }
}

void IRGenerator::visit(const parser::FloatLiteral &node) {
    if (node.is_f32) {
        emit_push_f32((float)node.value);
    } else {
        emit_push_f64(node.value);
    }
}

void IRGenerator::visit(const parser::VariableExpression &node) {
    Symbol s = get_var_symbol(node.name);

    if (s.is_global) {
        emit_load_global(s.slot, s.size);
    } else {
        emit_load_var(s.slot, s.size);
    }
}

void IRGenerator::visit(const parser::AssignmentExpression &node) {
    auto *idx_expr = dynamic_cast<const parser::IndexExpression *>(node.lvalue.get());

    if (idx_expr && idx_expr->object->type && idx_expr->object->type->kind == parser::DataType::Kind::String) {
        node.value->accept(*this);
        idx_expr->object->accept(*this);
        idx_expr->index->accept(*this);
        emit_str_set();
    } else if (idx_expr && idx_expr->object->type && idx_expr->object->type->kind == parser::DataType::Kind::Array) {
        node.value->accept(*this);

        idx_expr->object->accept(*this);

        idx_expr->index->accept(*this);

        uint16_t element_size = 1;
        if (idx_expr->object->type->inner && idx_expr->object->type->inner->kind == parser::DataType::Kind::Struct) {
            element_size = m_structs.at(idx_expr->object->type->inner->struct_name).total_size;
        }

        emit_push_i32(16 * element_size);
        emit_mul();
        emit_add();

        uint8_t store_size = 1;
        if (idx_expr->type && idx_expr->type->kind == parser::DataType::Kind::Struct) {
            store_size = m_structs.at(idx_expr->type->struct_name).total_size;
        }

        emit_store_ptr_offset(0, store_size);
    } else {
        node.value->accept(*this);

        LValueResolver resolver;
        resolver.gen = this;
        node.lvalue->accept(resolver);

        uint8_t size = 1;
        if (node.lvalue && node.lvalue->type && node.lvalue->type->kind == parser::DataType::Kind::Struct) {
            size = m_structs.at(node.lvalue->type->struct_name).total_size;
        }

        if (resolver.kind == LValueResolver::Stack) {
            if (resolver.is_global) {
                emit_store_global(resolver.slot, size);
            } else {
                emit_store_var(resolver.slot, size);
            }
        } else {
            emit_store_ptr_offset(resolver.offset, size);
        }
    }
}

void IRGenerator::visit(const parser::IncrementExpression &node) {
    node.lvalue->accept(*this);  // Push current value
    emit_push_i32(1);
    emit_add();

    LValueResolver resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_store_global(resolver.slot, 1);
            emit_load_global(resolver.slot, 1);
        } else {
            emit_store_var(resolver.slot, 1);
            emit_load_var(resolver.slot, 1);
        }
    } else {
        emit_store_ptr_offset(resolver.offset, 1);
        // Reload for result
        node.lvalue->accept(*this);
    }
}

void IRGenerator::visit(const parser::DecrementExpression &node) {
    node.lvalue->accept(*this);
    emit_push_i32(1);
    emit_sub();

    LValueResolver resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_store_global(resolver.slot, 1);
            emit_load_global(resolver.slot, 1);
        } else {
            emit_store_var(resolver.slot, 1);
            emit_load_var(resolver.slot, 1);
        }
    } else {
        emit_store_ptr_offset(resolver.offset, 1);
        node.lvalue->accept(*this);
    }
}

void IRGenerator::visit(const parser::AwaitExpression &node) {
    node.expr->accept(*this);
    emit_await();
}

void IRGenerator::visit(const parser::SpawnExpression &node) {
    // Push arguments
    for (const auto &arg : node.call->args) {
        arg->accept(*this);
    }

    uint8_t num_args = (uint8_t)node.call->args.size();
    if (!node.call->args.empty() && dynamic_cast<const parser::VarargExpression *>(node.call->args.back().get())) {
        num_args |= 0x80;
    }

    if (node.call->name == "syscall") {
        emit_spawn(0xFFFFFFFF, num_args);
    } else {
        size_t patch_pos = m_program.bytecode.size() + 1;
        m_call_patches.push_back({patch_pos, node.call->name});
        emit_spawn(0, num_args);
    }
}

void IRGenerator::visit(const parser::BinaryExpression &node) {
    node.left->accept(*this);
    node.right->accept(*this);

    bool is_float = false;
    if (node.left && node.left->type && node.left->type->is_float()) {
        is_float = true;
    }

    switch (node.op) {
        case parser::BinaryExpression::Op::Add:
            if (is_float)
                emit_add_f();
            else
                emit_add();
            break;
        case parser::BinaryExpression::Op::Sub:
            if (is_float)
                emit_sub_f();
            else
                emit_sub();
            break;
        case parser::BinaryExpression::Op::Mul:
            if (is_float)
                emit_mul_f();
            else
                emit_mul();
            break;
        case parser::BinaryExpression::Op::Div:
            if (is_float)
                emit_div_f();
            else
                emit_div();
            break;
        case parser::BinaryExpression::Op::Leq:
            if (is_float)
                emit_le_f();
            else
                emit_le();
            break;
        case parser::BinaryExpression::Op::Less:
            if (is_float)
                emit_lt_f();
            else
                emit_lt();
            break;
        case parser::BinaryExpression::Op::Eq:
            if (is_float)
                emit_eq_f();
            else
                emit_eq();
            break;
        case parser::BinaryExpression::Op::Gt:
            if (is_float)
                emit_gt_f();
            else
                emit_gt();
            break;
        case parser::BinaryExpression::Op::Geq:
            if (is_float)
                emit_ge_f();
            else
                emit_ge();
            break;
        default:
            throw std::runtime_error("Unsupported binary op in refactored IR Gen");
    }
}

void IRGenerator::visit(const parser::StringLiteral &node) { emit_push_str(get_string_id(node.value)); }

void IRGenerator::visit(const parser::FunctionCall &node) {
    uint8_t total_slots = 0;

    // If this is a method call, push 'this' pointer first
    if (node.object) {
        // Check if the object is already a pointer type
        bool object_is_pointer = false;
        if (node.object->type && node.object->type->kind == parser::DataType::Kind::Ptr) {
            object_is_pointer = true;
        }

        // We need to push the address of the object (or the pointer itself if already a pointer)
        if (auto *var_expr = dynamic_cast<const parser::VariableExpression *>(node.object.get())) {
            Symbol s = get_var_symbol(var_expr->name);

            if (object_is_pointer) {
                // Object is a pointer variable, load its value
                if (s.is_global) {
                    emit_load_global(s.slot, s.size);
                } else {
                    emit_load_var(s.slot, s.size);
                }
            } else {
                // Object is a value, take its address
                if (s.is_global) {
                    emit_lea_global(s.slot);
                } else {
                    emit_lea_stack(s.slot);
                }
            }
            total_slots += 1;  // 'this' pointer is 1 slot
        } else {
            // For more complex expressions, evaluate and assume it's already a pointer
            node.object->accept(*this);
            total_slots += 1;
        }
    }

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
        emit_syscall(num_args);
    } else {
        size_t patch_pos = m_program.bytecode.size() + 1;
        m_call_patches.push_back({patch_pos, node.name});
        emit_call(0, num_args);
    }
}

void IRGenerator::visit(const parser::VarargExpression &node) { emit_push_varargs(); }

}  // namespace ether::ir_gen
