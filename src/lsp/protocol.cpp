#include "lsp/protocol.hpp"

#include <cctype>
#include <string>

namespace ether::lsp {

std::string unescape(const std::string &s) {
    std::string res;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[++i]) {
                case '"':
                    res += '"';
                    break;
                case '\\':
                    res += '\\';
                    break;
                case 'n':
                    res += '\n';
                    break;
                case 'r':
                    res += '\r';
                    break;
                case 't':
                    res += '\t';
                    break;
                default:
                    res += s[i];
                    break;
            }
        } else {
            res += s[i];
        }
    }
    return res;
}

std::string escape_json(const std::string &s) {
    std::string res;
    for (char c : s) {
        if (c == '"')
            res += "\\\"";
        else if (c == '\\')
            res += "\\\\";
        else if (c == '\n')
            res += "\\n";
        else if (c == '\r')
            res += "\\r";
        else if (c == '\t')
            res += "\\t";
        else
            res += c;
    }
    return res;
}

std::string get_json_value(const std::string &json, const std::string &key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && std::isspace(json[pos])) pos++;

    if (pos < json.size() && json[pos] == '"') {
        // String value
        pos++;
        size_t start = pos;
        while (pos < json.size()) {
            if (json[pos] == '"' && json[pos - 1] != '\\') break;
            pos++;
        }
        return unescape(json.substr(start, pos - start));
    } else {
        // Simple value (number, null, etc.)
        size_t start = pos;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') pos++;
        std::string val = json.substr(start, pos - start);
        // Trim whitespace
        val.erase(val.find_last_not_of(" \n\r\t") + 1);
        return val;
    }
}

}  // namespace ether::lsp
