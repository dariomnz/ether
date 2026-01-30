#include "analyzer.hpp"

#include "common/error.hpp"

namespace ether::sema {

using namespace parser;

void Analyzer::analyze(Program &program) {
    push_scope();  // Global scope

    // Register built-ins
    m_functions["syscall"] = {DataType(DataType::Kind::I64), {}, true, "", 0, 0};

    // First pass: collect struct definitions
    for (const auto &str : program.structs) {
        StructInfo info;
        info.name = str->name;
        uint16_t offset = 0;
        for (const auto &member : str->members) {
            info.members[member.name] = {member.type, offset};
            uint16_t member_size = 1;
            if (member.type.kind == DataType::Kind::Struct) {
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

    // First pass (continued): collect function signatures
    for (const auto &func : program.functions) {
        std::vector<DataType> param_types;
        for (const auto &param : func->params) {
            param_types.push_back(param.type);
        }
        std::string func_name = func->name;
        if (!func->struct_name.empty()) {
            if (m_structs.find(func->struct_name) == m_structs.end()) {
                throw CompilerError("Undefined struct " + func->struct_name, func->filename, func->line, func->column,
                                    (int)func->struct_name.size());
            }
            // Ensure first parameter is ptr(StructName)
            if (param_types.empty()) {
                throw CompilerError("Struct method must have at least 'this' parameter", func->filename, func->line,
                                    func->column, (int)func_name.size());
            }
            // Strict check on 'this'? User said "ptr(Point) this".
            // We can just trust the user for now or check type
            func_name = func->struct_name + "::" + func_name;
        }

        m_functions[func_name] = {func->return_type, param_types, func->is_variadic,
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
    for (const auto &str : node.structs) {
        str->accept(*this);
    }
    for (const auto &func : node.functions) {
        // Temporarily rename function node if it's a method so internal visit uses correct name?
        // Actually visit(Function) body doesn't use its name for recursion, it just defines params.
        // But if we want recursive calls to work...
        // The AST node `name` is still the short name "add".
        // But scope has "Point::add".
        // Recursion within method: `add()`? Or `this.add()`?
        // Usually `this.add()`.
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
    if (node.type.kind == DataType::Kind::Struct) {
        if (m_structs.find(node.type.struct_name) == m_structs.end()) {
            throw CompilerError("Undefined struct: " + node.type.struct_name, node.filename, node.line, node.column,
                                node.length);
        }
    }
    if (node.init) {
        node.init->accept(*this);
        DataType init_type = m_current_type;
        if (!(init_type == node.type)) {
            bool is_null_ptr = (node.type.kind == DataType::Kind::Ptr && init_type.is_integer());
            bool is_ptr_cast = (node.type.kind == DataType::Kind::Ptr && init_type.kind == DataType::Kind::Ptr);
            bool is_int_conv = (node.type.is_integer() && init_type.is_integer());
            if (!is_null_ptr && !is_ptr_cast && !is_int_conv) {
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
    m_current_type = DataType(DataType::Kind::I32);
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

    bool left_ok = left.kind == DataType::Kind::I64 || left.kind == DataType::Kind::I32 ||
                   left.kind == DataType::Kind::I16 || left.kind == DataType::Kind::I8 ||
                   left.kind == DataType::Kind::Ptr;
    bool right_ok = right.kind == DataType::Kind::I64 || right.kind == DataType::Kind::I32 ||
                    right.kind == DataType::Kind::I16 || right.kind == DataType::Kind::I8 ||
                    right.kind == DataType::Kind::Ptr;

    if (!left_ok || !right_ok) {
        throw CompilerError("Binary operations are only supported for integers and pointers", node.filename, node.line,
                            node.column, node.length);
    }
    m_current_type = left;  // For now binary op type is the left type
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(FunctionCall &node) {
    std::string lookup_name = node.name;
    if (node.object) {
        node.object->accept(*this);
        DataType obj_type = m_current_type;
        std::string struct_name;
        if (obj_type.kind == DataType::Kind::Struct) {
            struct_name = obj_type.struct_name;
        } else if (obj_type.kind == DataType::Kind::Ptr && obj_type.inner &&
                   obj_type.inner->kind == DataType::Kind::Struct) {
            struct_name = obj_type.inner->struct_name;
        } else {
            throw CompilerError("Method call requires struct or struct pointer", node.filename, node.line, node.column,
                                node.length);
        }

        lookup_name = struct_name + "::" + node.name;

        // We update the node name so IR generation knows the full name
        node.name = lookup_name;
    }

    if (m_functions.find(lookup_name) == m_functions.end()) {
        if (node.object) {
            throw CompilerError("Struct " + node.name.substr(0, node.name.find("::")) + " has no method named " +
                                    node.name.substr(node.name.find("::") + 2),
                                node.filename, node.line, node.column, node.length);
        }
        throw CompilerError("Undefined function: " + node.name, node.filename, node.line, node.column, node.length);
    }
    const auto &info = m_functions[lookup_name];

    // Argument count check
    size_t expected_args = info.param_types.size();
    size_t provided_args = node.args.size();
    if (node.object) provided_args++;  // implicit 'this'

    if (info.is_variadic) {
        if (provided_args < expected_args) {
            throw CompilerError("Too few arguments for variadic function " + lookup_name, node.filename, node.line,
                                node.column, node.length);
        }
    } else {
        if (provided_args != expected_args) {
            // For error message, subtract 1 if method?
            // "Wrong number of arguments for Point::add".
            // Expected count includes 'this'.
            throw CompilerError("Wrong number of arguments for " + lookup_name, node.filename, node.line, node.column,
                                node.length);
        }
    }
    node.decl_filename = info.filename;
    node.decl_line = info.line;
    node.decl_col = info.col;
    node.param_types = info.param_types;
    node.is_variadic = info.is_variadic;

    // Check args
    size_t param_idx = 0;
    if (node.object) {
        // Implicit check for 'this' which is param 0
        DataType this_param = info.param_types[0];
        // obj_type was calculated above but lost. recalculate/cache?
        // Let's just trust node.object which was accepted.
        // Actually we need to check strict type.
        // If obj is Struct Point, and param is ptr(Point), it matches.
        // If obj is ptr(Point), and param is ptr(Point), it matches.

        // For now, assume it matches if struct name matches.
        param_idx++;
    }

    for (size_t i = 0; i < node.args.size(); ++i) {
        node.args[i]->accept(*this);
        // Param type check if not variadic part
        if (param_idx < info.param_types.size()) {
            // check type
        }
        param_idx++;
    }
    m_current_type = info.return_type;
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(VarargExpression &node) {
    // ellipsis can only appear in calls, and their type is effectively "multiple"
    // for now we just mark current type as i32 so it doesn't crash
    m_current_type = DataType(DataType::Kind::I32);
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
        m_current_type = DataType(DataType::Kind::I32);  // Fallback
    }
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(AssignmentExpression &node) {
    node.value->accept(*this);
    DataType val_type = m_current_type;
    node.lvalue->accept(*this);
    DataType lval_type = m_current_type;

    if (!(val_type == lval_type)) {
        bool is_null_ptr = (lval_type.kind == DataType::Kind::Ptr && val_type.is_integer());
        bool is_ptr_cast = (lval_type.kind == DataType::Kind::Ptr && val_type.kind == DataType::Kind::Ptr);
        bool is_int_conv = (lval_type.is_integer() && val_type.is_integer());
        if (!is_null_ptr && !is_ptr_cast && !is_int_conv) {
            throw CompilerError(
                "Type mismatch in assignment: expected " + lval_type.to_string() + ", but got " + val_type.to_string(),
                node.filename, node.line, node.column, node.length);
        }
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

void Analyzer::visit(Include &node) {}

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

void Analyzer::visit(StructDeclaration &node) {
    // Already processed in first pass
}

void Analyzer::visit(MemberAccessExpression &node) {
    node.object->accept(*this);
    DataType obj_type = m_current_type;

    if (obj_type.kind == DataType::Kind::Ptr && obj_type.inner && obj_type.inner->kind == DataType::Kind::Struct) {
        std::string struct_name = obj_type.inner->struct_name;
        if (m_structs.find(struct_name) == m_structs.end()) {
            throw CompilerError("Undefined struct: " + struct_name, node.filename, node.line, node.column, node.length);
        }
        const auto &info = m_structs[struct_name];
        if (info.members.find(node.member_name) == info.members.end()) {
            throw CompilerError("Struct " + struct_name + " has no member named " + node.member_name, node.filename,
                                node.line, node.column, node.length);
        }
        const auto &member = info.members.at(node.member_name);
        m_current_type = member.first;
        node.type = std::make_unique<DataType>(m_current_type);
    } else if (obj_type.kind == DataType::Kind::Struct) {
        std::string struct_name = obj_type.struct_name;
        if (m_structs.find(struct_name) == m_structs.end()) {
            throw CompilerError("Undefined struct: " + struct_name, node.filename, node.line, node.column, node.length);
        }
        const auto &info = m_structs[struct_name];
        if (info.members.find(node.member_name) == info.members.end()) {
            throw CompilerError("Struct " + struct_name + " has no member named " + node.member_name, node.filename,
                                node.line, node.column, node.length);
        }
        const auto &member = info.members.at(node.member_name);
        m_current_type = member.first;
        node.type = std::make_unique<DataType>(m_current_type);
    } else {
        throw CompilerError("Member access '.' requires struct or struct pointer, but got " + obj_type.to_string(),
                            node.filename, node.line, node.column, node.length);
    }
}

void Analyzer::visit(SizeofExpression &node) {
    uint32_t slots = 1;
    if (node.target_type.kind == DataType::Kind::Struct) {
        auto it = m_structs.find(node.target_type.struct_name);
        if (it != m_structs.end()) {
            slots = it->second.total_size;
        }
    }
    node.calculated_size = slots * 16;
    m_current_type = DataType(DataType::Kind::I32);
    node.type = std::make_unique<DataType>(m_current_type);
}

void Analyzer::visit(IndexExpression &node) {
    node.object->accept(*this);
    DataType obj_type = m_current_type;

    // Verify that the object is a pointer
    if (obj_type.kind != DataType::Kind::Ptr) {
        throw CompilerError("Index operator '[]' requires a pointer, but got " + obj_type.to_string(), node.filename,
                            node.line, node.column, node.length);
    }

    // Verify that the index is an integer
    node.index->accept(*this);
    DataType index_type = m_current_type;
    if (!index_type.is_integer()) {
        throw CompilerError("Index must be an integer type, but got " + index_type.to_string(), node.filename,
                            node.line, node.column, node.length);
    }

    // The result type is the inner type of the pointer
    if (obj_type.inner) {
        m_current_type = *obj_type.inner;
    } else {
        // Fallback to i32 if no inner type specified
        m_current_type = DataType(DataType::Kind::I32);
    }
    node.type = std::make_unique<DataType>(m_current_type);
}

}  // namespace ether::sema
