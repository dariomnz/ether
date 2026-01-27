#include "analyzer.hpp"

#include "common/error.hpp"

namespace ether::sema {

using namespace parser;

void Analyzer::analyze(Program &program) {
    // Register built-ins
    // printf is special because it's variadic, for now let's just allow it
    m_functions["printf"] = {DataType(DataType::Kind::Int), {}, "", 0, 0};
    m_functions["write"] = {
        DataType(DataType::Kind::Int), {DataType(DataType::Kind::Int), DataType(DataType::Kind::Int)}, "", 0, 0};

    // First pass: collect function signatures
    for (const auto &func : program.functions) {
        std::vector<DataType> param_types;
        for (const auto &param : func->params) {
            param_types.push_back(param.type);
        }
        m_functions[func->name] = {func->return_type, param_types, func->filename, func->line, func->column};
    }

    // Second pass: analyze bodies
    for (const auto &func : program.functions) {
        visit_function(*func);
    }
}

void Analyzer::visit_function(Function &func) {
    push_scope();
    for (const auto &param : func.params) {
        define_variable(param.name, param.type, func.filename, func.line, func.column);
    }
    visit_block(*func.body);
    pop_scope();
}

void Analyzer::visit_block(Block &block) {
    push_scope();
    for (auto &stmt : block.statements) {
        visit_statement(*stmt);
    }
    pop_scope();
}

void Analyzer::visit_statement(Statement &stmt) {
    if (auto ret = dynamic_cast<ReturnStatement *>(&stmt)) {
        visit_expression(*ret->expr);
    } else if (auto decl = dynamic_cast<VariableDeclaration *>(&stmt)) {
        if (decl->init) {
            DataType init_type = visit_expression(*decl->init);
            if (!(init_type == decl->type)) {
                throw CompilerError("Type mismatch in variable declaration", decl->filename, decl->line, decl->column);
            }
        }
        define_variable(decl->name, decl->type, decl->filename, decl->line, decl->column);
    } else if (auto expr_stmt = dynamic_cast<ExpressionStatement *>(&stmt)) {
        visit_expression(*expr_stmt->expr);
    } else if (auto if_stmt = dynamic_cast<IfStatement *>(&stmt)) {
        visit_expression(*if_stmt->condition);
        visit_block(*if_stmt->then_branch);
        if (if_stmt->else_branch) visit_block(*if_stmt->else_branch);
    } else if (auto for_stmt = dynamic_cast<ForStatement *>(&stmt)) {
        push_scope();
        if (for_stmt->init) visit_statement(*for_stmt->init);
        if (for_stmt->condition) visit_expression(*for_stmt->condition);
        if (for_stmt->increment) visit_expression(*for_stmt->increment);
        visit_block(*for_stmt->body);
        pop_scope();
    } else if (auto yield_stmt = dynamic_cast<YieldStatement *>(&stmt)) {
        // Nothing to check for yield
    }
}

DataType Analyzer::visit_expression(Expression &expr) {
    DataType result(DataType::Kind::Void);

    if (auto lit = dynamic_cast<IntegerLiteral *>(&expr)) {
        result = DataType(DataType::Kind::Int);
    } else if (auto str = dynamic_cast<StringLiteral *>(&expr)) {
        result = DataType(DataType::Kind::Int);  // strings are ints for now
    } else if (auto var = dynamic_cast<VariableExpression *>(&expr)) {
        const Symbol *sym = lookup_symbol(var->name);
        if (!sym) {
            throw CompilerError("Undefined variable: " + var->name, var->filename, var->line, var->column);
        }
        var->decl_filename = sym->filename;
        var->decl_line = sym->line;
        var->decl_col = sym->col;
        result = sym->type;
    } else if (auto bin = dynamic_cast<BinaryExpression *>(&expr)) {
        DataType left = visit_expression(*bin->left);
        DataType right = visit_expression(*bin->right);
        if (!(left == DataType(DataType::Kind::Int)) || !(right == DataType(DataType::Kind::Int))) {
            throw CompilerError("Binary operations are only supported for integers", bin->filename, bin->line,
                                bin->column);
        }
        result = DataType(DataType::Kind::Int);
    } else if (auto call = dynamic_cast<FunctionCall *>(&expr)) {
        if (m_functions.find(call->name) == m_functions.end()) {
            throw CompilerError("Undefined function: " + call->name, call->filename, call->line, call->column);
        }
        const auto &info = m_functions[call->name];
        if (call->name != "printf" && call->args.size() != info.param_types.size()) {
            throw CompilerError("Wrong number of arguments for " + call->name, call->filename, call->line,
                                call->column);
        }
        call->decl_filename = info.filename;
        call->decl_line = info.line;
        call->decl_col = info.col;
        for (size_t i = 0; i < call->args.size(); ++i) {
            visit_expression(*call->args[i]);
        }
        result = info.return_type;
    } else if (auto spawn = dynamic_cast<SpawnExpression *>(&expr)) {
        visit_expression(*spawn->call);
        result = DataType(DataType::Kind::Coroutine);
    } else if (auto await_expr = dynamic_cast<AwaitExpression *>(&expr)) {
        DataType target_type = visit_expression(*await_expr->expr);
        if (!(target_type == DataType(DataType::Kind::Coroutine))) {
            throw CompilerError("Semantic Error: 'await' expects a coroutine handle, but got another type.",
                                await_expr->filename, await_expr->line, await_expr->column);
        }
        result = DataType(DataType::Kind::Int);
    } else if (auto assign = dynamic_cast<AssignmentExpression *>(&expr)) {
        DataType val_type = visit_expression(*assign->value);
        DataType lval_type = visit_expression(*assign->lvalue);
        if (!(val_type == lval_type)) {
            throw CompilerError("Type mismatch in assignment", assign->filename, assign->line, assign->column);
        }
        result = lval_type;
    } else if (auto inc = dynamic_cast<IncrementExpression *>(&expr)) {
        result = visit_expression(*inc->lvalue);
    } else if (auto dec = dynamic_cast<DecrementExpression *>(&expr)) {
        result = visit_expression(*dec->lvalue);
    }

    expr.type = std::make_unique<DataType>(result);
    return result;
}

void Analyzer::push_scope() { m_scopes.emplace_back(); }

void Analyzer::pop_scope() { m_scopes.pop_back(); }

void Analyzer::define_variable(const std::string &name, DataType type, std::string filename, int line, int col) {
    m_scopes.back().variables[name] = {type, std::move(filename), line, col};
}

DataType Analyzer::lookup_variable(const std::string &name, std::string filename, int line, int col) {
    const Symbol *sym = lookup_symbol(name);
    if (sym) {
        return sym->type;
    }
    throw CompilerError("Undefined variable: " + name, std::move(filename), line, col);
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
