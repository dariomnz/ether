#include "analyzer.hpp"

#include "common/error.hpp"

namespace ether::sema {

using namespace parser;

void Analyzer::analyze(Program &program) {
    push_scope();  // Global scope

    // Register built-ins
    m_functions["syscall"] = {DataType(DataType::Kind::Int), {}, true, "", 0, 0};

    // First pass: collect function signatures
    for (const auto &func : program.functions) {
        std::vector<DataType> param_types;
        for (const auto &param : func->params) {
            param_types.push_back(param.type);
        }
        m_functions[func->name] = {func->return_type, param_types, func->is_variadic,
                                   func->filename,    func->line,  func->column};
    }

    // Second pass: analyze everything
    program.accept(*this);

    pop_scope();
}

void Analyzer::visit(Program &node) {
    for (const auto &global : node.globals) {
        global->accept(*this);
    }
    for (const auto &func : node.functions) {
        func->accept(*this);
    }
}

void Analyzer::visit(Function &func) {
    push_scope();
    for (const auto &param : func.params) {
        define_variable(param.name, param.type, func.filename, func.line, func.column);
    }
    func.body->accept(*this);
    pop_scope();
}

void Analyzer::visit(Block &block) {
    push_scope();
    for (auto &stmt : block.statements) {
        stmt->accept(*this);
    }
    pop_scope();
}

void Analyzer::visit(ReturnStatement &node) { node.expr->accept(*this); }

void Analyzer::visit(VariableDeclaration &node) {
    if (node.init) {
        node.init->accept(*this);
        DataType init_type = m_current_type;
        if (!(init_type == node.type)) {
            // Allow assigning 0 to pointers
            bool is_null_ptr = (node.type.kind == DataType::Kind::Ptr && init_type.kind == DataType::Kind::Int);
            if (!is_null_ptr) {
                throw CompilerError("Type mismatch in variable declaration: expected " + node.type.to_string() +
                                        ", but got " + init_type.to_string(),
                                    node.filename, node.line, node.column, node.length);
            }
        }
    }
    define_variable(node.name, node.type, node.filename, node.line, node.column);
}

void Analyzer::visit(ExpressionStatement &node) { node.expr->accept(*this); }

void Analyzer::visit(IfStatement &node) {
    node.condition->accept(*this);
    node.then_branch->accept(*this);
    if (node.else_branch) node.else_branch->accept(*this);
}

void Analyzer::visit(ForStatement &node) {
    push_scope();
    if (node.init) node.init->accept(*this);
    if (node.condition) node.condition->accept(*this);
    if (node.increment) node.increment->accept(*this);
    node.body->accept(*this);
    pop_scope();
}

void Analyzer::visit(YieldStatement &node) {
    // Nothing to check for yield
}

void Analyzer::visit(IntegerLiteral &node) {
    m_current_type = DataType(DataType::Kind::Int);
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(StringLiteral &node) {
    m_current_type = DataType(DataType::Kind::String);
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(VariableExpression &node) {
    const Symbol *sym = lookup_symbol(node.name);
    if (!sym) {
        throw CompilerError("Undefined variable: " + node.name, node.filename, node.line, node.column, node.length);
    }
    node.decl_filename = sym->filename;
    node.decl_line = sym->line;
    node.decl_col = sym->col;
    m_current_type = sym->type;
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(BinaryExpression &node) {
    node.left->accept(*this);
    DataType left = m_current_type;
    node.right->accept(*this);
    DataType right = m_current_type;

    bool left_ok = left.kind == DataType::Kind::Int || left.kind == DataType::Kind::Ptr;
    bool right_ok = right.kind == DataType::Kind::Int || right.kind == DataType::Kind::Ptr;

    if (!left_ok || !right_ok) {
        throw CompilerError("Binary operations are only supported for integers and pointers", node.filename, node.line,
                            node.column, node.length);
    }
    m_current_type = DataType(DataType::Kind::Int);
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(FunctionCall &node) {
    if (m_functions.find(node.name) == m_functions.end()) {
        throw CompilerError("Undefined function: " + node.name, node.filename, node.line, node.column, node.length);
    }
    const auto &info = m_functions[node.name];
    if (info.is_variadic) {
        if (node.args.size() < info.param_types.size()) {
            throw CompilerError("Too few arguments for variadic function " + node.name, node.filename, node.line,
                                node.column, node.length);
        }
    } else {
        if (node.args.size() != info.param_types.size()) {
            throw CompilerError("Wrong number of arguments for " + node.name, node.filename, node.line, node.column,
                                node.length);
        }
    }
    node.decl_filename = info.filename;
    node.decl_line = info.line;
    node.decl_col = info.col;
    node.param_types = info.param_types;
    node.is_variadic = info.is_variadic;
    for (size_t i = 0; i < node.args.size(); ++i) {
        node.args[i]->accept(*this);
    }
    m_current_type = info.return_type;
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(VarargExpression &node) {
    // ellipsis can only appear in calls, and their type is effectively "multiple"
    // for now we just mark current type as int so it doesn't crash
    m_current_type = DataType(DataType::Kind::Int);
}

void Analyzer::visit(SpawnExpression &node) {
    node.call->accept(*this);
    // m_current_type is the return type of the function being spawned
    m_current_type = DataType(DataType::Kind::Coroutine, std::make_shared<DataType>(m_current_type));
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(AwaitExpression &node) {
    node.expr->accept(*this);
    DataType target_type = m_current_type;
    if (target_type.kind != DataType::Kind::Coroutine) {
        throw CompilerError("Semantic Error: 'await' expects a coroutine handle, but got " + target_type.to_string(),
                            node.filename, node.line, node.column, node.length);
    }
    if (target_type.inner) {
        m_current_type = *target_type.inner;
    } else {
        m_current_type = DataType(DataType::Kind::Int);  // Fallback
    }
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(AssignmentExpression &node) {
    node.value->accept(*this);
    DataType val_type = m_current_type;
    node.lvalue->accept(*this);
    DataType lval_type = m_current_type;

    if (!(val_type == lval_type)) {
        throw CompilerError(
            "Type mismatch in assignment: expected " + lval_type.to_string() + ", but got " + val_type.to_string(),
            node.filename, node.line, node.column, node.length);
    }
    m_current_type = lval_type;
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(IncrementExpression &node) {
    node.lvalue->accept(*this);
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(DecrementExpression &node) {
    node.lvalue->accept(*this);
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::push_scope() { m_scopes.emplace_back(); }

void Analyzer::pop_scope() { m_scopes.pop_back(); }

void Analyzer::define_variable(const std::string &name, DataType type, std::string filename, int line, int col) {
    bool is_global = (m_scopes.size() == 1);
    m_scopes.back().variables[name] = {type, std::move(filename), line, col, is_global, 0};
}

DataType Analyzer::lookup_variable(const std::string &name, std::string filename, int line, int col) {
    const Symbol *sym = lookup_symbol(name);
    if (sym) {
        return sym->type;
    }
    throw CompilerError("Undefined variable: " + name, std::move(filename), line, col, (int)name.size());
}

const Analyzer::Symbol *Analyzer::lookup_symbol(const std::string &name) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        auto var_it = it->variables.find(name);
        if (var_it != it->variables.end()) {
            return &var_it->second;
        }
    }
    return nullptr;
}

const Analyzer::FunctionInfo *Analyzer::lookup_function(const std::string &name) {
    auto it = m_functions.find(name);
    if (it != m_functions.end()) {
        return &it->second;
    }
    return nullptr;
}

}  // namespace ether::sema
