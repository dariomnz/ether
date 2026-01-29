#pragma once
#include <string>

namespace ether {

struct TestOptions {
    int parallel_jobs = 1;
    bool quiet = false;
};

int run_tests(const std::string& ether_bin, const std::string& test_path, const TestOptions& options = {});
}  // namespace ether
