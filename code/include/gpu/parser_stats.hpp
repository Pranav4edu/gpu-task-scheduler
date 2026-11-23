#pragma once
#include "gpu/contracts.hpp"
#include "gpu/ds/id_hash_table.hpp"
namespace gpu {
// Published only after successful parsing. Original parse_csv(path) stays available.
struct ParserStats { HashStats task_ids, dependency_pairs; };
ParsedInput parse_csv(const std::string& path, ParserStats* stats);
}
