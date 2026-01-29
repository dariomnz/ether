#include "ir_gen.hpp"

#include <cstring>
#include <stdexcept>

namespace ether::ir_gen {

namespace {
struct DependencyTracker : public parser::ASTVisitor {
    std::unordered_set<std::string> reachable;
    const std::unordered_map<std::string, const parser::Function *> &all_funcs;
    const std::unordered_map<std::string, const parser::VariableDeclaration *> &all_globals;

    DependencyTracker(const std::unordered_map<std::string, const parser::Function *> &funcs,
                      const std::unordered_map<std::string, const parser::VariableDeclaration *> &globals)
        : all_funcs(funcs), all_globals(globals) {}

    void trace(const std::string &name) {
        if (name == "syscall") return;
        if (reachable.contains(name)) return;
        reachable.insert(name);

        auto it_f = all_funcs.find(name);
        if (it_f != all_funcs.end()) {
            it_f->second->accept(*this);
        }

        auto it_g = all_globals.find(name);
        if (it_g != all_globals.end()) {
            if (it_g->second->init) {
                it_g->second->init->accept(*this);
            }
        }
    }

    void visit(const parser::IntegerLiteral &) override {}
    void visit(const parser::StringLiteral &) override {}
    void visit(const parser::VariableExpression &node) override { trace(node.name); }
    void visit(const parser::FunctionCall &node) override {
        trace(node.name);
        for (const auto &arg : node.args) arg->accept(*this);
    }
    void visit(const parser::VarargExpression &) override {}
    void visit(const parser::BinaryExpression &node) override {
        if (node.left) node.left->accept(*this);
        if (node.right) node.right->accept(*this);
    }
    void visit(const parser::Block &node) override {
        for (const auto &stmt : node.statements) stmt->accept(*this);
    }
    void visit(const parser::IfStatement &node) override {
        if (node.condition) node.condition->accept(*this);
        if (node.then_branch) node.then_branch->accept(*this);
        if (node.else_branch) node.else_branch->accept(*this);
    }
    void visit(const parser::ReturnStatement &node) override {
        if (node.expr) node.expr->accept(*this);
    }
    void visit(const parser::ExpressionStatement &node) override {
        if (node.expr) node.expr->accept(*this);
    }
    void visit(const parser::YieldStatement &) override {}
    void visit(const parser::SpawnExpression &node) override {
        if (node.call) node.call->accept(*this);
    }
    void visit(const parser::AssignmentExpression &node) override {
        node.lvalue->accept(*this);
        if (node.value) node.value->accept(*this);
    }
    void visit(const parser::IncrementExpression &node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const parser::DecrementExpression &node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const parser::MemberAccessExpression &node) override {
        if (node.object) node.object->accept(*this);
    }
    void visit(const parser::StructDeclaration &node) override {}
    void visit(const parser::AwaitExpression &node) override {
        if (node.expr) node.expr->accept(*this);
    }
    void visit(const parser::SizeofExpression &) override {}
    void visit(const parser::ForStatement &node) override {
        if (node.init) node.init->accept(*this);
        if (node.condition) node.condition->accept(*this);
        if (node.increment) node.increment->accept(*this);
        if (node.body) node.body->accept(*this);
    }
    void visit(const parser::VariableDeclaration &node) override {
        if (node.init) node.init->accept(*this);
    }
    void visit(const parser::Function &node) override {
        if (node.body) node.body->accept(*this);
    }
    void visit(const parser::Program &) override {}
};
}  // namespace

ir::IRProgram IRGenerator::generate(const parser::Program &ast) {
    m_program.bytecode.clear();
    m_program.string_pool.clear();
    m_program.functions.clear();
    m_program.addr_to_info.clear();
    m_call_patches.clear();
    m_reachable.clear();
    m_scopes.clear();

    // 1. Dependency Tracking
    std::unordered_map<std::string, const parser::Function *> all_funcs;
    for (const auto &func : ast.functions) {
        all_funcs[func->name] = func.get();
    }
    std::unordered_map<std::string, const parser::VariableDeclaration *> all_globals_map;
    for (const auto &global : ast.globals) {
        all_globals_map[global->name] = global.get();
    }

    DependencyTracker tracker(all_funcs, all_globals_map);
    tracker.trace("main");
    m_reachable = std::move(tracker.reachable);

    // 2. Global Scope setup
    m_scopes.push_back({{}, 0, true});

    // Define ONLY reachable globals
    for (const auto &global : ast.globals) {
        if (m_reachable.contains(global->name)) {
            uint16_t size = 1;
            if (global->type.kind == parser::DataType::Kind::Struct) {
                size = m_structs.at(global->type.struct_name).total_size;
            }
            define_var(global->name, size);
        }
    }
    m_program.num_globals = m_scopes[0].next_slot;

    for (const auto &name : m_reachable) {
        auto it = all_funcs.find(name);
        if (it != all_funcs.end()) {
            const auto *func = it->second;
            m_program.functions[name] = {0, (uint8_t)func->params.size(), 0};
        }
    }
    m_program.functions["syscall"] = {0xFFFFFFFF, 0, 0};

    // 3. Collect struct layouts
    for (const auto &str : ast.structs) {
        StructInfo info;
        uint16_t offset = 0;
        for (const auto &member : str->members) {
            info.member_offsets[member.name] = (uint8_t)offset;
            uint16_t member_size = 1;
            if (member.type.kind == parser::DataType::Kind::Struct) {
                auto it = m_structs.find(member.type.struct_name);
                if (it != m_structs.end()) {
                    member_size = it->second.total_size;
                }
            }
            offset += member_size;
        }
        info.total_size = offset;
        m_structs[str->name] = info;
    }

    // 4. Entry point / Global initialization
    m_program.main_addr = 0;
    for (const auto &global : ast.globals) {
        if (m_reachable.contains(global->name) && global->init) {
            global->init->accept(*this);
            Symbol s = get_var_symbol(global->name);
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(s.slot);
        }
    }

    // Call main and halt
    emit_opcode(ir::OpCode::CALL);
    m_call_patches.push_back({m_program.bytecode.size(), "main"});
    emit_uint32(0);
    emit_byte(0);  // main takes 0 args
    emit_opcode(ir::OpCode::HALT);

    // 5. Generate functions
    ast.accept(*this);

    emit_opcode(ir::OpCode::HALT);

    for (const auto &patch : m_call_patches) {
        uint32_t addr = (uint32_t)m_program.functions.at(patch.func_name).entry_addr;
        std::memcpy(&m_program.bytecode[patch.pos], &addr, 4);
    }

    for (const auto &[name, info] : m_program.functions) {
        m_program.addr_to_info[info.entry_addr] = info;
    }

    return std::move(m_program);
}

void IRGenerator::visit(const parser::Program &node) {
    for (const auto &func : node.functions) {
        if (!m_reachable.contains(func->name)) continue;
        m_program.functions[func->name].entry_addr = m_program.bytecode.size();
        func->accept(*this);
    }
    for (const auto &str : node.structs) {
        str->accept(*this);
    }
}

void IRGenerator::visit(const parser::StructDeclaration &node) {
    // Layout already collected
}

uint32_t IRGenerator::get_type_size(const parser::DataType &type) {
    uint32_t num_slots = 1;
    if (type.kind == parser::DataType::Kind::Struct) {
        num_slots = m_structs.at(type.struct_name).total_size;
    }
    return num_slots * 16;
}

void IRGenerator::visit(const parser::SizeofExpression &node) {
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(get_type_size(node.target_type));
}

void IRGenerator::visit(const parser::MemberAccessExpression &node) {
    struct LValueResolver : parser::ASTVisitor {
        IRGenerator *gen;
        enum Kind { Stack, Heap } kind = Stack;
        uint16_t slot = 0;
        bool is_global = false;
        uint8_t offset = 0;

        void visit(const parser::VariableExpression &v) override {
            auto s = gen->get_var_symbol(v.name);
            kind = Stack;
            slot = s.slot;
            is_global = s.is_global;
        }

        void visit(const parser::MemberAccessExpression &m) override {
            m.object->accept(*this);
            std::string struct_name;
            bool is_ptr = false;
            if (m.object->type->kind == parser::DataType::Kind::Ptr) {
                struct_name = m.object->type->inner->struct_name;
                is_ptr = true;
            } else {
                struct_name = m.object->type->struct_name;
            }
            uint8_t m_offset = gen->m_structs.at(struct_name).member_offsets.at(m.member_name);

            if (kind == Stack) {
                if (is_ptr) {
                    if (is_global) {
                        gen->emit_opcode(ir::OpCode::LOAD_GLOBAL);
                        gen->emit_uint16(slot);
                    } else {
                        gen->emit_opcode(ir::OpCode::LOAD_VAR);
                        gen->emit_byte((uint8_t)slot);
                    }
                    kind = Heap;
                    offset = m_offset;
                } else {
                    slot += m_offset;
                }
            } else {
                if (is_ptr) {
                    gen->emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
                    gen->emit_byte(offset);
                    offset = m_offset;
                } else {
                    offset += m_offset;
                }
            }
        }
    } resolver;
    resolver.gen = this;
    node.accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::LOAD_GLOBAL);
            emit_uint16(resolver.slot);
        } else {
            emit_opcode(ir::OpCode::LOAD_VAR);
            emit_byte((uint8_t)resolver.slot);
        }
    } else {
        emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
        emit_byte(resolver.offset);
    }
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
        struct RetCheck : parser::ASTVisitor {
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
        emit_opcode(ir::OpCode::PUSH_I32);
        emit_int32(0);
        emit_opcode(ir::OpCode::RET);
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
    emit_opcode(ir::OpCode::RET);
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
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)s.slot);
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
    } else {
        emit_opcode(ir::OpCode::LOAD_VAR);
        emit_byte((uint8_t)s.slot);
    }
}

