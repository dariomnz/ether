#ifndef ETHER_LEXER_HPP
#define ETHER_LEXER_HPP

#include <string_view>
#include <vector>

#include "token.hpp"

namespace ether::lexer {

class Lexer {
   public:
    explicit Lexer(std::string_view source);
    std::vector<Token> tokenize();

   private:
    std::string_view m_source;
    size_t m_pos = 0;
    int m_line = 1;
    int m_col = 1;

    char peek() const;
    char advance();
    void skip_whitespace();
    Token next_token();
};

}  // namespace ether::lexer

#endif  // ETHER_LEXER_HPP
