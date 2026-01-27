#ifndef ETHER_ANALYZER_HPP
#define ETHER_ANALYZER_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "parser/ast.hpp"

namespace ether::sema {

class Analyzer {
   public:
    void analyze(parser::Program &program);

   private:
    struct Symbol {
        parser::DataType type;
    };

    struct Scope {
        std::unordered_map<std::string, Symbol> variables;
    };

    struct FunctionInfo {
        parser::DataType return_type;
        std::vector<parser::DataType> param_types;
    };

    std::vector<Scope> m_scopes;
    std::unordered_map<std::string, FunctionInfo> m_functions;

    void visit_function(parser::Function &func);
    void visit_block(parser::Block &block);
    void visit_statement(parser::Statement &stmt);
    parser::DataType visit_expression(parser::Expression &expr);

    void push_scope();
    void pop_scope();
    void define_variable(const std::string &name, parser::DataType type);
    parser::DataType lookup_variable(const std::string &name, std::string filename, int line, int col);
};

}  // namespace ether::sema

#endif  // ETHER_ANALYZER_HPP
