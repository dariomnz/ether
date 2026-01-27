#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

struct TestCase {
    fs::path path;
    std::optional<int> expected_result;
    std::vector<std::string> expected_outputs;
};

std::string exec(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool run_test(const std::string& ether_bin, const TestCase& tc) {
    std::cout << "Running test: " << tc.path.relative_path() << "... " << std::flush;
    auto start = std::chrono::high_resolution_clock::now();
    try {
        std::string cmd = ether_bin + " " + tc.path.string() + " 2>&1";
        std::string output = exec(cmd);

        std::vector<std::string> errors;

        if (tc.expected_result.has_value()) {
            std::string marker = "VM Execution Result: ";
            size_t pos = output.find(marker);
            if (pos != std::string::npos) {
                size_t start = pos + marker.length();
                size_t end = output.find_first_not_of("-0123456789", start);
                int actual_result = std::stoi(output.substr(start, end - start));
                if (actual_result != *tc.expected_result) {
                    errors.push_back("Expected result " + std::to_string(*tc.expected_result) + ", got " +
                                     std::to_string(actual_result));
                }
            } else {
                errors.push_back("Could not find VM Execution Result in output");
            }
        }

        for (const auto& expected_out : tc.expected_outputs) {
            if (output.find(expected_out) == std::string::npos) {
                errors.push_back("Expected output substring '" + expected_out + "' not found");
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        if (errors.empty()) {
            std::cout << "\033[32mPASSED\033[0m in " << std::fixed << std::setprecision(3) << elapsed.count()
                      << " seconds" << std::endl;
            return true;
        } else {
            std::cout << "\033[31mFAILED\033[0m in " << std::fixed << std::setprecision(3) << elapsed.count()
                      << " seconds" << std::endl;
            for (const auto& err : errors) {
                std::cout << "  - " << err << std::endl;
            }
            return false;
        }
    } catch (const std::exception& e) {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        std::cout << "\033[31mERROR\033[0m in " << std::fixed << std::setprecision(3) << elapsed.count() << " seconds"
                  << std::endl;
        std::cout << "  - " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <ether_bin> <test_dir_or_file>" << std::endl;
        return 1;
    }
    auto start = std::chrono::high_resolution_clock::now();
    std::string ether_bin = fs::absolute(argv[1]).string();
    fs::path target = argv[2];

    std::vector<TestCase> tests;
    auto process_file = [&](const fs::path& p) {
        if (p.extension() == ".eth") {
            TestCase tc;
            tc.path = p;
            std::ifstream file(p);
            std::string line;
            std::string res_marker = "// EXPECTED_RESULT:";
            std::string out_marker = "// EXPECTED_OUTPUT:";

            while (std::getline(file, line)) {
                if (size_t pos = line.find(res_marker); pos != std::string::npos) {
                    std::string val = line.substr(pos + res_marker.length());
                    // Trim whitespace
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);
                    if (!val.empty()) tc.expected_result = std::stoi(val);
                } else if (size_t pos = line.find(out_marker); pos != std::string::npos) {
                    std::string val = line.substr(pos + out_marker.length());
                    // Trim whitespace
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);
                    if (!val.empty()) tc.expected_outputs.push_back(val);
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

    int passed = 0;
    for (const auto& tc : tests) {
        if (run_test(ether_bin, tc)) {
            passed++;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "\nSummary: " << passed << "/" << tests.size() << " tests passed in " << std::fixed
              << std::setprecision(3) << elapsed.count() << " seconds" << std::endl;
    return (passed == tests.size()) ? 0 : 1;
}
