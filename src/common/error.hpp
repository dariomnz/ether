#ifndef ETHER_ERROR_HPP
#define ETHER_ERROR_HPP

#include <stdexcept>
#include <string>

namespace ether {

class CompilerError : public std::runtime_error {
   public:
    CompilerError(const std::string &message, std::string filename, int line, int col)
        : std::runtime_error(message), m_filename(std::move(filename)), m_line(line), m_col(col) {}

    const std::string &filename() const { return m_filename; }
    int line() const { return m_line; }
    int col() const { return m_col; }

   private:
    std::string m_filename;
    int m_line;
    int m_col;
};

}  // namespace ether

#endif  // ETHER_ERROR_HPP
