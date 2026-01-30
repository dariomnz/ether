#include "lsp/semantic_tokens.hpp"

#include "parser/ast.hpp"

namespace ether::lsp {

using namespace ether::parser;

void SemanticTokensVisitor::visit(const Program &node) {
    for (auto &s : node.structs) s->accept(*this);
    for (auto &g : node.globals) g->accept(*this);
    for (auto &f : node.functions) f->accept(*this);
}

void SemanticTokensVisitor::visit(const Function &node) {
    if (node.filename != target_filename) return;

    // Return type: if struct, highlight as type 3
    if (node.return_type.kind == DataType::Kind::Struct) {
        tokens.push_back({node.line, node.column, (int)node.return_type.struct_name.size(), 3});
    }

    // Function name: type 0
    tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 0});

    // Parameters
    for (const auto &p : node.params) {
        if (p.type.kind == DataType::Kind::Struct) {
            tokens.push_back({p.line, p.col, (int)p.type.struct_name.size(), 3});
        }
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

    // Type: if struct, highlight as type 3
    if (node.type.kind == DataType::Kind::Struct) {
        tokens.push_back({node.line, node.column, (int)node.type.struct_name.size(), 3});
    }

    // Variable name: type 1
    tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 1});
    if (node.init) node.init->accept(*this);
}

void SemanticTokensVisitor::visit(const VariableExpression &node) {
    // Variable reference: type 1
    tokens.push_back({node.line, node.column, (int)node.name.size(), 1});
}

void SemanticTokensVisitor::visit(const FunctionCall &node) {
    // Function call: type 0
    tokens.push_back({node.line, node.column, (int)node.name.size(), 0});
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
        if (m.type.kind == DataType::Kind::Struct) {
            tokens.push_back({m.line, m.col, (int)m.type.struct_name.size(), 3});
        }
        // Member name: type 1 (variable/member)
        tokens.push_back({m.name_line, m.name_col, (int)m.name.size(), 1});
    }
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
