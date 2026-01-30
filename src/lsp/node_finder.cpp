#include "lsp/node_finder.hpp"

#include <sstream>
// #define DEBUG
#include "common/debug.hpp"
#include "parser/ast.hpp"

namespace ether::lsp {

using namespace ether::parser;

std::string NodeFinder::find_struct_in_type(const DataType &type) {
    if (type.kind == DataType::Kind::Struct) return type.struct_name;
    if (type.inner) return find_struct_in_type(*type.inner);
    return "";
}

void NodeFinder::check_complex_type(const DataType &type, int type_line, int start_col) {
    if (found) return;

    // Only check if on the same line
    if (this->line != type_line) return;

    if (type.kind == DataType::Kind::Struct) {
        // Struct name starts at start_col
        int len = (int)type.struct_name.size();
        if (this->col >= start_col && this->col < start_col + len) {
            resolve_struct(type.struct_name);
        }
    } else if (type.kind == DataType::Kind::Ptr && type.inner) {
        // "ptr(" -> +4
        check_complex_type(*type.inner, type_line, start_col + 4);
    } else if (type.kind == DataType::Kind::Coroutine && type.inner) {
        // "coroutine(" -> +10
        check_complex_type(*type.inner, type_line, start_col + 10);
    }
}

void NodeFinder::resolve_struct(const std::string &name) {
    if (!root_program) return;
    for (const auto &s : root_program->structs) {
        if (s->name == name) {
            found = true;
            def_filename = s->filename;
            def_line = s->name_line;
            def_col = s->name_col;
            def_size = (int)s->name.size();

            std::stringstream ss;
            ss << "struct " << s->name << " {\n";
            for (const auto &m : s->members) {
                ss << "  " << m.type.to_string() << " " << m.name << ";\n";
            }
            ss << "}";
            hover_info = ss.str();
            return;
        }
    }
}

void NodeFinder::visit(const Program &node) {
    debug_msg("Visiting program " << node.filename);
    for (auto &inc : node.includes) {
        if (found) return;
        inc->accept(*this);
    }
    for (auto &g : node.globals) {
        if (found) return;
        g->accept(*this);
    }
    for (auto &s : node.structs) {
        if (found) return;
        s->accept(*this);
    }
    for (auto &f : node.functions) {
        if (found) return;
        f->accept(*this);
    }
}

void NodeFinder::visit(const Function &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting function " << node.name << " at " << node.filename << ":" << node.line << ":" << node.column);

    if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
        found = true;
        def_filename = node.filename;
        def_line = node.name_line;
        def_col = node.name_col;
        def_size = (int)node.name.size();

        std::stringstream ss;
        ss << "(function) " << node.return_type << " " << node.name << "(";
        for (size_t i = 0; i < node.params.size(); ++i) {
            ss << node.params[i].type << " " << node.params[i].name;
            if (i < node.params.size() - 1 || node.is_variadic) ss << ", ";
        }
        if (node.is_variadic) ss << "...";
        ss << ")";
        hover_info = ss.str();
        return;
    }

    // Check for Struct Name in method declaration (e.g. "Point::add")
    if (!node.struct_name.empty() && node.name_line == line) {
        int struct_name_col = node.name_col - (int)node.struct_name.size() - 2;
        if (col >= struct_name_col && col < struct_name_col + (int)node.struct_name.size()) {
            resolve_struct(node.struct_name);
            if (found) return;
        }
    }

    // Return type
    check_complex_type(node.return_type, node.line, node.column);
    if (found) return;

    // Parameters
    for (const auto &p : node.params) {
        check_complex_type(p.type, p.line, p.col);
        if (found) return;
    }

    if (node.body) node.body->accept(*this);
}

void NodeFinder::visit(const Block &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting block at " << node.filename << ":" << node.line << ":" << node.column);
    for (auto &s : node.statements) {
        if (found) return;
        s->accept(*this);
    }
}

void NodeFinder::visit(const VariableDeclaration &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting variable declaration " << node.name << " at " << node.filename << ":" << node.line << ":"
                                               << node.column);
    if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
        found = true;
        def_filename = node.filename;
        def_line = node.name_line;
        def_col = node.name_col;
        def_size = (int)node.name.size();
        hover_info = "(variable) " + node.type.to_string() + " " + node.name;
        return;
    }

    // Check if cursor is on the type area
    check_complex_type(node.type, node.line, node.column);
    if (found) return;

    if (node.init) node.init->accept(*this);
}

void NodeFinder::visit(const FunctionCall &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting function call " << node.name << " at " << node.filename << ":" << node.line << ":"
                                        << node.column);
    if (node.line == line && col >= node.column && col < node.column + (int)node.length) {
        found = true;
        def_filename = node.decl_filename;
        def_line = node.decl_line;
        def_col = node.decl_col;
        def_size = (int)node.length;
        std::stringstream ss;
        ss << "(call) ";
        if (node.type) {
            found_type = *node.type;
            ss << *node.type << " ";
        }
        ss << node.name << "(";
        for (size_t i = 0; i < node.param_types.size(); ++i) {
            ss << node.param_types[i];
            if (i < node.param_types.size() - 1 || node.is_variadic) ss << ", ";
        }
        if (node.is_variadic) ss << "...";
        ss << ")";
        hover_info = ss.str();
        return;
    }
    if (node.object) node.object->accept(*this);
    for (auto &a : node.args) {
        if (found) return;
        a->accept(*this);
    }
}

void NodeFinder::visit(const VariableExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting variable expression " << node.name << " at " << node.filename << ":" << node.line << ":"
                                              << node.column);
    if (node.line == line && col >= node.column && col < node.column + (int)node.length) {
        found = true;
        def_filename = node.decl_filename;
        def_line = node.decl_line;
        def_col = node.decl_col;
        def_size = (int)node.length;
        if (node.type) {
            hover_info = "(variable) " + node.type->to_string() + " " + node.name;
            found_type = *node.type;
        } else {
            hover_info = "(variable) " + node.name;
        }
    }
}

