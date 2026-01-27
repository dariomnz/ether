#include "parser.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/error.hpp"
#include "lexer/lexer.hpp"

namespace ether::parser {

Parser::Parser(const std::vector<lexer::Token> &tokens) : m_tokens(tokens) {}

const lexer::Token &Parser::peek() const { return m_tokens[m_pos]; }

const lexer::Token &Parser::advance() {
    if (m_pos < m_tokens.size() - 1) m_pos++;
    return m_tokens[m_pos - 1];
}

bool Parser::check(lexer::TokenType type) const { return peek().type == type; }

bool Parser::match(lexer::TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

// ... existing Parser methods ...

std::unique_ptr<Program> Parser::parse_program() {
    auto program = std::make_unique<Program>();
    while (!check(lexer::TokenType::EOF_TOKEN)) {
        if (match(lexer::TokenType::HashInclude)) {
            if (!match(lexer::TokenType::StringLiteral)) {
                const auto &token = peek();
                throw CompilerError("Expected string literal after '#include'", token.line, token.column);
            }
            std::string path(m_tokens[m_pos - 1].lexeme);

            // Recursive parse
            std::ifstream file(path);
            if (!file.is_open()) {
                const auto &token = m_tokens[m_pos - 1];
                throw CompilerError("Could not open included file: " + path, token.line, token.column);
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string included_source = buffer.str();
            lexer::Lexer imported_lexer(included_source);
            auto imported_tokens = imported_lexer.tokenize();
            Parser imported_parser(imported_tokens);
            auto imported_prog = imported_parser.parse_program();

            for (auto &f : imported_prog->functions) {
                program->functions.push_back(std::move(f));
            }
        } else {
            program->functions.push_back(parse_function());
        }
    }
    return program;
}

std::unique_ptr<Function> Parser::parse_function() {
    if (!match(lexer::TokenType::Int)) {
        const auto &token = peek();
        throw CompilerError("Expected type at start of function", token.line, token.column);
    }

    if (!check(lexer::TokenType::Identifier)) {
        const auto &token = peek();
        throw CompilerError("Expected function name", token.line, token.column);
    }
    auto name = advance().lexeme;

    if (!match(lexer::TokenType::LParent)) {
        const auto &token = peek();
        throw CompilerError("Expected '(' after function name", token.line, token.column);
    }

    std::vector<std::string> params;
    if (!check(lexer::TokenType::RParent)) {
        do {
            if (!match(lexer::TokenType::Int)) {
                const auto &token = peek();
                throw CompilerError("Expected type for parameter", token.line, token.column);
            }
            if (!check(lexer::TokenType::Identifier)) {
                const auto &token = peek();
                throw CompilerError("Expected parameter name", token.line, token.column);
            }
            params.push_back(std::string(advance().lexeme));
        } while (match(lexer::TokenType::Comma));
    }

    if (!match(lexer::TokenType::RParent)) {
        const auto &token = peek();
        throw CompilerError("Expected ')' after parameters", token.line, token.column);
    }

    auto body = parse_block();
    return std::make_unique<Function>(std::string(name), std::move(params), std::move(body));
}

std::unique_ptr<Block> Parser::parse_block() {
    if (!match(lexer::TokenType::LBrace)) {
        const auto &token = peek();
        throw CompilerError("Expected '{' at start of block", token.line, token.column);
    }

    auto block = std::make_unique<Block>();
    while (!check(lexer::TokenType::RBrace) && !check(lexer::TokenType::EOF_TOKEN)) {
        block->statements.push_back(parse_statement());
    }

    if (!match(lexer::TokenType::RBrace)) {
        const auto &token = peek();
        throw CompilerError("Expected '}' after block", token.line, token.column);
    }

    return block;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    if (match(lexer::TokenType::If)) {
        if (!match(lexer::TokenType::LParent))
            throw CompilerError("Expected '(' after 'if'", peek().line, peek().column);
        auto condition = parse_expression();
        if (!match(lexer::TokenType::RParent))
            throw CompilerError("Expected ')' after if condition", peek().line, peek().column);

        auto then_branch = parse_block();
        std::unique_ptr<Block> else_branch = nullptr;
        if (match(lexer::TokenType::Else)) {
            else_branch = parse_block();
        }
        return std::make_unique<IfStatement>(std::move(condition), std::move(then_branch), std::move(else_branch));
    }

    if (match(lexer::TokenType::For)) {
        if (!match(lexer::TokenType::LParent))
            throw CompilerError("Expected '(' after 'for'", peek().line, peek().column);

        std::unique_ptr<Statement> init = nullptr;
        if (!match(lexer::TokenType::Semicolon)) {
            init = parse_statement();  // This handles int i = 0; or i = 0;
        }

        std::unique_ptr<Expression> condition = nullptr;
        if (!match(lexer::TokenType::Semicolon)) {
            condition = parse_expression();
            if (!match(lexer::TokenType::Semicolon))
                throw CompilerError("Expected ';' after for condition", peek().line, peek().column);
        }

        std::unique_ptr<Expression> increment = nullptr;
        if (!match(lexer::TokenType::RParent)) {
            increment = parse_expression();
            if (!match(lexer::TokenType::RParent))
                throw CompilerError("Expected ')' after for increment", peek().line, peek().column);
        }

        auto body = parse_block();
        return std::make_unique<ForStatement>(std::move(init), std::move(condition), std::move(increment),
                                              std::move(body));
    }

    if (match(lexer::TokenType::Return)) {
        auto expr = parse_expression();
        if (!match(lexer::TokenType::Semicolon)) {
            const auto &token = peek();
            throw CompilerError("Expected ';' after return", token.line, token.column);
        }
        return std::make_unique<ReturnStatement>(std::move(expr));
    }

    if (match(lexer::TokenType::Int)) {
        if (!check(lexer::TokenType::Identifier)) {
            const auto &token = peek();
            throw CompilerError("Expected variable name after 'int'", token.line, token.column);
        }
        auto name = advance().lexeme;
        std::unique_ptr<Expression> init = nullptr;
        if (match(lexer::TokenType::Equal)) {
            init = parse_expression();
        }
        if (!match(lexer::TokenType::Semicolon)) {
            const auto &token = peek();
            throw CompilerError("Expected ';' after declaration", token.line, token.column);
        }
        return std::make_unique<VariableDeclaration>(std::move(name), std::move(init));
    }

    // Expression statement as fallback
    auto expr = parse_expression();
    if (!match(lexer::TokenType::Semicolon)) {
        const auto &token = peek();
        throw CompilerError("Expected ';' after expression", token.line, token.column);
    }
    return std::make_unique<ExpressionStatement>(std::move(expr));
}

std::unique_ptr<Expression> Parser::parse_expression() {
    if (check(lexer::TokenType::Identifier)) {
        // Lookahead to see if it's an assignment or increment
        if (m_pos + 1 < m_tokens.size()) {
            if (m_tokens[m_pos + 1].type == lexer::TokenType::Equal) {
                std::string name(advance().lexeme);
                advance();  // skip '='
                return std::make_unique<AssignmentExpression>(std::move(name), parse_expression());
            }
            if (m_tokens[m_pos + 1].type == lexer::TokenType::PlusPlus) {
                std::string name(advance().lexeme);
                advance();  // skip '++'
                return std::make_unique<IncrementExpression>(std::move(name));
            }
        }
    }
    return parse_comparison();
}

std::unique_ptr<Expression> Parser::parse_comparison() {
    auto left = parse_addition();

    while (match(lexer::TokenType::Less) || match(lexer::TokenType::LessEqual) || match(lexer::TokenType::Greater) ||
           match(lexer::TokenType::GreaterEqual) || match(lexer::TokenType::EqualEqual)) {
        lexer::TokenType op_token = m_tokens[m_pos - 1].type;
        auto right = parse_addition();
        BinaryExpression::Op op;
        switch (op_token) {
            case lexer::TokenType::Less:
                op = BinaryExpression::Op::Less;
                break;
            case lexer::TokenType::LessEqual:
                op = BinaryExpression::Op::Leq;
                break;
            case lexer::TokenType::Greater:
                op = BinaryExpression::Op::Gt;
                break;
            case lexer::TokenType::GreaterEqual:
                op = BinaryExpression::Op::Geq;
                break;
            case lexer::TokenType::EqualEqual:
                op = BinaryExpression::Op::Eq;
                break;
            default:
                throw std::runtime_error("Unknown comparison operator");
        }
        left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_addition() {
    auto left = parse_multiplication();

    while (match(lexer::TokenType::Plus) || match(lexer::TokenType::Minus)) {
        lexer::TokenType op_token = m_tokens[m_pos - 1].type;
        auto right = parse_multiplication();
        BinaryExpression::Op op =
            (op_token == lexer::TokenType::Plus) ? BinaryExpression::Op::Add : BinaryExpression::Op::Sub;
        left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_multiplication() {
    auto left = parse_primary();

    while (match(lexer::TokenType::Star) || match(lexer::TokenType::Slash)) {
        lexer::TokenType op_token = m_tokens[m_pos - 1].type;
        auto right = parse_primary();
        BinaryExpression::Op op =
            (op_token == lexer::TokenType::Star) ? BinaryExpression::Op::Mul : BinaryExpression::Op::Div;
        left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_primary() {
    if (match(lexer::TokenType::IntegerLiteral)) {
        int val = std::stoi(std::string(m_tokens[m_pos - 1].lexeme));
        return std::make_unique<IntegerLiteral>(val);
    }
    if (match(lexer::TokenType::StringLiteral)) {
        return std::make_unique<StringLiteral>(m_tokens[m_pos - 1].lexeme);
    }
    if (match(lexer::TokenType::Identifier)) {
        std::string name = m_tokens[m_pos - 1].lexeme;
        if (match(lexer::TokenType::LParent)) {
            std::vector<std::unique_ptr<Expression>> args;
            if (!check(lexer::TokenType::RParent)) {
                do {
                    args.push_back(parse_expression());
                } while (match(lexer::TokenType::Comma));
            }
            if (!match(lexer::TokenType::RParent)) {
                throw std::runtime_error("Expected ')' after arguments");
            }
            return std::make_unique<FunctionCall>(std::move(name), std::move(args));
        }
        return std::make_unique<VariableExpression>(std::move(name));
    }
    if (match(lexer::TokenType::LParent)) {
        auto expr = parse_expression();
        if (!match(lexer::TokenType::RParent)) {
            throw std::runtime_error("Expected ')' after expression");
        }
        return expr;
    }
    throw std::runtime_error("Expected expression at line " + std::to_string(peek().line));
}

}  // namespace ether::parser
