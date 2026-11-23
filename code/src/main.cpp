#include "gpu/experiments.hpp"
#include "gpu/self_test.hpp"
#include "gpu/workloads.hpp"
#include <exception>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") return gpu::run_self_tests(std::cout);
        if (argc == 2 && std::string(argv[1]) == "--write-experiment-inputs") {
            gpu::write_experiment_inputs(); return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--write-skew-workloads") {
            gpu::write_owner_skew_fixtures("data/workloads/"); return 0;
        }
        if (argc != 1) throw std::invalid_argument("Use root ./run.sh without arguments.");
        gpu::run_experiments();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n'; return 1;
    }
}
