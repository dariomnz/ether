#include "test_runner.hpp"

#include <sys/wait.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace ether {

struct TestCase {
    fs::path path;
    std::optional<int64_t> expected_result;
    std::vector<std::string> expected_outputs;
    std::vector<std::string> not_expected_outputs;
    std::string args;
};

struct ExecResult {
    std::string output;
    int status;
};

struct TestResult {
    bool success;
    std::string test_name;
    double elapsed;
    std::vector<std::string> errors;
    std::string system_error;
    std::string output;
};

ExecResult exec(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int status = pclose(pipe);
    return {result, status};
}

TestResult perform_test(const std::string& ether_bin, const TestCase& tc) {
    auto start = std::chrono::high_resolution_clock::now();
    try {
        if (tc.expected_outputs.empty() && tc.not_expected_outputs.empty() && !tc.expected_result.has_value()) {
            return {false, tc.path.string(), 0, {}, "NOTHING TO TEST", ""};
        }
        // Use 'timeout 1s' to prevent hanging
        std::string cmd = "timeout 1s " + ether_bin + " " + tc.path.string() + " " + tc.args + " 2>&1";
        ExecResult res = exec(cmd);
        std::string output = res.output;

        // Check for timeout (exit code 124 for 'timeout' command)
        int exit_code = WEXITSTATUS(res.status);
        if (exit_code == 124) {
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            return {false, tc.path.string(), elapsed.count(), {}, "TIMEOUT", output};
        }

        std::vector<std::string> errors;

        if (tc.expected_result.has_value()) {
            std::string marker = "VM Execution Result: ";
            size_t pos = output.find(marker);
            if (pos != std::string::npos) {
                size_t start_pos = pos + marker.length();
                size_t end_pos = output.find_first_not_of("-0123456789", start_pos);
                int64_t actual_result = std::stoll(output.substr(start_pos, end_pos - start_pos));
                if (actual_result != *tc.expected_result) {
                    errors.push_back("Expected result " + std::to_string(*tc.expected_result) + ", got " +
                                     std::to_string(actual_result));
                }
            } else {
                errors.push_back("Could not find VM Execution Result in output");
            }
        }

        size_t current_search_pos = 0;
        for (const auto& expected_out : tc.expected_outputs) {
            size_t pos = output.find(expected_out, current_search_pos);
            if (pos == std::string::npos) {
                if (output.find(expected_out) != std::string::npos) {
                    errors.push_back("Expected output substring '" + expected_out + "' found but out of order");
                } else {
                    errors.push_back("Expected output substring '" + expected_out + "' not found");
                }
            } else {
                current_search_pos = pos + expected_out.length();
            }
        }

        for (const auto& not_expected_out : tc.not_expected_outputs) {
            if (output.find(not_expected_out) != std::string::npos) {
                errors.push_back("Not expected output substring '" + not_expected_out + "' found");
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        return {errors.empty(), tc.path.string(), elapsed.count(), errors, "", output};
    } catch (const std::exception& e) {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        return {false, tc.path.string(), elapsed.count(), {}, e.what(), ""};
    }
}

int run_tests(const std::string& ether_bin, const std::string& test_path, const TestOptions& options) {
    auto start = std::chrono::high_resolution_clock::now();
    std::string ether_bin_abs = fs::absolute(ether_bin).string();
    fs::path target = test_path;

    std::vector<TestCase> tests;
    auto process_file = [&](const fs::path& p) {
        if (p.extension() == ".eth") {
            TestCase tc;
            tc.path = p;
            std::ifstream file(p);
            std::string line;
            std::string res_marker = "// EXPECTED_RESULT:";
            std::string out_marker = "// EXPECTED_OUTPUT:";
            std::string args_marker = "// ARGS:";
            std::string nout_marker = "// NOT_EXPECTED_OUTPUT:";

            while (std::getline(file, line)) {
                if (size_t pos = line.find(res_marker); pos != std::string::npos) {
                    std::string val = line.substr(pos + res_marker.length());
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);
                    if (!val.empty()) tc.expected_result = std::stoll(val);
                } else if (size_t pos = line.find(out_marker); pos != std::string::npos) {
                    std::string val = line.substr(pos + out_marker.length());
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);
                    if (!val.empty()) tc.expected_outputs.push_back(val);
                } else if (size_t pos = line.find(nout_marker); pos != std::string::npos) {
                    std::string val = line.substr(pos + nout_marker.length());
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);
                    if (!val.empty()) tc.not_expected_outputs.push_back(val);
                } else if (size_t pos = line.find(args_marker); pos != std::string::npos) {
                    std::string val = line.substr(pos + args_marker.length());
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);
                    if (!val.empty()) tc.args = val;
                }
            }
            tests.push_back(tc);
        }
    };

    if (fs::is_directory(target)) {
        for (const auto& entry : fs::recursive_directory_iterator(target)) {
            if (entry.is_regular_file()) {
                process_file(entry.path());
            }
        }
    } else {
        process_file(target);
    }

    std::mutex output_mutex;
    std::mutex queue_mutex;
    std::queue<size_t> test_queue;
    for (size_t i = 0; i < tests.size(); ++i) test_queue.push(i);

    std::atomic<int> passed(0);
    int num_workers = options.parallel_jobs;
    if (num_workers <= 0) num_workers = std::thread::hardware_concurrency();
    if (num_workers <= 0) num_workers = 1;

    auto worker_task = [&] {
        while (true) {
            size_t test_idx;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (test_queue.empty()) return;
                test_idx = test_queue.front();
                test_queue.pop();
            }

            auto result = perform_test(ether_bin_abs, tests[test_idx]);

            {
                std::lock_guard<std::mutex> lock(output_mutex);
                if (result.success) {
                    passed++;
                    if (!options.quiet) {
                        std::cout << "Running test: " << result.test_name << "... " << "\033[32mPASSED\033[0m in "
                                  << std::fixed << std::setprecision(3) << result.elapsed << " seconds" << std::endl;
                    }
                } else {
                    std::cout << "Running test: " << result.test_name << "... " << "\033[31mFAILED\033[0m in "
                              << std::fixed << std::setprecision(3) << result.elapsed << " seconds" << std::endl;
                    if (!result.system_error.empty()) {
                        std::cout << "  - \033[31mERROR:\033[0m " << result.system_error << std::endl;
                    }
                    for (const auto& err : result.errors) {
                        std::cout << "  - " << err << std::endl;
                    }
                    if (!result.output.empty()) {
                        std::cout << "  --- PROGRAM OUTPUT ---" << std::endl;
                        std::string line;
                        std::stringstream ss(result.output);
                        while (std::getline(ss, line)) {
                            std::cout << "  | " << line << std::endl;
                        }
                        std::cout << "  ----------------------" << std::endl;
                    }
                }
            }
        }
    };
    if (num_workers == 1) {
        worker_task();
    } else {
        std::vector<std::thread> workers;
        for (int i = 0; i < num_workers; ++i) {
            workers.emplace_back(worker_task);
        }
        for (auto& w : workers) w.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "\nSummary: " << passed << "/" << tests.size() << " tests passed in " << std::fixed
              << std::setprecision(3) << elapsed.count() << " seconds" << std::endl;
    return (passed == (int)tests.size()) ? 0 : 1;
}

}  // namespace ether
