#pragma once

#include <string>
#include <vector>

#include "parser/ast.hpp"

namespace ether::lsp {

struct SemanticToken {
    int line;
    int col;
    int length;
    int type;

    bool operator<(const SemanticToken &other) const {
        if (line != other.line) return line < other.line;
        return col < other.col;
    }
};

struct SemanticTokensVisitor : public parser::ConstASTVisitor {
    std::string target_filename;
    std::vector<SemanticToken> tokens;

    SemanticTokensVisitor(std::string filename) : target_filename(std::move(filename)) {}

    void highlight_complex_type(const parser::DataType &type, int line, int start_col);

    void visit(const parser::Program &node) override;
    void visit(const parser::Function &node) override;
    void visit(const parser::Block &node) override;
    void visit(const parser::VariableDeclaration &node) override;
    void visit(const parser::VariableExpression &node) override;
    void visit(const parser::FunctionCall &node) override;
    void visit(const parser::IfStatement &node) override;
    void visit(const parser::ForStatement &node) override;
    void visit(const parser::ReturnStatement &node) override;
    void visit(const parser::ExpressionStatement &node) override;
    void visit(const parser::BinaryExpression &node) override;
    void visit(const parser::AssignmentExpression &node) override;
    void visit(const parser::IntegerLiteral &node) override;
    void visit(const parser::FloatLiteral &node) override;
    void visit(const parser::StringLiteral &node) override;
    void visit(const parser::YieldStatement &node) override;
    void visit(const parser::SpawnExpression &node) override;
    void visit(const parser::IncrementExpression &node) override;
    void visit(const parser::DecrementExpression &node) override;
    void visit(const parser::AwaitExpression &node) override;
    void visit(const parser::StructDeclaration &node) override;
    void visit(const parser::EnumDeclaration &node) override;
    void visit(const parser::EnumAccessExpression &node) override;
    void visit(const parser::MemberAccessExpression &node) override;
    void visit(const parser::SizeofExpression &node) override;
    void visit(const parser::IndexExpression &node) override;
    void visit(const parser::Include &node) override;
    void visit(const parser::VarargExpression &node) override;
};

}  // namespace ether::lsp
