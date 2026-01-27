#ifndef ETHER_PARSER_HPP
#define ETHER_PARSER_HPP

#include <memory>
#include <optional>
#include <vector>

#include "ast.hpp"
#include "lexer/lexer.hpp"

namespace ether::parser {

class Parser {
   public:
    explicit Parser(const std::vector<lexer::Token> &tokens, std::string filename);
    std::unique_ptr<Program> parse_program();

   private:
    const std::vector<lexer::Token> &m_tokens;
    std::string m_filename;
    size_t m_pos = 0;

    const lexer::Token &peek() const;
    const lexer::Token &advance();
    bool match(lexer::TokenType type);
    bool check(lexer::TokenType type) const;

    DataType parse_type();
    std::unique_ptr<Function> parse_function();
    std::unique_ptr<Block> parse_block();
    std::unique_ptr<Statement> parse_statement();
    std::unique_ptr<Expression> parse_expression();
    std::unique_ptr<Expression> parse_comparison();
    std::unique_ptr<Expression> parse_addition();
    std::unique_ptr<Expression> parse_multiplication();
    std::unique_ptr<Expression> parse_primary();
};

}  // namespace ether::parser

#endif  // ETHER_PARSER_HPP
