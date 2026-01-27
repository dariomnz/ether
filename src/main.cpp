#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "common/error.hpp"
#include "ir/ir.hpp"
#include "ir/ir_gen.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "test_runner/test_runner.hpp"
#include "vm/vm.hpp"

using Clock = std::chrono::high_resolution_clock;

void report_error(const std::string &filename, const std::string &source, const ether::CompilerError &e) {
    std::cerr << filename << ":" << e.line() << ":" << e.col() << ": error: " << e.what() << std::endl;

    std::stringstream ss(source);
    std::string line;
    for (int i = 1; i <= e.line(); ++i) {
        if (!std::getline(ss, line)) break;
    }

    std::cerr << "  " << line << std::endl;
    std::cerr << "  ";
    for (int i = 1; i < e.col(); ++i) {
        std::cerr << " ";
    }
    std::cerr << "^" << std::endl;
}

void printLiteral(const std::string_view &texto) {
    std::cout << "\"";
    for (char c : texto) {
        switch (c) {
            case '\n':
                std::cout << "\\n";
                break;
            case '\t':
                std::cout << "\\t";
                break;
            case '\r':
                std::cout << "\\r";
                break;
            case '\\':
                std::cout << "\\\\";
                break;
            default:
                std::cout << c;
                break;
        }
    }
    std::cout << "\"";
}

void disassemble(const ether::ir::IRProgram &program) {
    std::cout << "\nBytecode Disassembly:" << std::endl;
    const uint8_t *code = program.bytecode.data();
    size_t ip = 0;

    // Create a mapping from address to function name/info for display
    std::unordered_map<size_t, std::pair<std::string, ether::ir::IRProgram::FunctionInfo>> addr_to_func;
    for (const auto &[name, info] : program.functions) {
        addr_to_func[info.entry_addr] = {name, info};
    }

    while (ip < program.bytecode.size()) {
        size_t addr = ip;

        // Check if this address is a function entry point
        if (addr_to_func.contains(addr)) {
            const auto &[name, info] = addr_to_func.at(addr);
            std::cout << "\n<function: " << name << "> (params: " << (int)info.num_params
                      << ", slots: " << (int)info.num_slots << ")" << std::endl;
        }

        uint8_t op_byte = code[ip++];
        ether::ir::OpCode op = static_cast<ether::ir::OpCode>(op_byte);

        std::cout << std::right << std::setw(4) << addr << ": " << std::left << std::setw(15) << op;

        switch (op) {
            case ether::ir::OpCode::PUSH_INT: {
                int32_t val = *(int32_t *)&code[ip];
                ip += 4;
                std::cout << val;
                break;
            }
            case ether::ir::OpCode::PUSH_STR: {
                uint32_t id = *(uint32_t *)&code[ip];
                ip += 4;
                printLiteral(program.string_pool[id]);
                break;
            }
            case ether::ir::OpCode::STORE_VAR:
            case ether::ir::OpCode::LOAD_VAR: {
                uint8_t slot = code[ip++];
                std::cout << "slot " << (int)slot;
                break;
            }
            case ether::ir::OpCode::SYS_PRINTF: {
                uint8_t num_args = code[ip++];
                std::cout << "args " << (int)num_args;
                break;
            }
            case ether::ir::OpCode::CALL:
            case ether::ir::OpCode::SPAWN: {
                uint32_t target = *(uint32_t *)&code[ip];
                ip += 4;
                std::cout << "addr " << target;
                if (addr_to_func.contains(target)) {
                    std::cout << " <" << addr_to_func.at(target).first << ">";
                }
                break;
            }
            case ether::ir::OpCode::YIELD: {
                break;
            }
            case ether::ir::OpCode::JMP:
            case ether::ir::OpCode::JZ: {
                uint32_t target = *(uint32_t *)&code[ip];
                ip += 4;
                std::cout << "addr " << target;
                break;
            }
            default:
                break;
        }
        std::cout << std::endl;
    }
}

