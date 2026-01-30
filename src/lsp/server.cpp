#include "server.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
// #define DEBUG
#include "common/debug.hpp"
#include "common/error.hpp"
#include "lexer/lexer.hpp"
#include "parser/ast.hpp"
#include "parser/parser.hpp"
#include "sema/analyzer.hpp"

namespace ether::lsp {

using namespace ether::parser;

void LSPServer::run() {
    std::cerr << "[LSP] Server started, waiting for messages..." << std::endl;
    while (m_running && std::cin) {
        std::string line;
        if (!std::getline(std::cin, line)) break;

        if (line.starts_with("Content-Length: ")) {
            int length = std::stoi(line.substr(16));

            while (std::getline(std::cin, line) && !line.empty() && line != "\r") {
            }

            std::string body(length, ' ');
            std::cin.read(&body[0], length);
            std::cerr << "[LSP] Received: " << body << std::endl;
            handle_message(body);
        }
    }
}

static std::string unescape(const std::string& s) {
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

static std::string escape_json(const std::string& s) {
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

static std::string get_json_value(const std::string& json, const std::string& key) {
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

void LSPServer::handle_message(const std::string& message) {
    std::string id = get_json_value(message, "id");
    std::string method = get_json_value(message, "method");
    std::cerr << "[LSP] Handling method: " << method << " (id: " << id << ")" << std::endl;

    if (method == "initialize") {
        on_initialize(id, message);
    } else if (method == "shutdown") {
        on_shutdown(id);
    } else if (method == "exit") {
        on_exit();
    } else if (method == "textDocument/didOpen") {
        on_did_open(message);
    } else if (method == "textDocument/didChange") {
        on_did_change(message);
    } else if (method == "textDocument/definition") {
        on_definition(id, message);
    } else if (method == "textDocument/hover") {
        on_hover(id, message);
    } else if (method == "textDocument/semanticTokens/full") {
        on_semantic_tokens(id, message);
    }
}

void LSPServer::send_response(const std::string& id, const std::string& result) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}";
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
    std::cerr << "[LSP] Sent response: " << body << std::endl;
}

void LSPServer::send_notification(const std::string& method, const std::string& params) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params + "}";
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
    std::cerr << "[LSP] Sent notification: " << body << std::endl;
}

void LSPServer::on_initialize(const std::string& id, const std::string& params) {
    send_response(id,
                  "{\"capabilities\":{"
                  "\"textDocumentSync\":1,"
                  "\"definitionProvider\":true,"
                  "\"hoverProvider\":true,"
                  "\"semanticTokensProvider\":{"
                  "\"legend\":{"
                  "\"tokenTypes\":[\"function\", \"variable\", \"parameter\", \"type\"],"
                  "\"tokenModifiers\":[]"
                  "},"
                  "\"full\":true"
                  "}"
                  "}}");
}

void LSPServer::on_shutdown(const std::string& id) {
    std::cerr << "[LSP] Received shutdown request." << std::endl;
    send_response(id, "null");
}

void LSPServer::on_exit() {
    std::cerr << "[LSP] Received exit notification. Exiting..." << std::endl;
    m_running = false;
}

void LSPServer::on_did_open(const std::string& params) {
    std::string uri = get_json_value(params, "uri");
    std::string path = uri;
    if (path.starts_with("file://")) path = path.substr(7);

    std::string text = get_json_value(params, "text");
    if (!text.empty()) {
        std::cerr << "[LSP] Received text from VS Code for: " << path << std::endl;
        process_file(path, text);
        return;
    }

    std::cerr << "[LSP] Warning: No text in message, falling back to disk for: " << path << std::endl;
    std::ifstream file(path);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        process_file(path, ss.str());
    } else {
        std::cerr << "[LSP] Error: Could not open file at " << path << std::endl;
    }
}

void LSPServer::on_did_change(const std::string& params) {
    // For didChange, we should ideally parse the changes, but for simplicity
    // we can just re-read the file or trigger a re-analysis.
    on_did_open(params);
}

struct NodeFinder : public ConstASTVisitor {
    int line;
    int col;
    bool found = false;
    std::string def_filename = "";
    int def_line = 0;
    int def_col = 0;
    int def_size = 0;
    std::string hover_info = "";
    Program* root_program = nullptr;
    std::string target_filename = "";

