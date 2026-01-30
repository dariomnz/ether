#include "server.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

#include "common/debug.hpp"
#include "common/error.hpp"
#include "lexer/lexer.hpp"
#include "lsp/node_finder.hpp"
#include "lsp/protocol.hpp"
#include "lsp/semantic_tokens.hpp"
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
    } else if (method == "textDocument/completion") {
        on_completion(id, message);
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
                  "},"
                  "\"completionProvider\":{"
                  "\"triggerCharacters\":[\".\"]"
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
    std::stable_sort(visitor.tokens.begin(), visitor.tokens.end());

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
    // Update source immediately so we have the latest text even if parsing fails
    if (m_documents.find(filename) == m_documents.end()) {
        m_documents[filename] = {source, nullptr};
    } else {
        m_documents[filename].source = source;
    }

    try {
        std::cerr << "[LSP] Analyzing file: " << filename << std::endl;
        ether::lexer::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();
        ether::parser::Parser parser(tokens, filename);
        auto program_ast = parser.parse_program();

        // Store AST now, so even if sema fails, the LSP has the latest structural info
        auto* ast_ptr = program_ast.get();
        m_documents[filename].ast = std::move(program_ast);

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

void LSPServer::on_completion(const std::string& id, const std::string& params) {
    std::string uri = get_json_value(params, "uri");
    if (uri.starts_with("file://")) uri = uri.substr(7);

    if (m_documents.find(uri) == m_documents.end()) {
        send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
        return;
    }

    auto& doc = m_documents[uri];
    if (doc.source.empty()) {
        send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
        return;
    }

    std::string line_str = get_json_value(params, "line");
    std::string char_str = get_json_value(params, "character");
    int line = std::stoi(line_str);
    int col = std::stoi(char_str);

    // Extract line content
    std::stringstream ss(doc.source);
    std::string content_line;
    for (int i = 0; i <= line; ++i) {
        if (!std::getline(ss, content_line)) break;
    }

    if (content_line.empty() || col > (int)content_line.size()) {
        send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
        return;
    }

    // Check providing loop for '.'
    if (!content_line.empty() && content_line.back() == '\r') content_line.pop_back();

    // Clamp col just in case
    if (col > (int)content_line.size()) col = (int)content_line.size();

    int i = col - 1;
    while (i >= 0 && std::isspace(content_line[i])) i--;

    if (i < 0 || content_line[i] != '.') {
        send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
        return;
    }
    i--;  // skip dot

    while (i >= 0 && std::isspace(content_line[i])) i--;
    if (i < 0) {
        send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
        return;
    }

    // Find end of identifier or ')' or ']'
    int target_col = -1;
    if (isalnum(content_line[i]) || content_line[i] == '_') {
        target_col = i + 1;  // 1-based, points to last char of identifier (or within it)
    } else if (content_line[i] == ')') {
        int balance = 1;
        i--;
        while (i >= 0 && balance > 0) {
            if (content_line[i] == ')')
                balance++;
            else if (content_line[i] == '(')
                balance--;
            i--;
        }
        if (balance == 0) {
            while (i >= 0 && std::isspace(content_line[i])) i--;
            if (i >= 0 && (isalnum(content_line[i]) || content_line[i] == '_')) {
                target_col = i + 1;
            }
        }
    } else if (content_line[i] == ']') {
        int balance = 1;
        i--;
        while (i >= 0 && balance > 0) {
            if (content_line[i] == ']')
                balance++;
            else if (content_line[i] == '[')
                balance--;
            i--;
        }
        if (balance == 0) {
            while (i >= 0 && std::isspace(content_line[i])) i--;
            if (i >= 0 && (isalnum(content_line[i]) || content_line[i] == '_')) {
                target_col = i + 1;
            }
        }
    }

    if (target_col == -1) {
        send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
        return;
    }

    // If we have no AST, we can't do anything
    if (!doc.ast) {
        send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
        return;
    }

    NodeFinder finder;
    finder.line = line + 1;
    finder.col = target_col;
    finder.root_program = doc.ast.get();
    finder.target_filename = uri;
    doc.ast->accept(finder);

    if (finder.found && finder.found_type) {
        std::string s_name = finder.find_struct_in_type(*finder.found_type);
        if (!s_name.empty()) {
            // Find struct definition
            for (const auto& s : doc.ast->structs) {
                if (s->name == s_name) {
                    std::stringstream json;
                    json << "{\"isIncomplete\":false,\"items\":[";
                    bool first = true;

                    // Add struct members
                    for (const auto& m : s->members) {
                        if (!first) json << ",";
                        json << "{"
                             << "\"label\":\"" << escape_json(m.name) << "\","
                             << "\"kind\":5,"  // Field
                             << "\"detail\":\"" << escape_json(m.type.to_string()) << "\""
                             << "}";
                        first = false;
                    }

                    // Add struct methods
                    for (const auto& f : doc.ast->functions) {
                        if (!f->struct_name.empty() && f->struct_name == s_name) {
                            if (!first) json << ",";

                            // Build method signature
                            std::stringstream sig;
                            sig << f->return_type.to_string() << " " << f->name << "(";
                            bool first_param = true;
                            // Skip first parameter (this pointer)
                            for (size_t i = 1; i < f->params.size(); ++i) {
                                if (!first_param) sig << ", ";
                                sig << f->params[i].type.to_string() << " " << f->params[i].name;
                                first_param = false;
                            }
                            if (f->is_variadic) {
                                if (!first_param) sig << ", ";
                                sig << "...";
                            }
                            sig << ")";

                            json << "{"
                                 << "\"label\":\"" << escape_json(f->name) << "\","
                                 << "\"kind\":2,"  // Method
                                 << "\"detail\":\"" << escape_json(sig.str()) << "\""
                                 << "}";
                            first = false;
                        }
                    }

                    json << "]}";
                    send_response(id, json.str());
                    return;
                }
            }
        }
    }

    send_response(id, "{\"isIncomplete\":false,\"items\":[]}");
}

}  // namespace ether::lsp