void print_stats(const ether::vm::VM &vm, double total_ms, double lex_ms, double parse_ms, double ir_ms, double vm_ms) {
    std::cout << "\nPhase Timings:" << std::endl;
    std::cout << std::left << std::setw(15) << "Phase" << "Time (ms)" << std::endl;
    std::cout << std::string(30, '-') << std::endl;
    std::cout << std::left << std::setw(15) << "Tokenizing" << std::fixed << std::setprecision(3) << lex_ms << " ms"
              << std::endl;
    std::cout << std::left << std::setw(15) << "Parsing" << parse_ms << " ms" << std::endl;
    std::cout << std::left << std::setw(15) << "IR Gen" << ir_ms << " ms" << std::endl;
    std::cout << std::left << std::setw(15) << "VM Run" << vm_ms << " ms" << std::endl;
    std::cout << std::string(30, '-') << std::endl;
    std::cout << std::left << std::setw(15) << "Total" << total_ms << " ms" << std::endl;

    std::cout << "\nExecution Statistics (Sorted by Total Time):" << std::endl;
    std::cout << std::left << std::setw(15) << "OpCode" << std::setw(10) << "Count" << std::setw(15) << "Time (ms)"
              << "Avg (ns)" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    std::vector<std::pair<ether::ir::OpCode, ether::vm::OpCodeStats>> sorted_stats;
    for (const auto &item : vm.get_stats()) {
        sorted_stats.push_back(item);
    }

    std::sort(sorted_stats.begin(), sorted_stats.end(),
              [](const auto &a, const auto &b) { return a.second.total_time > b.second.total_time; });

    uint64_t total_count = 0;
    std::chrono::nanoseconds total_time_ns{0};

    for (const auto &[op, stats] : sorted_stats) {
        total_count += stats.count;
        total_time_ns += stats.total_time;

        double ms = stats.total_time.count() / 1000000.0;
        double avg = stats.count > 0 ? (double)stats.total_time.count() / stats.count : 0;
        std::cout << std::left << std::setw(15) << op << std::setw(10) << stats.count << std::fixed
                  << std::setprecision(3) << std::setw(15) << ms << std::setprecision(1) << avg << std::endl;
    }

    std::cout << std::string(50, '-') << std::endl;
    double total_ms_val = total_time_ns.count() / 1000000.0;
    double total_avg = total_count > 0 ? (double)total_time_ns.count() / total_count : 0;
    std::cout << std::left << std::setw(15) << "Total" << std::setw(10) << total_count << std::fixed
              << std::setprecision(3) << std::setw(15) << total_ms_val << std::setprecision(1) << total_avg
              << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ether <filename|--test> [path] [--dump-ir] [--stats]" << std::endl;
        return 1;
    }

    std::string first_arg = argv[1];
    if (first_arg == "--test") {
        if (argc < 3) {
            std::cerr << "Error: --test requires a directory or file path" << std::endl;
            return 1;
        }
        return ether::run_tests(argv[0], argv[2]);
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

    try {
        auto start_total = Clock::now();

        auto t1 = Clock::now();
        ether::lexer::Lexer lexer(source);
        auto tokens = lexer.tokenize();
        auto t2 = Clock::now();

        ether::parser::Parser parser(tokens);
        auto program_ast = parser.parse_program();
        auto t3 = Clock::now();

        ether::ir_gen::IRGenerator ir_gen;
        ether::ir::IRProgram program = ir_gen.generate(*program_ast);
        auto t4 = Clock::now();

        if (dump_ir) {
            std::cout << "Bytecode Size: " << program.bytecode.size() << " bytes" << std::endl;
            std::cout << "String Pool Size: " << program.string_pool.size() << " entries" << std::endl;
            std::cout << "Functions:" << std::endl;
            for (const auto &[name, info] : program.functions) {
                std::cout << "  " << name << " @ " << info.entry_addr << " (Params: " << (int)info.num_params
                          << ", Slots: " << (int)info.num_slots << ")" << std::endl;
            }
            disassemble(program);
            return 0;
        }

        auto t5 = Clock::now();
        ether::vm::VM vm(program);
        ether::vm::Value result = vm.run(show_stats);
        auto t6 = Clock::now();
        auto end_total = Clock::now();

        std::cout << "VM Execution Result: " << result << std::endl;

        if (show_stats) {
            auto total_ms =
                std::chrono::duration_cast<std::chrono::microseconds>(end_total - start_total).count() / 1000.0;
            auto lex_ms = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;
            auto parse_ms = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0;
            auto ir_ms = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count() / 1000.0;
            auto vm_ms = std::chrono::duration_cast<std::chrono::microseconds>(t6 - t5).count() / 1000.0;

            print_stats(vm, total_ms, lex_ms, parse_ms, ir_ms, vm_ms);
        }
    } catch (const ether::CompilerError &e) {
        report_error(filename, source, e);
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