    std::string find_struct_in_type(const DataType& type) {
        if (type.kind == DataType::Kind::Struct) return type.struct_name;
        if (type.inner) return find_struct_in_type(*type.inner);
        return "";
    }

    void resolve_struct(const std::string& name) {
        if (!root_program) return;
        for (const auto& s : root_program->structs) {
            if (s->name == name) {
                found = true;
                def_filename = s->filename;
                def_line = s->name_line;
                def_col = s->name_col;
                def_size = (int)s->name.size();

                std::stringstream ss;
                ss << "struct " << s->name << " {\n";
                for (const auto& m : s->members) {
                    ss << "  " << m.type.to_string() << " " << m.name << ";\n";
                }
                ss << "}";
                hover_info = ss.str();
                return;
            }
        }
    }

    void visit(const Program& node) override {
        debug_msg("Visiting program " << node.filename);
        for (auto& inc : node.includes) {
            if (found) return;
            inc->accept(*this);
        }
        for (auto& g : node.globals) {
            if (found) return;
            g->accept(*this);
        }
        for (auto& s : node.structs) {
            if (found) return;
            s->accept(*this);
        }
        for (auto& f : node.functions) {
            if (found) return;
            f->accept(*this);
        }
    }

    void visit(const Function& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting function " << node.name << " at " << node.filename << ":" << node.line << ":"
                                       << node.column);

        if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
            found = true;
            def_filename = node.filename;
            def_line = node.name_line;
            def_col = node.name_col;
            def_size = (int)node.name.size();

