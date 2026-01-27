#include "server.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
// #define DEBUG
#include "common/debug.hpp"
#include "common/error.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "sema/analyzer.hpp"

namespace ether::lsp {

using namespace ether::parser;

void LSPServer::run() {
    std::cerr << "[LSP] Server started, waiting for messages..." << std::endl;
    while (std::cin) {
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
    } else if (method == "textDocument/didOpen") {
        on_did_open(message);
    } else if (method == "textDocument/didChange") {
        on_did_change(message);
    } else if (method == "textDocument/definition") {
        on_definition(id, message);
    } else if (method == "textDocument/hover") {
        on_hover(id, message);
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
    send_response(id, "{\"capabilities\":{\"textDocumentSync\":1,\"definitionProvider\":true,\"hoverProvider\":true}}");
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

static std::string type_to_string(const ether::parser::DataType& type) {
    switch (type.kind) {
        case ether::parser::DataType::Kind::Int:
            return "int";
        case ether::parser::DataType::Kind::Coroutine:
            return "coroutine";
        case ether::parser::DataType::Kind::Void:
            return "void";
    }
    return "unknown";
}

struct NodeFinder : public ASTVisitor {
    int line;
    int col;
    bool found = false;
    std::string def_filename = "";
    int def_line = 0;
    int def_col = 0;
    std::string hover_info = "";

    void visit(Program& node) override {
        debug_msg("Visiting program " << node.filename);
        for (auto& f : node.functions) {
            if (found) return;
            f->accept(*this);
        }
    }

    void visit(Function& node) override {
        debug_msg("Visiting function " << node.name << " at " << node.filename << ":" << node.line << ":"
                                       << node.column);
        if (found) return;
        if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
            found = true;
            def_filename = node.filename;
            def_line = node.name_line;
            def_col = node.name_col;

            std::stringstream ss;
            ss << "(function) " << type_to_string(node.return_type) << " " << node.name << "(";
            for (size_t i = 0; i < node.params.size(); ++i) {
                ss << type_to_string(node.params[i].type) << " " << node.params[i].name;
                if (i < node.params.size() - 1) ss << ", ";
            }
            ss << ")";
            hover_info = ss.str();
            return;
        }
        if (node.body) node.body->accept(*this);
    }

    void visit(Block& node) override {
        debug_msg("Visiting block at " << node.filename << ":" << node.line << ":" << node.column);
        for (auto& s : node.statements) {
            if (found) return;
            s->accept(*this);
        }
    }

    void visit(VariableDeclaration& node) override {
        debug_msg("Visiting variable declaration " << node.name << " at " << node.filename << ":" << node.line << ":"
                                                   << node.column);
        if (found) return;
        if (node.name_line == line && col >= node.name_col && col < node.name_col + (int)node.name.size()) {
            found = true;
            def_filename = node.filename;
            def_line = node.name_line;
            def_col = node.name_col;
            hover_info = "(variable) " + type_to_string(node.type) + " " + node.name;
            return;
        }
        if (node.init) node.init->accept(*this);
    }

    void visit(FunctionCall& node) override {
        debug_msg("Visiting function call " << node.name << " at " << node.filename << ":" << node.line << ":"
                                            << node.column);
        if (found) return;
        if (node.line == line && col >= node.column && col <= node.column + (int)node.name.size()) {
            found = true;
            def_filename = node.decl_filename;
            def_line = node.decl_line;
            def_col = node.decl_col;
            if (node.type) {
                hover_info = "(call) " + node.name + " -> " + type_to_string(*node.type);
            } else {
                hover_info = "(call) " + node.name;
            }
            return;
        }
        for (auto& a : node.args) {
            if (found) return;
            a->accept(*this);
        }
    }

    void visit(VariableExpression& node) override {
        debug_msg("Visiting variable expression " << node.name << " at " << node.filename << ":" << node.line << ":"
                                                  << node.column);
        if (found) return;
        if (node.line == line && col >= node.column && col <= node.column + (int)node.name.size()) {
            found = true;
            def_filename = node.decl_filename;
            def_line = node.decl_line;
            def_col = node.decl_col;
            if (node.type) {
                hover_info = "(variable) " + type_to_string(*node.type) + " " + node.name;
            } else {
                hover_info = "(variable) " + node.name;
            }
        }
    }

    void visit(IfStatement& node) override {
        debug_msg("Visiting if statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.condition) node.condition->accept(*this);
        if (found) return;
        if (node.then_branch) node.then_branch->accept(*this);
        if (found) return;
        if (node.else_branch) node.else_branch->accept(*this);
    }

    void visit(ForStatement& node) override {
        debug_msg("Visiting for statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.init) node.init->accept(*this);
        if (found) return;
        if (node.condition) node.condition->accept(*this);
        if (found) return;
        if (node.increment) node.increment->accept(*this);
        if (found) return;
        if (node.body) node.body->accept(*this);
    }

    void visit(ReturnStatement& node) override {
        debug_msg("Visiting return statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.expr) node.expr->accept(*this);
    }

    void visit(ExpressionStatement& node) override {
        debug_msg("Visiting expression statement at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.expr) node.expr->accept(*this);
    }

    void visit(BinaryExpression& node) override {
        debug_msg("Visiting binary expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.left) node.left->accept(*this);
        if (found) return;
        if (node.right) node.right->accept(*this);
    }

    void visit(AssignmentExpression& node) override {
        debug_msg("Visiting assignment expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.lvalue) node.lvalue->accept(*this);
        if (found) return;
        if (node.value) node.value->accept(*this);
    }

    void visit(IntegerLiteral& node) override {
        debug_msg("Visiting integer literal at " << node.filename << ":" << node.line << ":" << node.column);
    }
    void visit(StringLiteral& node) override {
        debug_msg("Visiting string literal at " << node.filename << ":" << node.line << ":" << node.column);
    }
    void visit(YieldStatement& node) override {
        debug_msg("Visiting yield statement at " << node.filename << ":" << node.line << ":" << node.column);
    }
    void visit(SpawnExpression& node) override {
        debug_msg("Visiting spawn expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.call) node.call->accept(*this);
    }
    void visit(IncrementExpression& node) override {
        debug_msg("Visiting increment expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(DecrementExpression& node) override {
        debug_msg("Visiting decrement expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(AwaitExpression& node) override {
        debug_msg("Visiting await expression at " << node.filename << ":" << node.line << ":" << node.column);
        if (node.expr) node.expr->accept(*this);
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
    doc.ast->accept(finder);

    if (finder.found) {
        if (!finder.def_filename.empty()) {
            std::cerr << "[LSP] Found definition: " << finder.def_filename << ":" << finder.def_line << ":"
                      << finder.def_col << std::endl;
            std::stringstream ss;
            ss << "{\"uri\":\"file://" << finder.def_filename << "\",\"range\":{"
               << "\"start\":{\"line\":" << (finder.def_line - 1) << ",\"character\":" << (finder.def_col - 1) << "},"
               << "\"end\":{\"line\":" << (finder.def_line - 1) << ",\"character\":" << (finder.def_col - 1 + 5)
               << "}"  // Dummy end
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

void LSPServer::publish_diagnostics(const std::string& filename, const std::vector<ether::CompilerError>& errors) {
    std::stringstream ss;
    ss << "{\"uri\":\"file://" << filename << "\",\"diagnostics\":[";
    for (size_t i = 0; i < errors.size(); ++i) {
        const auto& e = errors[i];
        ss << "{"
           << "\"range\":{"
           << "\"start\":{\"line\":" << (e.line() - 1) << ",\"character\":" << (e.col() - 1) << "},"
           << "\"end\":{\"line\":" << (e.line() - 1) << ",\"character\":" << (e.col()) << "}"
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
        std::cerr << "[LSP] Parsed " << program_ast->functions.size() << " functions." << std::endl;
        ether::sema::Analyzer analyzer;
        analyzer.analyze(*program_ast);

        m_documents[filename] = {source, std::move(program_ast)};
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
