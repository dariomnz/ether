#include "lsp/semantic_tokens.hpp"

#include <iostream>

#include "parser/ast.hpp"

namespace ether::lsp {

using namespace ether::parser;

void SemanticTokensVisitor::highlight_complex_type(const DataType &type, int line, int start_col) {
    if (type.kind == DataType::Kind::Struct) {
        tokens.push_back({line, start_col, (int)type.struct_name.size(), 3});
    } else if (type.kind == DataType::Kind::Ptr && type.inner) {
        // "ptr" is keyword (offset 0), inner starts at offset 4 "ptr("
        // But we rely on lexer coloring "ptr". We iterate inside.
        highlight_complex_type(*type.inner, line, start_col + 4);
    } else if (type.kind == DataType::Kind::Coroutine && type.inner) {
        // "coroutine(" -> 10 chars
        highlight_complex_type(*type.inner, line, start_col + 10);
    } else if (type.kind == DataType::Kind::Array && type.inner) {
        // "[" -> 1 char, array_size -> size chars, "]" -> 1 char
        highlight_complex_type(*type.inner, line, start_col + 2 + std::to_string(type.array_size).size());
    }
}

void SemanticTokensVisitor::visit(const Program &node) {
    for (auto &s : node.structs) s->accept(*this);
    for (auto &e : node.enums) e->accept(*this);
    for (auto &g : node.globals) g->accept(*this);
    for (auto &f : node.functions) f->accept(*this);
}

void SemanticTokensVisitor::visit(const Function &node) {
    if (node.filename != target_filename) return;

    // Return type
    highlight_complex_type(node.return_type, node.line, node.column);

    // If this is a method, highlight the struct name before ::
    if (!node.struct_name.empty()) {
        int struct_name_col = node.name_col - (int)node.struct_name.size() - 2;
        tokens.push_back({node.name_line, struct_name_col, (int)node.struct_name.size(), 3});
    }

    // Function name: type 0
    tokens.push_back({node.name_line, node.name_col, (int)node.length, 0});

    // Parameters
    for (const auto &p : node.params) {
        highlight_complex_type(p.type, p.line, p.col);
        // Parameter name: type 2
        tokens.push_back({p.name_line, p.name_col, (int)p.name.size(), 2});
    }

    if (node.body) node.body->accept(*this);
}

void SemanticTokensVisitor::visit(const Block &node) {
    for (auto &s : node.statements) s->accept(*this);
}

void SemanticTokensVisitor::visit(const VariableDeclaration &node) {
    if (node.filename != target_filename) return;

    // Type
    highlight_complex_type(node.type, node.line, node.column);

    // Variable name: type 1
    tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 1});
    if (node.init) node.init->accept(*this);
}

void SemanticTokensVisitor::visit(const VariableExpression &node) {
    // Variable reference: type 1
    tokens.push_back({node.line, node.column, (int)node.name.size(), 1});
}

void SemanticTokensVisitor::visit(const FunctionCall &node) {
    if (node.object) node.object->accept(*this);
    tokens.push_back({node.line, node.column, node.length, 0});
    for (auto &a : node.args) a->accept(*this);
}

void SemanticTokensVisitor::visit(const IfStatement &node) {
    if (node.condition) node.condition->accept(*this);
    if (node.then_branch) node.then_branch->accept(*this);
    if (node.else_branch) node.else_branch->accept(*this);
}

void SemanticTokensVisitor::visit(const ForStatement &node) {
    if (node.init) node.init->accept(*this);
    if (node.condition) node.condition->accept(*this);
    if (node.increment) node.increment->accept(*this);
    if (node.body) node.body->accept(*this);
}

void SemanticTokensVisitor::visit(const ReturnStatement &node) {
    if (node.expr) node.expr->accept(*this);
}

void SemanticTokensVisitor::visit(const ExpressionStatement &node) {
    if (node.expr) node.expr->accept(*this);
}

void SemanticTokensVisitor::visit(const BinaryExpression &node) {
    if (node.left) node.left->accept(*this);
    if (node.right) node.right->accept(*this);
}

void SemanticTokensVisitor::visit(const AssignmentExpression &node) {
    if (node.lvalue) node.lvalue->accept(*this);
    if (node.value) node.value->accept(*this);
}

void SemanticTokensVisitor::visit(const IntegerLiteral &node) {}
void SemanticTokensVisitor::visit(const FloatLiteral &node) {}
void SemanticTokensVisitor::visit(const StringLiteral &node) {}
void SemanticTokensVisitor::visit(const YieldStatement &node) {}

void SemanticTokensVisitor::visit(const SpawnExpression &node) {
    if (node.call) node.call->accept(*this);
}

void SemanticTokensVisitor::visit(const IncrementExpression &node) {
    if (node.lvalue) node.lvalue->accept(*this);
}

void SemanticTokensVisitor::visit(const DecrementExpression &node) {
    if (node.lvalue) node.lvalue->accept(*this);
}

void SemanticTokensVisitor::visit(const AwaitExpression &node) {
    if (node.expr) node.expr->accept(*this);
}

void SemanticTokensVisitor::visit(const StructDeclaration &node) {
    if (node.filename != target_filename) return;
    // Struct name: type 3 (type)
    tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 3});

    // Members
    for (const auto &m : node.members) {
        highlight_complex_type(m.type, m.line, m.col);
        // Member name: type 1 (variable/member)
        tokens.push_back({m.name_line, m.name_col, (int)m.name.size(), 1});
    }
}

void SemanticTokensVisitor::visit(const parser::EnumDeclaration &node) {
    if (node.filename != target_filename) return;
    tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 3});
    for (const auto &m : node.members) {
        tokens.push_back({m.line, m.col, (int)m.name.size(), 1});
    }
}

void SemanticTokensVisitor::visit(const parser::EnumAccessExpression &node) {
    if (node.filename != target_filename) return;
    tokens.push_back({node.line, node.column, (int)node.enum_name.size(), 3});
    tokens.push_back({node.line, node.column + (int)node.enum_name.size() + 2, (int)node.member_name.size(), 1});
}

void SemanticTokensVisitor::visit(const MemberAccessExpression &node) {
    node.object->accept(*this);
    // Member name: type 1 (variable)
    int member_start = (int)(node.length - node.member_name.size());
    tokens.push_back({node.line, node.column + member_start, (int)node.member_name.size(), 1});
}

void SemanticTokensVisitor::visit(const SizeofExpression &node) {
    if (node.filename != target_filename) return;
    if (node.target_type.kind == DataType::Kind::Struct) {
        tokens.push_back({node.type_line, node.type_col, (int)node.target_type.struct_name.size(), 3});
    }
}

void SemanticTokensVisitor::visit(const IndexExpression &node) {
    if (node.object) node.object->accept(*this);
    if (node.index) node.index->accept(*this);
}

void SemanticTokensVisitor::visit(const Include &node) {
    if (node.filename != target_filename) return;
}

void SemanticTokensVisitor::visit(const VarargExpression &node) {
    if (node.filename != target_filename) return;
}

}  // namespace ether::lsp
