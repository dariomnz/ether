#pragma once

#include <string>

#include "common/error.hpp"
#include "vm/vm.hpp"

namespace ether::driver {

void report_error(const std::string &main_filename, const std::string &main_source, const ether::CompilerError &e);
void print_stats(const ether::vm::VM &vm, double total_ms, double lex_ms, double parse_ms, double sema_ms, double ir_ms,
                 double vm_ms);
void print_usage();

}  // namespace ether::driver
