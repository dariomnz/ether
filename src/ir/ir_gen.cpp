#include "ir_gen.hpp"

#include <cstring>
#include <stdexcept>

namespace ether::ir_gen {

namespace {
struct DependencyTracker : public parser::ASTVisitor {
    std::unordered_set<std::string> reachable;
    const std::unordered_map<std::string, const parser::Function *> &all_funcs;

    DependencyTracker(const std::unordered_map<std::string, const parser::Function *> &funcs) : all_funcs(funcs) {}

    void trace(const std::string &name) {
        if (name == "syscall") return;
        if (reachable.contains(name)) return;
        reachable.insert(name);
        auto it = all_funcs.find(name);
        if (it != all_funcs.end()) {
            it->second->accept(*this);
        }
    }

    void visit(const parser::IntegerLiteral &) override {}
    void visit(const parser::StringLiteral &) override {}
    void visit(const parser::VariableExpression &) override {}
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
        if (node.value) node.value->accept(*this);
    }
    void visit(const parser::IncrementExpression &node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const parser::DecrementExpression &node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const parser::AwaitExpression &node) override {
        if (node.expr) node.expr->accept(*this);
    }
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
    m_call_patches.clear();
    m_reachable.clear();

    // First pass: collect all function definitions
    std::unordered_map<std::string, const parser::Function *> all_funcs;
    for (const auto &func : ast.functions) {
        all_funcs[func->name] = func.get();
    }

    // Trace dependencies starting from main
    DependencyTracker tracker(all_funcs);
    tracker.trace("main");
    m_reachable = std::move(tracker.reachable);

    // Initial pass for reachable function signatures
    for (const auto &name : m_reachable) {
        const auto *func = all_funcs.at(name);
        m_program.functions[name] = {0, (uint8_t)func->params.size(), 0};
    }

    // Register built-in native functions
    m_program.functions["syscall"] = {0xFFFFFFFF, 5, 0};

    // Second pass: generate code for reachable functions
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
        if (func->name == "main") m_program.main_addr = m_program.bytecode.size();
        func->accept(*this);
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
        emit_opcode(ir::OpCode::PUSH_INT);
        emit_int(0);
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
    } else {
        emit_opcode(ir::OpCode::PUSH_INT);
        emit_int(0);
    }
    define_var(node.name);
    emit_opcode(ir::OpCode::STORE_VAR);
    emit_byte(get_var_slot(node.name));
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
    emit_opcode(ir::OpCode::PUSH_INT);
    emit_int(node.value);
}

void IRGenerator::visit(const parser::VariableExpression &node) {
    emit_opcode(ir::OpCode::LOAD_VAR);
    emit_byte(get_var_slot(node.name));
}

void IRGenerator::visit(const parser::AssignmentExpression &node) {
    node.value->accept(*this);
    emit_opcode(ir::OpCode::STORE_VAR);
    emit_byte(get_var_slot(node.lvalue->name));
}

void IRGenerator::visit(const parser::IncrementExpression &node) {
    emit_opcode(ir::OpCode::LOAD_VAR);
    emit_byte(get_var_slot(node.lvalue->name));
    emit_opcode(ir::OpCode::PUSH_INT);
    emit_int(1);
    emit_opcode(ir::OpCode::ADD);
    emit_opcode(ir::OpCode::STORE_VAR);
    emit_byte(get_var_slot(node.lvalue->name));
    // Push result for expression consistency
    emit_opcode(ir::OpCode::LOAD_VAR);
    emit_byte(get_var_slot(node.lvalue->name));
}

void IRGenerator::visit(const parser::DecrementExpression &node) {
    emit_opcode(ir::OpCode::LOAD_VAR);
    emit_byte(get_var_slot(node.lvalue->name));
    emit_opcode(ir::OpCode::PUSH_INT);
    emit_int(1);
    emit_opcode(ir::OpCode::SUB);
    emit_opcode(ir::OpCode::STORE_VAR);
    emit_byte(get_var_slot(node.lvalue->name));
    // Push result for expression consistency
    emit_opcode(ir::OpCode::LOAD_VAR);
    emit_byte(get_var_slot(node.lvalue->name));
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
