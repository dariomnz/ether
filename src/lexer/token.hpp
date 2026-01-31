#ifndef ETHER_TOKEN_HPP
#define ETHER_TOKEN_HPP

#include <string>

namespace ether::lexer {

enum class TokenType {
    // Keywords
    I64,
    I32,
    I16,
    I8,
    F64,
    F32,
    Return,
    If,
    Else,
    While,
    For,
    Coroutine,
    HashInclude,
    Spawn,
    Yield,
    Await,
    String,
    Ptr,
    Void,
    Struct,
    Sizeof,

    // Identifiers and Literals
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,

    // Operators
    Plus,
    PlusPlus,
    Minus,
    MinusMinus,
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
    LBracket,
    RBracket,
    Semicolon,
    Comma,
    Ellipsis,
    Dot,
    ColonColon,

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
