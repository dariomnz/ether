#pragma once

#include "parser/ast.hpp"

namespace ether::lsp {

struct NodeFinder : public parser::ConstASTVisitor {
    int line;
    int col;
    bool found = false;
    std::string def_filename = "";
    int def_line = 0;
    int def_col = 0;
    int def_size = 0;
    std::string hover_info = "";
    parser::Program *root_program = nullptr;
    std::string target_filename = "";
    std::optional<parser::DataType> found_type;

    std::string find_struct_in_type(const parser::DataType &type);
    void resolve_struct(const std::string &name);
    void check_complex_type(const parser::DataType &type, int line, int start_col);

    void visit(const parser::Program &node) override;
    void visit(const parser::Function &node) override;
    void visit(const parser::Block &node) override;
    void visit(const parser::VariableDeclaration &node) override;
    void visit(const parser::FunctionCall &node) override;
    void visit(const parser::VariableExpression &node) override;
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
    void visit(const parser::Include &node) override;
    void visit(const parser::StructDeclaration &node) override;
    void visit(const parser::MemberAccessExpression &node) override;
    void visit(const parser::SizeofExpression &node) override;
    void visit(const parser::IndexExpression &node) override;
    void visit(const parser::VarargExpression &node) override;
};

}  // namespace ether::lsp
