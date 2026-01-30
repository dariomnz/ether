#include "driver/driver_utils.hpp"

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace ether::driver {

void report_error(const std::string &main_filename, const std::string &main_source, const ether::CompilerError &e) {
    std::string filename = e.filename();
    if (filename.empty()) filename = main_filename;

    // ANSI Colors - only if stderr is a terminal
    bool use_color = isatty(STDERR_FILENO);
    const char *RED = use_color ? "\033[1;31m" : "";
    const char *BOLD = use_color ? "\033[1m" : "";
    const char *RESET = use_color ? "\033[0m" : "";

    std::cerr << BOLD << filename << ":" << e.line() << ":" << e.col() << ": " << RED << "error: " << RESET << BOLD
              << e.what() << RESET << std::endl;

    std::string source;
    if (filename == main_filename) {
        source = main_source;
    } else {
        std::ifstream file(filename);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            source = buffer.str();
        }
    }

    if (!source.empty()) {
        std::stringstream ss(source);
        std::string line;
        for (int i = 1; i <= e.line(); ++i) {
            if (!std::getline(ss, line)) break;
        }

        // Highlight the erroneous part in the line itself
        int col = e.col() - 1;  // 0-indexed
        int len = e.length();

        std::cerr << "  ";
        if (col >= 0 && col < (int)line.size()) {
            std::cerr << line.substr(0, col);
            std::cerr << RED << line.substr(col, len) << RESET;
            if (col + len < (int)line.size()) {
                std::cerr << line.substr(col + len);
            }
        } else {
            std::cerr << line;
        }
        std::cerr << std::endl;

        std::cerr << "  ";
        for (int i = 0; i < col; ++i) {
            std::cerr << " ";
        }
        std::cerr << RED << "^";
        for (int i = 1; i < len; ++i) {
            std::cerr << "~";
        }
        std::cerr << RESET << std::endl;
    }
}

void print_stats(const ether::vm::VM &vm, double total_ms, double lex_ms, double parse_ms, double sema_ms, double ir_ms,
                 double vm_ms) {
    std::cout << "\nPhase Timings:" << std::endl;
    std::cout << std::left << std::setw(15) << "Phase" << "Time (ms)" << std::endl;
    std::cout << std::string(30, '-') << std::endl;
    std::cout << std::left << std::setw(15) << "Tokenizing" << std::fixed << std::setprecision(3) << lex_ms << " ms"
              << std::endl;
    std::cout << std::left << std::setw(15) << "Parsing" << parse_ms << " ms" << std::endl;
    std::cout << std::left << std::setw(15) << "Sema" << sema_ms << " ms" << std::endl;
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

void print_usage() {
    std::cerr << "Usage: ether <command> [args]\n\n"
              << "Commands:\n"
              << "  ether <filename> [flags]    Compile and run a source file\n"
              << "      --dump-ir               Dump the generated bytecode\n"
              << "      --stats                 Show execution statistics\n\n"
              << "  ether --test <path> [flags] Run tests\n"
              << "      -j <N>                  Number of parallel jobs\n"
              << "      -q, --quiet             Suppress output\n\n"
              << "  ether --lsp                 Start the Language Server\n\n"
              << "  ether -h, --help            Show this help message" << std::endl;
}

}  // namespace ether::driver
