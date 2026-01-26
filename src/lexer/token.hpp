#ifndef ETHER_TOKEN_HPP
#define ETHER_TOKEN_HPP

#include <string>
#include <string_view>

namespace ether::lexer {

enum class TokenType {
    // Keywords
    Int,
    Return,
    If,
    Else,
    While,
    For,
    HashInclude,

    // Identifiers and Literals
    Identifier,
    IntegerLiteral,
    StringLiteral,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Equal,
    EqualEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LParent,
    RParent,
    LBrace,
    RBrace,
    Semicolon,
    Comma,

    // special
    EOF_TOKEN,
    Unknown
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
};

}  // namespace ether::lexer

#endif  // ETHER_TOKEN_HPP