void IRGenerator::visit(const parser::AssignmentExpression &node) {
    node.value->accept(*this);  // Push value to store

    struct LValueResolver : parser::ASTVisitor {
        IRGenerator *gen;
        enum Kind { Stack, Heap } kind = Stack;
        uint16_t slot = 0;
        bool is_global = false;
        uint8_t offset = 0;

        void visit(const parser::VariableExpression &v) override {
            auto s = gen->get_var_symbol(v.name);
            kind = Stack;
            slot = s.slot;
            is_global = s.is_global;
        }

        void visit(const parser::MemberAccessExpression &m) override {
            m.object->accept(*this);
            std::string struct_name;
            bool is_ptr = false;
            if (m.object->type->kind == parser::DataType::Kind::Ptr) {
                struct_name = m.object->type->inner->struct_name;
                is_ptr = true;
            } else {
                struct_name = m.object->type->struct_name;
            }
            uint8_t m_offset = gen->m_structs.at(struct_name).member_offsets.at(m.member_name);

            if (kind == Stack) {
                if (is_ptr) {
                    if (is_global) {
                        gen->emit_opcode(ir::OpCode::LOAD_GLOBAL);
                        gen->emit_uint16(slot);
                    } else {
                        gen->emit_opcode(ir::OpCode::LOAD_VAR);
                        gen->emit_byte((uint8_t)slot);
                    }
                    kind = Heap;
                    offset = m_offset;
                } else {
                    slot += m_offset;
                }
            } else {
                if (is_ptr) {
                    gen->emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
                    gen->emit_byte(offset);
                    offset = m_offset;
                } else {
                    offset += m_offset;
                }
            }
        }
    } resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(resolver.slot);
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)resolver.slot);
        }
    } else {
        emit_opcode(ir::OpCode::STORE_PTR_OFFSET);
        emit_byte(resolver.offset);
    }
}

