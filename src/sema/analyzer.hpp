#ifndef ETHER_ANALYZER_HPP
#define ETHER_ANALYZER_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "parser/ast.hpp"

namespace ether::sema {

class Analyzer : public parser::ASTVisitor {
   public:
    void analyze(parser::Program &program);

    void visit(parser::IntegerLiteral &node) override;
    void visit(parser::FloatLiteral &node) override;
    void visit(parser::StringLiteral &node) override;
    void visit(parser::VariableExpression &node) override;
    void visit(parser::FunctionCall &node) override;
    void visit(parser::VarargExpression &node) override;
    void visit(parser::BinaryExpression &node) override;
    void visit(parser::Block &node) override;
    void visit(parser::IfStatement &node) override;
    void visit(parser::ReturnStatement &node) override;
    void visit(parser::ExpressionStatement &node) override;
    void visit(parser::YieldStatement &node) override;
    void visit(parser::SpawnExpression &node) override;
    void visit(parser::AssignmentExpression &node) override;
    void visit(parser::IncrementExpression &node) override;
    void visit(parser::DecrementExpression &node) override;
    void visit(parser::AwaitExpression &node) override;
    void visit(parser::ForStatement &node) override;
    void visit(parser::VariableDeclaration &node) override;
    void visit(parser::Function &node) override;
    void visit(parser::StructDeclaration &node) override;
    void visit(parser::Program &node) override;
    void visit(parser::MemberAccessExpression &node) override;
    void visit(parser::IndexExpression &node) override;
    void visit(parser::SizeofExpression &node) override;
    void visit(parser::Include &node) override;

   private:
    struct Symbol {
        parser::DataType type;
        std::string filename;
        int line;
        int col;
        bool is_global = false;
        uint16_t slot = 0;
    };

    struct Scope {
        std::unordered_map<std::string, Symbol> variables;
    };

    struct FunctionInfo {
        parser::DataType return_type;
        std::vector<parser::DataType> param_types;
        bool is_variadic;
        std::string filename;
        int line;
        int col;
    };

    struct StructInfo {
        std::string name;
        std::unordered_map<std::string, std::pair<parser::DataType, uint16_t>> members;
        uint16_t total_size;
    };

    std::vector<Scope> m_scopes;
    std::unordered_map<std::string, FunctionInfo> m_functions;
    std::unordered_map<std::string, StructInfo> m_structs;
    parser::DataType m_current_type;

    void push_scope();
    void pop_scope();
    void define_variable(const std::string &name, parser::DataType type, std::string filename, int line, int col);
    parser::DataType lookup_variable(const std::string &name, std::string filename, int line, int col);
    const Symbol *lookup_symbol(const std::string &name);
    const FunctionInfo *lookup_function(const std::string &name);
};

}  // namespace ether::sema

#endif  // ETHER_ANALYZER_HPP
