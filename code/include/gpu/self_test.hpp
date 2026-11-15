#pragma once
#include <functional>
#include <iosfwd>
#include <utility>
#include <vector>
namespace gpu {
int run_self_tests(std::ostream& output);
// Module test lists registered with the shared runner (one main only).
std::vector<std::pair<const char*, std::function<void()>>> ring_deque_tests();  // Aayush
std::vector<std::pair<const char*, std::function<void()>>> scheduler_tests();   // Aayush
std::vector<std::pair<const char*, std::function<void()>>> policy_tests();      // Aayush
std::vector<std::pair<const char*, std::function<void()>>> static_scheduler_tests(); // Aayush
std::vector<std::pair<const char*, std::function<void()>>> export_tests();           // Aayush
std::vector<std::pair<const char*, std::function<void()>>> workload_tests();         // Aayush
std::vector<std::pair<const char*, std::function<void()>>> stealing_tests();         // Aayush
std::vector<std::pair<const char*, std::function<void()>>> ds_metrics_tests();       // Aayush
}