void IRGenerator::visit(const parser::IncrementExpression &node) {
    node.lvalue->accept(*this);  // Push current value
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(1);
    emit_opcode(ir::OpCode::ADD);

    struct LValueResolver : parser::ASTVisitor {
        IRGenerator *gen;
        enum Kind { Stack, Heap } kind = Stack;
        uint16_t slot = 0;
        bool is_global = false;
        uint8_t offset = 0;

        void visit(const parser::VariableExpression &v) override {
            auto s = gen->get_var_symbol(v.name);
            kind = Stack;
            slot = s.slot;
            is_global = s.is_global;
        }

        void visit(const parser::MemberAccessExpression &m) override {
            m.object->accept(*this);
            std::string struct_name;
            bool is_ptr = false;
            if (m.object->type->kind == parser::DataType::Kind::Ptr) {
                struct_name = m.object->type->inner->struct_name;
                is_ptr = true;
            } else {
                struct_name = m.object->type->struct_name;
            }
            uint8_t m_offset = gen->m_structs.at(struct_name).member_offsets.at(m.member_name);

            if (kind == Stack) {
                if (is_ptr) {
                    if (is_global) {
                        gen->emit_opcode(ir::OpCode::LOAD_GLOBAL);
                        gen->emit_uint16(slot);
                    } else {
                        gen->emit_opcode(ir::OpCode::LOAD_VAR);
                        gen->emit_byte((uint8_t)slot);
                    }
                    kind = Heap;
                    offset = m_offset;
                } else {
                    slot += m_offset;
                }
            } else {
                if (is_ptr) {
                    gen->emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
                    gen->emit_byte(offset);
                    offset = m_offset;
                } else {
                    offset += m_offset;
                }
            }
        }
    } resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(resolver.slot);
            emit_opcode(ir::OpCode::LOAD_GLOBAL);
            emit_uint16(resolver.slot);
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_opcode(ir::OpCode::LOAD_VAR);
            emit_byte((uint8_t)resolver.slot);
        }
    } else {
        emit_opcode(ir::OpCode::STORE_PTR_OFFSET);
        emit_byte(resolver.offset);
        // Reload for result
        node.lvalue->accept(*this);
    }
}

