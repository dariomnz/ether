#ifndef ETHER_ERROR_HPP
#define ETHER_ERROR_HPP

#include <stdexcept>
#include <string>

namespace ether {

class CompilerError : public std::runtime_error {
   public:
    CompilerError(const std::string &message, int line, int col)
        : std::runtime_error(message), line_(line), col_(col) {}

    int line() const { return line_; }
    int col() const { return col_; }

   private:
    int line_;
    int col_;
};

}  // namespace ether

#endif  // ETHER_ERROR_HPP