            std::stringstream ss;
            ss << "(function) " << node.return_type << " " << node.name << "(";
            for (size_t i = 0; i < node.params.size(); ++i) {
                ss << node.params[i].type << " " << node.params[i].name;
                if (i < node.params.size() - 1 || node.is_variadic) ss << ", ";
            }
            if (node.is_variadic) ss << "...";
            ss << ")";
            hover_info = ss.str();
            return;
        }

        // Return type
        if (node.line == line && col >= node.column && col < node.name_col) {
            std::string s_name = find_struct_in_type(node.return_type);
            if (!s_name.empty()) {
                resolve_struct(s_name);
                if (found) return;
            }
        }

        if (node.body) node.body->accept(*this);
    }

    void visit(const Block& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting block at " << node.filename << ":" << node.line << ":" << node.column);
        for (auto& s : node.statements) {
            if (found) return;
            s->accept(*this);
        }
    }

    void visit(const VariableDeclaration& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting variable declaration " << node.name << " at " << node.filename << ":" << node.line << ":"
                                                   << node.column);
        if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
            found = true;
            def_filename = node.filename;
            def_line = node.name_line;
            def_col = node.name_col;
            def_size = (int)node.name.size();
            hover_info = "(variable) " + node.type.to_string() + " " + node.name;
            return;
        }

        // Check if cursor is on the type area
        if (node.line == line && col >= node.column && col < node.name_col) {
            std::string s_name = find_struct_in_type(node.type);
            if (!s_name.empty()) {
                resolve_struct(s_name);
                if (found) return;
            }
        }

        if (node.init) node.init->accept(*this);
    }

    void visit(const FunctionCall& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting function call " << node.name << " at " << node.filename << ":" << node.line << ":"
                                            << node.column);
        if (node.line == line && col >= node.column && col < node.column + (int)node.name.size()) {
            found = true;
            def_filename = node.decl_filename;
            def_line = node.decl_line;
            def_col = node.decl_col;
            def_size = (int)node.name.size();
            std::stringstream ss;
            ss << "(call) ";
            if (node.type) {
                ss << *node.type << " ";
            }
            ss << node.name << "(";
            for (size_t i = 0; i < node.param_types.size(); ++i) {
                ss << node.param_types[i];
                if (i < node.param_types.size() - 1 || node.is_variadic) ss << ", ";
            }
            if (node.is_variadic) ss << "...";
            ss << ")";
            hover_info = ss.str();
            return;
        }
        for (auto& a : node.args) {
            if (found) return;
            a->accept(*this);
        }
    }

    void visit(const VariableExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting variable expression " << node.name << " at " << node.filename << ":" << node.line << ":"
                                                  << node.column);
        if (node.line == line && col >= node.column && col < node.column + (int)node.name.size()) {
            found = true;
            def_filename = node.decl_filename;
            def_line = node.decl_line;
            def_col = node.decl_col;
            def_size = (int)node.name.size();
            if (node.type) {
                hover_info = "(variable) " + node.type->to_string() + " " + node.name;
            } else {
                hover_info = "(variable) " + node.name;
            }
        }
    }

    void visit(const IfStatement& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting if statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.condition) node.condition->accept(*this);
        if (found) return;
        if (node.then_branch) node.then_branch->accept(*this);
        if (found) return;
        if (node.else_branch) node.else_branch->accept(*this);
    }

    void visit(const ForStatement& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting for statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.init) node.init->accept(*this);
        if (found) return;
        if (node.condition) node.condition->accept(*this);
        if (found) return;
        if (node.increment) node.increment->accept(*this);
        if (found) return;
        if (node.body) node.body->accept(*this);
    }

    void visit(const ReturnStatement& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting return statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.expr) node.expr->accept(*this);
    }

    void visit(const ExpressionStatement& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting expression statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.expr) node.expr->accept(*this);
    }

    void visit(const BinaryExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting binary expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.left) node.left->accept(*this);
        if (found) return;
        if (node.right) node.right->accept(*this);
    }

    void visit(const AssignmentExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting assignment expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.lvalue) node.lvalue->accept(*this);
        if (found) return;
        if (node.value) node.value->accept(*this);
    }

    void visit(const IntegerLiteral& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting integer literal at " << node.filename << ":" << node.line << ":" << node.column);
    }
    void visit(const StringLiteral& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting string literal at " << node.filename << ":" << node.line << ":" << node.column);
    }
    void visit(const YieldStatement& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting yield statement at " << node.filename << ":" << node.line << ":" << node.column);
    }
    void visit(const SpawnExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting spawn expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.call) node.call->accept(*this);
    }
    void visit(const IncrementExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting increment expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const DecrementExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting decrement expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const AwaitExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting await expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.expr) node.expr->accept(*this);
    }

    void visit(const Include& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting include " << node.path << " at " << node.filename << ":" << node.line << ":"
                                      << node.column);
        // Check if cursor is on the #include or the path
        // For simplicity, we match the whole line of the include
        if (node.line == line) {
            found = true;
            def_filename = node.path;
            def_line = 1;
            def_col = 1;
            def_size = 0;  // LSP will just open the file at the start
            hover_info = "include \"" + node.path + "\"";
        }
    }

    void visit(const StructDeclaration& node) override {
        if (found || node.filename != target_filename) return;
        if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
            found = true;
            def_filename = node.filename;
            def_line = node.name_line;
            def_col = node.name_col;
            def_size = (int)node.name.size();
            std::stringstream ss;
            ss << "struct " << node.name << " {\n";
            for (const auto& m : node.members) {
                ss << "  " << m.type.to_string() << " " << m.name << ";\n";
            }
            ss << "}";
            hover_info = ss.str();
        }
    }

    void visit(const MemberAccessExpression& node) override {
        if (found || node.filename != target_filename) return;
        node.object->accept(*this);
        if (found) return;

        // Check if cursor is on the member name
        int member_start = (int)(node.length - node.member_name.size());
        if (node.line == line && col >= node.column + member_start && col < node.column + node.length) {
            found = true;
            if (node.type) {
                hover_info = "(member) " + node.type->to_string() + " " + node.member_name;
            } else {
                hover_info = "(member) " + node.member_name;
            }

            // If we have object type, we can jump to member definition
            if (node.object->type) {
                std::string s_name = find_struct_in_type(*node.object->type);
                if (!s_name.empty() && root_program) {
                    for (const auto& s : root_program->structs) {
                        if (s->name == s_name) {
                            for (const auto& m : s->members) {
                                if (m.name == node.member_name) {
                                    // Found the member!
                                    // Wait, Parameter has no pos, but maybe we can guess from struct definition?
                                    // We'll jump to the struct definition for now as it's better than nothing.
                                    def_filename = s->filename;
                                    def_line = s->name_line;
                                    def_col = s->name_col;
                                    def_size = (int)s->name.size();
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    void visit(const SizeofExpression& node) override {
        if (found || node.filename != target_filename) return;
        if (node.line == line && col >= node.column && col < node.column + node.length) {
            std::string s_name = find_struct_in_type(node.target_type);
            if (!s_name.empty()) {
                resolve_struct(s_name);
            }

            std::string struct_info = hover_info;
            std::stringstream ss;
            if (!struct_info.empty()) {
                ss << struct_info << "\n\n";
            }
            ss << "// Result: " << node.calculated_size << " bytes\n";
            ss << "sizeof(" << node.target_type.to_string() << ")";
            hover_info = ss.str();
            found = true;
        }
    }
    void visit(const IndexExpression& node) override {
        if (found || node.filename != target_filename) return;
        debug_msg("Visiting index expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.object) node.object->accept(*this);
        if (node.index) node.index->accept(*this);
    }

    void visit(const VarargExpression& node) override {
        if (found || node.filename != target_filename) return;
    }
};

void LSPServer::on_definition(const std::string& id, const std::string& params) {
    std::string uri = get_json_value(params, "uri");
    if (uri.starts_with("file://")) uri = uri.substr(7);

    std::string line_str = get_json_value(params, "line");
    std::string char_str = get_json_value(params, "character");

    if (line_str.empty() || char_str.empty()) {
        std::cerr << "[LSP] Error: Missing line/character in definition request" << std::endl;
        send_response(id, "null");
        return;
    }

    if (m_documents.find(uri) == m_documents.end()) {
        std::cerr << "[LSP] Error: Document not found: " << uri << std::endl;
        send_response(id, "null");
        return;
    }

    auto& doc = m_documents[uri];
    if (!doc.ast) {
        std::cerr << "[LSP] Error: No AST available for " << uri << std::endl;
        send_response(id, "null");
        return;
    }

    int line = std::stoi(line_str) + 1;
    int col = std::stoi(char_str) + 1;
    std::cerr << "[LSP] Searching for definition in " << uri << ":" << line << ":" << col << std::endl;

    NodeFinder finder;
    finder.line = line;
    finder.col = col;
    finder.root_program = doc.ast.get();
    finder.target_filename = uri;
    doc.ast->accept(finder);

    if (finder.found) {
        if (!finder.def_filename.empty()) {
            std::cerr << "[LSP] Found definition: " << finder.def_filename << ":" << finder.def_line << ":"
                      << finder.def_col << std::endl;
            std::stringstream ss;
            ss << "{\"uri\":\"file://" << finder.def_filename << "\",\"range\":{"
               << "\"start\":{\"line\":" << (finder.def_line - 1) << ",\"character\":" << (finder.def_col - 1) << "},"
               << "\"end\":{\"line\":" << (finder.def_line - 1)
               << ",\"character\":" << (finder.def_col - 1 + finder.def_size) << "}"
               << "}}";
            send_response(id, ss.str());
            return;
        } else {
            std::cerr << "[LSP] Node found but has no declaration info" << std::endl;
        }
    } else {
        std::cerr << "[LSP] No node found at cursor position" << std::endl;
    }

    send_response(id, "null");
}

void LSPServer::on_hover(const std::string& id, const std::string& params) {
    std::string uri = get_json_value(params, "uri");
    if (uri.starts_with("file://")) uri = uri.substr(7);

    std::string line_str = get_json_value(params, "line");
    std::string char_str = get_json_value(params, "character");

    if (line_str.empty() || char_str.empty()) {
        send_response(id, "null");
        return;
    }

    if (m_documents.find(uri) == m_documents.end()) {
        send_response(id, "null");
        return;
    }

    auto& doc = m_documents[uri];
    if (!doc.ast) {
        send_response(id, "null");
        return;
    }

    int line = std::stoi(line_str) + 1;
    int col = std::stoi(char_str) + 1;

    NodeFinder finder;
    finder.line = line;
    finder.col = col;
    finder.root_program = doc.ast.get();
    finder.target_filename = uri;
    doc.ast->accept(finder);

    if (finder.found && !finder.hover_info.empty()) {
        std::stringstream ss;
        ss << "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```ether\\n"
           << escape_json(finder.hover_info) << "\\n```\"}}";
        send_response(id, ss.str());
    } else {
        send_response(id, "null");
    }
}

struct SemanticToken {
    int line;
    int col;
    int length;
    int type;

    bool operator<(const SemanticToken& other) const {
        if (line != other.line) return line < other.line;
        return col < other.col;
    }
};

struct SemanticTokensVisitor : public ConstASTVisitor {
    std::string target_filename;
    std::vector<SemanticToken> tokens;

    SemanticTokensVisitor(std::string filename) : target_filename(std::move(filename)) {}

    void visit(const Program& node) override {
        for (auto& s : node.structs) s->accept(*this);
        for (auto& g : node.globals) g->accept(*this);
        for (auto& f : node.functions) f->accept(*this);
    }

    void visit(const Function& node) override {
        if (node.filename != target_filename) return;

        // Return type: if struct, highlight as type 3
        if (node.return_type.kind == DataType::Kind::Struct) {
            tokens.push_back({node.line, node.column, (int)node.return_type.struct_name.size(), 3});
        }

        // Function name: type 0
        tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 0});

        // Parameters
        for (const auto& p : node.params) {
            if (p.type.kind == DataType::Kind::Struct) {
                tokens.push_back({p.line, p.col, (int)p.type.struct_name.size(), 3});
            }
            // Parameter name: type 2
            tokens.push_back({p.name_line, p.name_col, (int)p.name.size(), 2});
        }

        if (node.body) node.body->accept(*this);
    }

    void visit(const Block& node) override {
        for (auto& s : node.statements) s->accept(*this);
    }

    void visit(const VariableDeclaration& node) override {
        if (node.filename != target_filename) return;

        // Type: if struct, highlight as type 3
        if (node.type.kind == DataType::Kind::Struct) {
            tokens.push_back({node.line, node.column, (int)node.type.struct_name.size(), 3});
        }

        // Variable name: type 1
        tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 1});
        if (node.init) node.init->accept(*this);
    }

    void visit(const VariableExpression& node) override {
        // Variable reference: type 1
        tokens.push_back({node.line, node.column, (int)node.name.size(), 1});
    }

    void visit(const FunctionCall& node) override {
        // Function call: type 0
        tokens.push_back({node.line, node.column, (int)node.name.size(), 0});
        for (auto& a : node.args) a->accept(*this);
    }

    void visit(const IfStatement& node) override {
        if (node.condition) node.condition->accept(*this);
        if (node.then_branch) node.then_branch->accept(*this);
        if (node.else_branch) node.else_branch->accept(*this);
    }

    void visit(const ForStatement& node) override {
        if (node.init) node.init->accept(*this);
        if (node.condition) node.condition->accept(*this);
        if (node.increment) node.increment->accept(*this);
        if (node.body) node.body->accept(*this);
    }

    void visit(const ReturnStatement& node) override {
        if (node.expr) node.expr->accept(*this);
    }

    void visit(const ExpressionStatement& node) override {
        if (node.expr) node.expr->accept(*this);
    }

    void visit(const BinaryExpression& node) override {
        if (node.left) node.left->accept(*this);
        if (node.right) node.right->accept(*this);
    }

    void visit(const AssignmentExpression& node) override {
        if (node.lvalue) node.lvalue->accept(*this);
        if (node.value) node.value->accept(*this);
    }

    void visit(const IntegerLiteral& node) override {}
    void visit(const StringLiteral& node) override {}
    void visit(const YieldStatement& node) override {}

    void visit(const SpawnExpression& node) override {
        if (node.call) node.call->accept(*this);
    }

    void visit(const IncrementExpression& node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }

    void visit(const DecrementExpression& node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }

    void visit(const AwaitExpression& node) override {
        if (node.expr) node.expr->accept(*this);
    }

    void visit(const StructDeclaration& node) override {
        if (node.filename != target_filename) return;
        // Struct name: type 3 (type)
        tokens.push_back({node.name_line, node.name_col, (int)node.name.size(), 3});

        // Members
        for (const auto& m : node.members) {
            if (m.type.kind == DataType::Kind::Struct) {
                tokens.push_back({m.line, m.col, (int)m.type.struct_name.size(), 3});
            }
            // Member name: type 1 (variable/member)
            tokens.push_back({m.name_line, m.name_col, (int)m.name.size(), 1});
        }
    }

    void visit(const MemberAccessExpression& node) override {
        node.object->accept(*this);
        // Member name: type 1 (variable)
        int member_start = (int)(node.length - node.member_name.size());
        tokens.push_back({node.line, node.column + member_start, (int)node.member_name.size(), 1});
    }

    void visit(const SizeofExpression& node) override {
        if (node.filename != target_filename) return;
        if (node.target_type.kind == DataType::Kind::Struct) {
            tokens.push_back({node.type_line, node.type_col, (int)node.target_type.struct_name.size(), 3});
        }
    }

    void visit(const IndexExpression& node) override {
        if (node.object) node.object->accept(*this);
        if (node.index) node.index->accept(*this);
    }

    void visit(const Include& node) override {
        if (node.filename != target_filename) return;
    }
    void visit(const VarargExpression& node) override {
        if (node.filename != target_filename) return;
    }
};

void LSPServer::on_semantic_tokens(const std::string& id, const std::string& params) {
    std::string uri = get_json_value(params, "uri");
    if (uri.starts_with("file://")) uri = uri.substr(7);

    if (m_documents.find(uri) == m_documents.end()) {
        send_response(id, "{\"data\":[]}");
        return;
    }

    auto& doc = m_documents[uri];
    if (!doc.ast) {
        send_response(id, "{\"data\":[]}");
        return;
    }

    SemanticTokensVisitor visitor(uri);
    doc.ast->accept(visitor);
    std::sort(visitor.tokens.begin(), visitor.tokens.end());

    std::vector<int> data;
    int last_line = 1;
    int last_col = 1;

    for (const auto& token : visitor.tokens) {
        int delta_line = token.line - last_line;
        int delta_start = (delta_line == 0) ? (token.col - last_col) : (token.col - 1);

        data.push_back(delta_line);
        data.push_back(delta_start);
        data.push_back(token.length);
        data.push_back(token.type);
        data.push_back(0);  // modifiers

        last_line = token.line;
        last_col = token.col;
    }

    std::stringstream ss;
    ss << "{\"data\":[";
    for (size_t i = 0; i < data.size(); ++i) {
        ss << data[i];
        if (i < data.size() - 1) ss << ",";
    }
    ss << "]}";
    send_response(id, ss.str());
}

void LSPServer::publish_diagnostics(const std::string& filename, const std::vector<ether::CompilerError>& errors) {
    std::stringstream ss;
    ss << "{\"uri\":\"file://" << filename << "\",\"diagnostics\":[";
    for (size_t i = 0; i < errors.size(); ++i) {
        const auto& e = errors[i];
        ss << "{"
           << "\"range\":{"
           << "\"start\":{\"line\":" << (e.line() - 1) << ",\"character\":" << (e.col() - 1) << "},"
           << "\"end\":{\"line\":" << (e.line() - 1) << ",\"character\":" << (e.col() - 1 + e.length()) << "}"
           << "},"
           << "\"severity\":1,"
           << "\"message\":\"" << escape_json(e.what()) << "\""
           << "}";
        if (i < errors.size() - 1) ss << ",";
    }
    ss << "]}";
    send_notification("textDocument/publishDiagnostics", ss.str());
}

void LSPServer::process_file(const std::string& filename, const std::string& source) {
    try {
        std::cerr << "[LSP] Analyzing file: " << filename << std::endl;
        ether::lexer::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();
        ether::parser::Parser parser(tokens, filename);
        auto program_ast = parser.parse_program();

        // Store AST now, so even if sema fails, the LSP has the latest structural info
        auto* ast_ptr = program_ast.get();
        m_documents[filename] = {source, std::move(program_ast)};

        std::cerr << "[LSP] Parsed " << ast_ptr->functions.size() << " functions." << std::endl;
        ether::sema::Analyzer analyzer;
        analyzer.analyze(*ast_ptr);

        std::cerr << "[LSP] Analysis complete for " << filename << std::endl;
        publish_diagnostics(filename, {});
    } catch (const CompilerError& e) {
        std::cerr << "[LSP] Sema error during analysis: " << e.what() << " at " << e.line() << ":" << e.col()
                  << std::endl;
        publish_diagnostics(filename, {e});
    } catch (const std::exception& e) {
        std::cerr << "[LSP] Error during analysis: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[LSP] Unknown error during analysis" << std::endl;
    }
}

}  // namespace ether::lsp
