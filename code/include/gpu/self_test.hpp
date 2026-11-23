#pragma once
#include <iosfwd>
#include <string>
namespace gpu {
int run_self_tests(std::ostream& output);
int run_engine_tests(std::ostream& output);
// Generates a clearly labeled trace using the same test-only fixtures.
void write_engine_checkpoint(const std::string& directory);
}
