#pragma once
#include <functional>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>
namespace gpu {
int run_self_tests(std::ostream& output);
int run_engine_tests(std::ostream& output);
// Generates a clearly labeled trace using the same test-only fixtures.
void write_engine_checkpoint(const std::string& directory);
// Module test lists registered with the shared runner (one main only).
std::vector<std::pair<const char*, std::function<void()>>> ring_deque_tests();  // Aayush
std::vector<std::pair<const char*, std::function<void()>>> scheduler_tests();   // Aayush
std::vector<std::pair<const char*, std::function<void()>>> policy_tests();      // Aayush
std::vector<std::pair<const char*, std::function<void()>>> static_scheduler_tests(); // Aayush
std::vector<std::pair<const char*, std::function<void()>>> export_tests();           // Aayush
std::vector<std::pair<const char*, std::function<void()>>> workload_tests();         // Aayush
std::vector<std::pair<const char*, std::function<void()>>> stealing_tests();         // Aayush
std::vector<std::pair<const char*, std::function<void()>>> ds_metrics_tests();       // Aayush
std::vector<std::pair<const char*, std::function<void()>>> scheduler_engine_tests(); // Aayush: production schedulers in the real engine
}
