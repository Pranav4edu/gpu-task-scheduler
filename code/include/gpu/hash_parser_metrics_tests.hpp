#pragma once
#include <functional>
#include <utility>
#include <vector>

namespace gpu {
using NamedTest = std::pair<const char*, std::function<void()>>;
// Tests for IdHashTable, parse_csv and compute_metrics (Divyaa's modules).
// Registered by run_self_tests in tests/self_test.cpp. Paths are relative to code/.
std::vector<NamedTest> hash_parser_metrics_tests();
} // namespace gpu
