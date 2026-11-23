#pragma once
#include "gpu/contracts.hpp"
#include <string>
#include <vector>
namespace gpu {
struct ExperimentInput {
    std::string name, path, family;
    std::size_t tasks = 0;
    std::uint32_t seed = 0;
    std::vector<std::size_t> units;
};
std::vector<ExperimentInput> default_experiment_inputs();
ParsedInput generate_experiment_input(const ExperimentInput& input);
std::string workload_csv(const ParsedInput& input);
void write_experiment_inputs(); // developer regeneration; not required by grading
void run_experiments();         // cwd = code; reads saved CSVs, verifies every result
}
