#ifndef ETHER_ERROR_HPP
#define ETHER_ERROR_HPP

#include <stdexcept>
#include <string>

namespace ether {

class CompilerError : public std::runtime_error {
   public:
    CompilerError(const std::string &message, std::string filename, int line, int col, int length = 1)
        : std::runtime_error(message), m_filename(std::move(filename)), m_line(line), m_col(col), m_length(length) {}

    const std::string &filename() const { return m_filename; }
    int line() const { return m_line; }
    int col() const { return m_col; }
    int length() const { return m_length; }

   private:
    std::string m_filename;
    int m_line;
    int m_col;
    int m_length;
};

}  // namespace ether

#endif  // ETHER_ERROR_HPP