void IRGenerator::visit(const parser::DecrementExpression &node) {
    node.lvalue->accept(*this);
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(1);
    emit_opcode(ir::OpCode::SUB);

    struct LValueResolver : parser::ASTVisitor {
        IRGenerator *gen;
        enum Kind { Stack, Heap } kind = Stack;
        uint16_t slot = 0;
        bool is_global = false;
        uint8_t offset = 0;

        void visit(const parser::VariableExpression &v) override {
            auto s = gen->get_var_symbol(v.name);
            kind = Stack;
            slot = s.slot;
            is_global = s.is_global;
        }

        void visit(const parser::MemberAccessExpression &m) override {
            m.object->accept(*this);
            std::string struct_name;
            bool is_ptr = false;
            if (m.object->type->kind == parser::DataType::Kind::Ptr) {
                struct_name = m.object->type->inner->struct_name;
                is_ptr = true;
            } else {
                struct_name = m.object->type->struct_name;
            }
            uint8_t m_offset = gen->m_structs.at(struct_name).member_offsets.at(m.member_name);

            if (kind == Stack) {
                if (is_ptr) {
                    if (is_global) {
                        gen->emit_opcode(ir::OpCode::LOAD_GLOBAL);
                        gen->emit_uint16(slot);
                    } else {
                        gen->emit_opcode(ir::OpCode::LOAD_VAR);
                        gen->emit_byte((uint8_t)slot);
                    }
                    kind = Heap;
                    offset = m_offset;
                } else {
                    slot += m_offset;
                }
            } else {
                if (is_ptr) {
                    gen->emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
                    gen->emit_byte(offset);
                    offset = m_offset;
                } else {
                    offset += m_offset;
                }
            }
        }
    } resolver;
    resolver.gen = this;
    node.lvalue->accept(resolver);

    if (resolver.kind == LValueResolver::Stack) {
        if (resolver.is_global) {
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(resolver.slot);
            emit_opcode(ir::OpCode::LOAD_GLOBAL);
            emit_uint16(resolver.slot);
        } else {
            emit_opcode(ir::OpCode::STORE_VAR);
            emit_byte((uint8_t)resolver.slot);
            emit_opcode(ir::OpCode::LOAD_VAR);
            emit_byte((uint8_t)resolver.slot);
        }
    } else {
        emit_opcode(ir::OpCode::STORE_PTR_OFFSET);
        emit_byte(resolver.offset);
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
    for (const auto &arg : node.args) {
        arg->accept(*this);
    }
    uint8_t num_args = (uint8_t)node.args.size();
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

void IRGenerator::emit_int64(int64_t val) {
    uint8_t bytes[8];
    std::memcpy(bytes, &val, 8);
    for (int i = 0; i < 8; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_int32(int32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, 4);
    for (int i = 0; i < 4; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_int16(int16_t val) {
    uint8_t bytes[2];
    std::memcpy(bytes, &val, 2);
    for (int i = 0; i < 2; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_int8(int8_t val) { emit_byte(static_cast<uint8_t>(val)); }

void IRGenerator::emit_uint32(uint32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, 4);
    for (int i = 0; i < 4; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_uint16(uint16_t val) {
    uint8_t bytes[2];
    std::memcpy(bytes, &val, 2);
    for (int i = 0; i < 2; ++i) emit_byte(bytes[i]);
}

uint32_t IRGenerator::get_string_id(const std::string &str) {
    for (uint32_t i = 0; i < m_program.string_pool.size(); ++i) {
        if (m_program.string_pool[i] == str) return i;
    }
    m_program.string_pool.push_back(str);
    return (uint32_t)m_program.string_pool.size() - 1;
}

IRGenerator::Symbol IRGenerator::get_var_symbol(const std::string &name) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->variables.contains(name)) return it->variables.at(name);
    }
    throw std::runtime_error("Undefined variable: " + name);
}

void IRGenerator::define_var(const std::string &name, uint16_t size) {
    if (m_scopes.empty()) throw std::runtime_error("Variable defined outside scope");
    auto &scope = m_scopes.back();
    uint16_t slot = scope.next_slot;
    scope.next_slot += size;
    scope.variables[name] = {slot, scope.is_global};
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
