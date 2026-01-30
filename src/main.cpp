#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "common/error.hpp"
#include "driver/driver_utils.hpp"
#include "ir/disassembler.hpp"
#include "ir/ir.hpp"
#include "ir/ir_gen.hpp"
#include "lexer/lexer.hpp"
#include "lsp/server.hpp"
#include "parser/parser.hpp"
#include "sema/analyzer.hpp"
#include "test_runner/test_runner.hpp"
#include "vm/vm.hpp"

using Clock = std::chrono::high_resolution_clock;
using time_point = decltype(std::chrono::high_resolution_clock::now());
auto cast_micro(auto t) { return std::chrono::duration_cast<std::chrono::microseconds>(t); }

int main(int argc, char *argv[]) {
    if (argc < 2) {
        ether::driver::print_usage();
        return 1;
    }

    std::string first_arg = argv[1];
    if (first_arg == "-h" || first_arg == "--help") {
        ether::driver::print_usage();
        return 0;
    }
    if (first_arg == "--test") {
        if (argc < 3) {
            std::cerr << "Error: --test requires a directory or file path" << std::endl;
            return 1;
        }
        std::string test_path = argv[2];
        ether::TestOptions options;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-j" && i + 1 < argc) {
                options.parallel_jobs = std::stoi(argv[++i]);
                if (options.parallel_jobs == 0) {
                    options.parallel_jobs = std::thread::hardware_concurrency();
                }
            } else if (arg == "--quiet" || arg == "-q") {
                options.quiet = true;
            }
        }
        return ether::run_tests(argv[0], test_path, options);
    }

    if (first_arg == "--lsp") {
        ether::lsp::LSPServer server;
        server.run();
        return 0;
    }

    std::string filename = first_arg;
    bool dump_ir = false;
    bool show_stats = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-ir") {
            dump_ir = true;
        } else if (arg == "--stats") {
            show_stats = true;
        }
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << filename << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    time_point start_total, end_total, t1, t2, t3, t4, t5, t6, t7;
    try {
        if (show_stats) start_total = Clock::now();

        if (show_stats) t1 = Clock::now();
        ether::lexer::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();
        if (show_stats) t2 = Clock::now();

        ether::parser::Parser parser(tokens, filename);
        auto program_ast = parser.parse_program();
        if (show_stats) t3 = Clock::now();

        ether::sema::Analyzer analyzer;
        analyzer.analyze(*program_ast);
        if (show_stats) t4 = Clock::now();

        ether::ir_gen::IRGenerator ir_gen;
        ether::ir::IRProgram program = ir_gen.generate(*program_ast);
        if (show_stats) t5 = Clock::now();

        if (dump_ir) {
            ether::ir::disassemble(program);
            return 0;
        }

        if (show_stats) t6 = Clock::now();
        ether::vm::VM vm(program);
        ether::vm::Value result = vm.run(show_stats);
        if (show_stats) t7 = Clock::now();
        if (show_stats) end_total = Clock::now();

        std::cout << "VM Execution Result: " << result << std::endl;

        if (show_stats) {
            auto total_ms = cast_micro(end_total - start_total).count() / 1000.0;
            auto lex_ms = cast_micro(t2 - t1).count() / 1000.0;
            auto parse_ms = cast_micro(t3 - t2).count() / 1000.0;
            auto sema_ms = cast_micro(t4 - t3).count() / 1000.0;
            auto ir_ms = cast_micro(t5 - t4).count() / 1000.0;
            auto vm_ms = cast_micro(t7 - t6).count() / 1000.0;

            ether::driver::print_stats(vm, total_ms, lex_ms, parse_ms, sema_ms, ir_ms, vm_ms);
        }
    } catch (const ether::CompilerError &e) {
        ether::driver::report_error(filename, source, e);
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
