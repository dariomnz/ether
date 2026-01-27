#ifndef ETHER_LSP_SERVER_HPP
#define ETHER_LSP_SERVER_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/error.hpp"
#include "parser/ast.hpp"

namespace ether::lsp {

class LSPServer {
   public:
    void run();

   private:
    void handle_message(const std::string& message);
    void send_response(const std::string& id, const std::string& result);
    void send_notification(const std::string& method, const std::string& params);

    void on_initialize(const std::string& id, const std::string& params);
    void on_did_open(const std::string& params);
    void on_did_change(const std::string& params);
    void on_definition(const std::string& id, const std::string& params);
    void on_hover(const std::string& id, const std::string& params);
    void on_semantic_tokens(const std::string& id, const std::string& params);
    void publish_diagnostics(const std::string& filename, const std::vector<ether::CompilerError>& errors);

    struct Document {
        std::string source;
        std::unique_ptr<parser::Program> ast;
    };

    std::unordered_map<std::string, Document> m_documents;

    void process_file(const std::string& filename, const std::string& source);
};

}  // namespace ether::lsp

#endif  // ETHER_LSP_SERVER_HPP
