#pragma once

#include <string>

namespace ether::lsp {

std::string unescape(const std::string &s);
std::string escape_json(const std::string &s);
std::string get_json_value(const std::string &json, const std::string &key);

}  // namespace ether::lsp
