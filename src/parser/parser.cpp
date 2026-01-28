#include "parser.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common/error.hpp"
#include "lexer/lexer.hpp"

namespace ether::parser {

Parser::Parser(const std::vector<lexer::Token> &tokens, std::string filename)
    : m_tokens(tokens), m_filename(std::move(filename)) {}

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

DataType Parser::parse_type() {
    if (match(lexer::TokenType::Int)) return DataType(DataType::Kind::Int);
    if (match(lexer::TokenType::Coroutine)) return DataType(DataType::Kind::Coroutine);
    const auto &err_token = peek();
    throw CompilerError("Expected type", m_filename, err_token.line, err_token.column, (int)err_token.lexeme.size());
}

std::unique_ptr<Program> Parser::parse_program() {
    auto program = std::make_unique<Program>();
    program->filename = m_filename;
    while (!check(lexer::TokenType::EOF_TOKEN)) {
        if (match(lexer::TokenType::HashInclude)) {
            if (!match(lexer::TokenType::StringLiteral)) {
                const auto &token = peek();
                throw CompilerError("Expected string literal after '#include'", m_filename, token.line, token.column,
                                    (int)token.lexeme.size());
            }
            std::string path(m_tokens[m_pos - 1].lexeme);

            // Recursive parse
            std::ifstream file(path);
            if (!file.is_open()) {
                const auto &token = m_tokens[m_pos - 1];
                throw CompilerError("Could not open included file: " + path, m_filename, token.line, token.column,
                                    (int)token.lexeme.size());
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string included_source = buffer.str();
            lexer::Lexer imported_lexer(included_source, path);
            auto imported_tokens = imported_lexer.tokenize();
            Parser imported_parser(imported_tokens, path);
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
    const auto &start_token = peek();
    DataType return_type = parse_type();

    if (!check(lexer::TokenType::Identifier)) {
        const auto &token = peek();
        throw CompilerError("Expected function name", m_filename, token.line, token.column, (int)token.lexeme.size());
    }
    const auto &name_token = advance();
    auto name = name_token.lexeme;

    if (!match(lexer::TokenType::LParent)) {
        const auto &token = peek();
        throw CompilerError("Expected '(' after function name", m_filename, token.line, token.column);
    }

    std::vector<Parameter> params;
    if (!check(lexer::TokenType::RParent)) {
        do {
            DataType param_type = parse_type();
            if (!check(lexer::TokenType::Identifier)) {
                const auto &token = peek();
                throw CompilerError("Expected parameter name", m_filename, token.line, token.column,
                                    (int)token.lexeme.size());
            }
            params.push_back({param_type, std::string(advance().lexeme)});
        } while (match(lexer::TokenType::Comma));
    }

    if (!match(lexer::TokenType::RParent)) {
        const auto &token = peek();
        throw CompilerError("Expected ')' after parameters", m_filename, token.line, token.column);
    }

    auto body = parse_block();
    return std::make_unique<Function>(return_type, std::string(name), name_token.line, name_token.column,
                                      std::move(params), std::move(body), m_filename, start_token.line,
                                      start_token.column);
}

std::unique_ptr<Block> Parser::parse_block() {
    const auto &start_token = peek();
    if (!match(lexer::TokenType::LBrace)) {
        throw CompilerError("Expected '{' at start of block", m_filename, start_token.line, start_token.column);
    }

    auto block = std::make_unique<Block>(m_filename, start_token.line, start_token.column);
    while (!check(lexer::TokenType::RBrace) && !check(lexer::TokenType::EOF_TOKEN)) {
        block->statements.push_back(parse_statement());
    }

    if (!match(lexer::TokenType::RBrace)) {
        const auto &token = peek();
        throw CompilerError("Expected '}' after block", m_filename, token.line, token.column);
    }

    return block;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    const auto &start_token = peek();
    if (match(lexer::TokenType::If)) {
        if (!match(lexer::TokenType::LParent))
            throw CompilerError("Expected '(' after 'if'", m_filename, peek().line, peek().column);
        auto condition = parse_expression();
        if (!match(lexer::TokenType::RParent))
            throw CompilerError("Expected ')' after if condition", m_filename, peek().line, peek().column);

        auto then_branch = parse_block();
        std::unique_ptr<Block> else_branch = nullptr;
        if (match(lexer::TokenType::Else)) {
            else_branch = parse_block();
        }
        return std::make_unique<IfStatement>(std::move(condition), std::move(then_branch), std::move(else_branch),
                                             m_filename, start_token.line, start_token.column);
    }

    if (match(lexer::TokenType::For)) {
        if (!match(lexer::TokenType::LParent))
            throw CompilerError("Expected '(' after 'for'", m_filename, peek().line, peek().column);

        std::unique_ptr<Statement> init = nullptr;
        if (!match(lexer::TokenType::Semicolon)) {
            init = parse_statement();
        }

        std::unique_ptr<Expression> condition = nullptr;
        if (!match(lexer::TokenType::Semicolon)) {
            condition = parse_expression();
            if (!match(lexer::TokenType::Semicolon))
                throw CompilerError("Expected ';' after for condition", m_filename, peek().line, peek().column);
        }

        std::unique_ptr<Expression> increment = nullptr;
        if (!match(lexer::TokenType::RParent)) {
            increment = parse_expression();
            if (!match(lexer::TokenType::RParent))
                throw CompilerError("Expected ')' after for increment", m_filename, peek().line, peek().column);
        }

        auto body = parse_block();
        return std::make_unique<ForStatement>(std::move(init), std::move(condition), std::move(increment),
                                              std::move(body), m_filename, start_token.line, start_token.column);
    }

    if (match(lexer::TokenType::Return)) {
        auto expr = parse_expression();
        if (!match(lexer::TokenType::Semicolon)) {
            const auto &token = peek();
            throw CompilerError("Expected ';' after return", m_filename, token.line, token.column,
                                (int)token.lexeme.size());
        }
        return std::make_unique<ReturnStatement>(std::move(expr), m_filename, start_token.line, start_token.column);
    }

    if (check(lexer::TokenType::Int) || check(lexer::TokenType::Coroutine)) {
        DataType type = parse_type();
        if (!check(lexer::TokenType::Identifier)) {
            const auto &token = peek();
            throw CompilerError("Expected variable name after type", m_filename, token.line, token.column,
                                (int)token.lexeme.size());
        }
        const auto &name_token = advance();
        auto name = name_token.lexeme;
        std::unique_ptr<Expression> init = nullptr;
        if (match(lexer::TokenType::Equal)) {
            init = parse_expression();
        }
        if (!match(lexer::TokenType::Semicolon)) {
            const auto &token = peek();
            throw CompilerError("Expected ';' after declaration", m_filename, token.line, token.column,
                                (int)token.lexeme.size());
        }
        return std::make_unique<VariableDeclaration>(type, std::string(name), name_token.line, name_token.column,
                                                     std::move(init), m_filename, start_token.line, start_token.column);
    }

    if (match(lexer::TokenType::Yield)) {
        if (!match(lexer::TokenType::Semicolon)) {
            const auto &token = peek();
            throw CompilerError("Expected ';' after yield", m_filename, token.line, token.column,
                                (int)token.lexeme.size());
        }
        return std::make_unique<YieldStatement>(m_filename, start_token.line, start_token.column);
    }

    // Expression statement as fallback
    auto expr = parse_expression();
    if (!match(lexer::TokenType::Semicolon)) {
        const auto &token = peek();
        throw CompilerError("Expected ';' after expression", m_filename, token.line, token.column,
                            (int)token.lexeme.size());
    }
    return std::make_unique<ExpressionStatement>(std::move(expr), m_filename, start_token.line, start_token.column);
}

std::unique_ptr<Expression> Parser::parse_expression() {
    const auto &start_token = peek();
    if (check(lexer::TokenType::Identifier)) {
        // Lookahead to see if it's an assignment or increment
        if (m_pos + 1 < m_tokens.size()) {
            if (m_tokens[m_pos + 1].type == lexer::TokenType::Equal) {
                const auto &id_token = advance();
                auto lvalue = std::make_unique<VariableExpression>(std::string(id_token.lexeme), m_filename,
                                                                   id_token.line, id_token.column);
                advance();  // skip '='
                return std::make_unique<AssignmentExpression>(std::move(lvalue), parse_expression(), m_filename,
                                                              start_token.line, start_token.column);
            }
            if (m_tokens[m_pos + 1].type == lexer::TokenType::PlusPlus) {
                const auto &id_token = advance();
                auto lvalue = std::make_unique<VariableExpression>(std::string(id_token.lexeme), m_filename,
                                                                   id_token.line, id_token.column);
                advance();  // skip '++'
                return std::make_unique<IncrementExpression>(std::move(lvalue), m_filename, start_token.line,
                                                             start_token.column);
            }
            if (m_tokens[m_pos + 1].type == lexer::TokenType::MinusMinus) {
                const auto &id_token = advance();
                auto lvalue = std::make_unique<VariableExpression>(std::string(id_token.lexeme), m_filename,
                                                                   id_token.line, id_token.column);
                advance();  // skip '--'
                return std::make_unique<DecrementExpression>(std::move(lvalue), m_filename, start_token.line,
                                                             start_token.column);
            }
        }
    }
    if (match(lexer::TokenType::Await)) {
        return std::make_unique<AwaitExpression>(parse_expression(), m_filename, start_token.line, start_token.column);
    }
    if (match(lexer::TokenType::Spawn)) {
        auto expr = parse_expression();
        const auto line = start_token.line;
        const auto col = start_token.column;

        struct CallCheck : ASTVisitor {
            bool is_call = false;
            void visit(const FunctionCall &) override { is_call = true; }
        } checker;
        expr->accept(checker);

        if (!checker.is_call) {
            const auto &token = peek();
            throw CompilerError("Expected function call after spawn", m_filename, token.line, token.column);
        }
        auto call = std::unique_ptr<FunctionCall>(static_cast<FunctionCall *>(expr.release()));
        return std::make_unique<SpawnExpression>(std::move(call), m_filename, line, col);
    }
    return parse_comparison();
}

std::unique_ptr<Expression> Parser::parse_comparison() {
    const auto &start_token = peek();
    auto left = parse_addition();

    while (match(lexer::TokenType::Less) || match(lexer::TokenType::LessEqual) || match(lexer::TokenType::Greater) ||
           match(lexer::TokenType::GreaterEqual) || match(lexer::TokenType::EqualEqual)) {
        const auto &op_token_info = m_tokens[m_pos - 1];
        lexer::TokenType op_token = op_token_info.type;
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
                throw CompilerError("Unknown comparison operator", m_filename, op_token_info.line,
                                    op_token_info.column);
        }
        left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right), m_filename, start_token.line,
                                                  start_token.column);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_addition() {
    const auto &start_token = peek();
    auto left = parse_multiplication();

    while (match(lexer::TokenType::Plus) || match(lexer::TokenType::Minus)) {
        lexer::TokenType op_token = m_tokens[m_pos - 1].type;
        auto right = parse_multiplication();
        BinaryExpression::Op op =
            (op_token == lexer::TokenType::Plus) ? BinaryExpression::Op::Add : BinaryExpression::Op::Sub;
        left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right), m_filename, start_token.line,
                                                  start_token.column);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_multiplication() {
    const auto &start_token = peek();
    auto left = parse_primary();

    while (match(lexer::TokenType::Star) || match(lexer::TokenType::Slash)) {
        lexer::TokenType op_token = m_tokens[m_pos - 1].type;
        auto right = parse_primary();
        BinaryExpression::Op op =
            (op_token == lexer::TokenType::Star) ? BinaryExpression::Op::Mul : BinaryExpression::Op::Div;
        left = std::make_unique<BinaryExpression>(op, std::move(left), std::move(right), m_filename, start_token.line,
                                                  start_token.column);
    }

    return left;
}

std::unique_ptr<Expression> Parser::parse_primary() {
    const auto &token = peek();
    if (match(lexer::TokenType::IntegerLiteral)) {
        int val = std::stoi(std::string(m_tokens[m_pos - 1].lexeme));
        return std::make_unique<IntegerLiteral>(val, m_filename, token.line, token.column);
    }
    if (match(lexer::TokenType::StringLiteral)) {
        return std::make_unique<StringLiteral>(std::string(m_tokens[m_pos - 1].lexeme), m_filename, token.line,
                                               token.column);
    }
    if (match(lexer::TokenType::Identifier)) {
        std::string name = std::string(m_tokens[m_pos - 1].lexeme);
        if (match(lexer::TokenType::LParent)) {
            std::vector<std::unique_ptr<Expression>> args;
            if (!check(lexer::TokenType::RParent)) {
                do {
                    args.push_back(parse_expression());
                } while (match(lexer::TokenType::Comma));
            }
            if (!match(lexer::TokenType::RParent)) {
                const auto &err_tok = peek();
                throw CompilerError("Expected ')' after arguments", m_filename, err_tok.line, err_tok.column,
                                    (int)err_tok.lexeme.size());
            }
            return std::make_unique<FunctionCall>(std::move(name), std::move(args), m_filename, token.line,
                                                  token.column);
        }
        return std::make_unique<VariableExpression>(std::move(name), m_filename, token.line, token.column);
    }
    if (match(lexer::TokenType::LParent)) {
        auto expr = parse_expression();
        if (!match(lexer::TokenType::RParent)) {
            const auto &err_tok = peek();
            throw CompilerError("Expected ')' after expression", m_filename, err_tok.line, err_tok.column,
                                (int)err_tok.lexeme.size());
        }
        return expr;
    }
    const auto &err_token = peek();
    throw CompilerError("Expected expression", m_filename, err_token.line, err_token.column,
                        (int)err_token.lexeme.size());
}

}  // namespace ether::parser