void NodeFinder::visit(const IfStatement &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting if statement at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.condition) node.condition->accept(*this);
    if (found) return;
    if (node.then_branch) node.then_branch->accept(*this);
    if (found) return;
    if (node.else_branch) node.else_branch->accept(*this);
}

void NodeFinder::visit(const ForStatement &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting for statement at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.init) node.init->accept(*this);
    if (found) return;
    if (node.condition) node.condition->accept(*this);
    if (found) return;
    if (node.increment) node.increment->accept(*this);
    if (found) return;
    if (node.body) node.body->accept(*this);
}

void NodeFinder::visit(const ReturnStatement &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting return statement at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.expr) node.expr->accept(*this);
}

void NodeFinder::visit(const ExpressionStatement &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting expression statement at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.expr) node.expr->accept(*this);
}

void NodeFinder::visit(const BinaryExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting binary expression at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.left) node.left->accept(*this);
    if (found) return;
    if (node.right) node.right->accept(*this);
}

void NodeFinder::visit(const AssignmentExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting assignment expression at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.lvalue) node.lvalue->accept(*this);
    if (found) return;
    if (node.value) node.value->accept(*this);
}

void NodeFinder::visit(const IntegerLiteral &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting integer literal at " << node.filename << ":" << node.line << ":" << node.column);
}

void NodeFinder::visit(const StringLiteral &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting string literal at " << node.filename << ":" << node.line << ":" << node.column);
}

void NodeFinder::visit(const YieldStatement &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting yield statement at " << node.filename << ":" << node.line << ":" << node.column);
}

void NodeFinder::visit(const SpawnExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting spawn expression at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.call) node.call->accept(*this);
}

void NodeFinder::visit(const IncrementExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting increment expression at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.lvalue) node.lvalue->accept(*this);
}

void NodeFinder::visit(const DecrementExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting decrement expression at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.lvalue) node.lvalue->accept(*this);
}

void NodeFinder::visit(const AwaitExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting await expression at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.expr) node.expr->accept(*this);
}

void NodeFinder::visit(const Include &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting include " << node.path << " at " << node.filename << ":" << node.line << ":" << node.column);
    // Check if cursor is on the #include or the path
    // For simplicity, we match the whole line of the include
    if (node.line == line) {
        found = true;
        def_filename = node.path;
        def_line = 1;
        def_col = 1;
        def_size = 0;  // LSP will just open the file at the start
        hover_info = "include \"" + node.path + "\"";
    }
}

void NodeFinder::visit(const StructDeclaration &node) {
    if (found || node.filename != target_filename) return;
    if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
        found = true;
        def_filename = node.filename;
        def_line = node.name_line;
        def_col = node.name_col;
        def_size = (int)node.name.size();
        std::stringstream ss;
        ss << "struct " << node.name << " {\n";
        for (const auto &m : node.members) {
            ss << "  " << m.type.to_string() << " " << m.name << ";\n";
        }
        ss << "}";
        ss << "}";
        hover_info = ss.str();
        return;
    }

    // Check member types
    for (const auto &m : node.members) {
        check_complex_type(m.type, m.line, m.col);
        if (found) return;
    }
}

void NodeFinder::visit(const MemberAccessExpression &node) {
    if (found || node.filename != target_filename) return;
    node.object->accept(*this);
    if (found) return;

    // Check if cursor is on the member name
    int member_start = (int)(node.length - node.member_name.size());
    if (node.line == line && col >= node.column + member_start && col < node.column + node.length) {
        found = true;
        if (node.type) {
            hover_info = "(member) " + node.type->to_string() + " " + node.member_name;
            found_type = *node.type;
        } else {
            hover_info = "(member) " + node.member_name;
        }

        // If we have object type, we can jump to member definition
        if (node.object->type) {
            std::string s_name = find_struct_in_type(*node.object->type);
            if (!s_name.empty() && root_program) {
                for (const auto &s : root_program->structs) {
                    if (s->name == s_name) {
                        for (const auto &m : s->members) {
                            if (m.name == node.member_name) {
                                // Found the member!
                                // Wait, Parameter has no pos, but maybe we can guess from struct definition?
                                // We'll jump to the struct definition for now as it's better than nothing.
                                def_filename = s->filename;
                                def_line = s->name_line;
                                def_col = s->name_col;
                                def_size = (int)s->name.size();
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
}

void NodeFinder::visit(const SizeofExpression &node) {
    if (found || node.filename != target_filename) return;
    if (node.line == line && col >= node.column && col < node.column + node.length) {
        std::string s_name = find_struct_in_type(node.target_type);
        if (!s_name.empty()) {
            resolve_struct(s_name);
        }

        std::string struct_info = hover_info;
        std::stringstream ss;
        if (!struct_info.empty()) {
            ss << struct_info << "\n\n";
        }
        ss << "// Result: " << node.calculated_size << " bytes\n";
        ss << "sizeof(" << node.target_type.to_string() << ")";
        hover_info = ss.str();
        found = true;
    }
}

void NodeFinder::visit(const IndexExpression &node) {
    if (found || node.filename != target_filename) return;
    debug_msg("Visiting index expression at " << node.filename << ":" << node.line << ":" << node.column);
    if (node.object) node.object->accept(*this);
    if (node.index) node.index->accept(*this);
}

void NodeFinder::visit(const VarargExpression &node) {
    if (found || node.filename != target_filename) return;
}

}  // namespace ether::lsp
