#define ETHER_LEXER_CPP
#include "lexer.hpp"

#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/error.hpp"


namespace ether::lexer {

static const std::unordered_map<std::string_view, TokenType> keywords = {
    {"int", TokenType::Int},     {"return", TokenType::Return},      {"if", TokenType::If},
    {"else", TokenType::Else},   {"while", TokenType::While},        {"for", TokenType::For},
    {"string", TokenType::Int},  {"spawn", TokenType::Spawn},        {"yield", TokenType::Yield},
    {"await", TokenType::Await}, {"coroutine", TokenType::Coroutine}};

Lexer::Lexer(std::string_view source, std::string filename) : m_source(source), m_filename(std::move(filename)) {}

char Lexer::peek() const {
    if (m_pos >= m_source.size()) return '\0';
    return m_source[m_pos];
}

char Lexer::advance() {
    char c = peek();
    m_pos++;
    m_col++;
    if (c == '\n') {
        m_line++;
        m_col = 1;
    }
    return c;
}

void Lexer::skip_whitespace() {
    while (true) {
        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else if (c == '/' && m_source.substr(m_pos).starts_with("//")) {
            while (peek() != '\n' && peek() != '\0') {
                advance();
            }
        } else {
            break;
        }
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (peek() != '\0') {
        skip_whitespace();
        if (peek() == '\0') break;
        tokens.push_back(next_token());
    }
    tokens.push_back({TokenType::EOF_TOKEN, "", m_line, m_col});
    return tokens;
}

Token Lexer::next_token() {
    char c = peek();
    int start_col = m_col;
    int start_line = m_line;

    if (c == '#') {
        size_t start_pos = m_pos;
        advance();  // skip '#'
        while (std::isalpha(peek())) advance();
        std::string lexeme(m_source.substr(start_pos, m_pos - start_pos));
        if (lexeme == "#include") {
            return {TokenType::HashInclude, lexeme, start_line, start_col};
        }
        return {TokenType::Unknown, lexeme, start_line, start_col};
    }

    if (std::isalpha(c) || c == '_') {
        size_t start_pos = m_pos;
        while (std::isalnum(peek()) || peek() == '_') advance();
        std::string lexeme(m_source.substr(start_pos, m_pos - start_pos));

        if (keywords.contains(lexeme)) {
            return {keywords.at(lexeme), lexeme, start_line, start_col};
        }
        return {TokenType::Identifier, lexeme, start_line, start_col};
    }

    if (std::isdigit(c)) {
        size_t start_pos = m_pos;
        while (std::isdigit(peek())) advance();
        return {TokenType::IntegerLiteral, std::string(m_source.substr(start_pos, m_pos - start_pos)), start_line,
                start_col};
    }

    if (c == '"') {
        advance();  // skip opening "
        std::string value;
        while (peek() != '"' && peek() != '\0') {
            if (peek() == '\\') {
                advance();  // skip '\'
                char escaped = advance();
                switch (escaped) {
                    case 'n':
                        value += '\n';
                        break;
                    case 't':
                        value += '\t';
                        break;
                    case 'r':
                        value += '\r';
                        break;
                    case '\\':
                        value += '\\';
                        break;
                    case '"':
                        value += '"';
                        break;
                    default:
                        value += escaped;
                        break;
                }
            } else {
                value += advance();
            }
        }
        if (peek() == '"') {
            advance();  // skip closing "
            return {TokenType::StringLiteral, value, start_line, start_col};
        } else {
            throw CompilerError("Unterminated string literal", m_filename, start_line, start_col);
        }
    }

    advance();
    std::string lexeme(m_source.substr(m_pos - 1, 1));

    switch (c) {
        case '+': {
            if (peek() == '+') {
                advance();
                return {TokenType::PlusPlus, "++", start_line, start_col};
            }
            return {TokenType::Plus, lexeme, start_line, start_col};
        }
        case '-': {
            if (peek() == '-') {
                advance();
                return {TokenType::MinusMinus, "--", start_line, start_col};
            }
            return {TokenType::Minus, lexeme, start_line, start_col};
        }
        case '*':
            return {TokenType::Star, lexeme, start_line, start_col};
        case '/':
            return {TokenType::Slash, lexeme, start_line, start_col};
        case ';':
            return {TokenType::Semicolon, lexeme, start_line, start_col};
        case ',':
            return {TokenType::Comma, lexeme, start_line, start_col};
        case '(':
            return {TokenType::LParent, lexeme, start_line, start_col};
        case ')':
            return {TokenType::RParent, lexeme, start_line, start_col};
        case '{':
            return {TokenType::LBrace, lexeme, start_line, start_col};
        case '}':
            return {TokenType::RBrace, lexeme, start_line, start_col};
        case '=': {
            if (peek() == '=') {
                advance();
                return {TokenType::EqualEqual, "==", start_line, start_col};
            }
            return {TokenType::Equal, "=", start_line, start_col};
        }
        case '<': {
            if (peek() == '=') {
                advance();
                return {TokenType::LessEqual, "<=", start_line, start_col};
            }
            return {TokenType::Less, "<", start_line, start_col};
        }
        case '>': {
            if (peek() == '=') {
                advance();
                return {TokenType::GreaterEqual, ">=", start_line, start_col};
            }
            return {TokenType::Greater, ">", start_line, start_col};
        }
    }

    return {TokenType::Unknown, lexeme, start_line, start_col};
}

}  // namespace ether::lexer
