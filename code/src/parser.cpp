#include "gpu/contracts.hpp"
#include "gpu/ds/id_hash_table.hpp"
#include "gpu/parser_stats.hpp"

#include <fstream>
#include <sstream>

// CSV workload parser (Divyaa's module).
//
// Format: task_id,release_time,duration,dependencies
//   * exactly four comma-separated fields, no quoting
//   * dependencies are predecessor IDs separated by ';' (may be empty)
//   * IDs use letters, digits and underscores
//   * LF or CRLF line endings; whitespace around fields is trimmed
//
// Two passes so a task may depend on a task defined on a later row:
//   pass 1: parse every row, register ID -> dense index (row order) in IdHashTable
//   pass 2: resolve dependency names to indices and emit edges predecessor -> task
//
// Every error names the file, the 1-based line number (header is row 1) and field.
// Cycle detection is NOT done here; that belongs to validate_workload.
namespace gpu {
namespace {
struct PendingRow {
    std::size_t line;
    std::vector<std::string> dependencies;
};

[[noreturn]] void fail(const std::string& path, std::size_t line, const char* field,
                       const std::string& message) {
    std::ostringstream out;
    out << path << ": row " << line;
    if (field != nullptr && field[0] != '\0') out << ", field '" << field << "'";
    out << ": " << message;
    throw std::runtime_error(out.str());
}

std::string trim(const std::string& text) {
    const char* space = " \t\r\n";
    const std::size_t first = text.find_first_not_of(space);
    if (first == std::string::npos) return std::string();
    const std::size_t last = text.find_last_not_of(space);
    return text.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t end = text.find(separator, begin);
        if (end == std::string::npos) { parts.push_back(text.substr(begin)); break; }
        parts.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return parts;
}

bool valid_id(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// Parses an optionally negative decimal integer. Rejects empty text, stray
// characters and values outside Tick's range ("overflow").
Tick parse_integer(const std::string& path, std::size_t line, const char* field,
                   const std::string& text) {
    std::size_t pos = 0;
    bool negative = false;
    if (pos < text.size() && text[pos] == '-') { negative = true; ++pos; }
    if (pos >= text.size()) fail(path, line, field, "expected an integer, found '" + text + "'");
    const Tick max = std::numeric_limits<Tick>::max();
    Tick magnitude = 0;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (c < '0' || c > '9') fail(path, line, field, "malformed integer '" + text + "'");
        const Tick digit = c - '0';
        if (magnitude > (max - digit) / 10) fail(path, line, field, "integer overflow in '" + text + "'");
        magnitude = magnitude * 10 + digit;
    }
    return negative ? -magnitude : magnitude;
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(path + ": cannot open file");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (in.bad()) throw std::runtime_error(path + ": read error");
    // Ignore blank lines at the end of the file (editors often add them).
    while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
    return lines;
}
} // namespace

ParsedInput parse_csv(const std::string& path) {
    return parse_csv(path, nullptr);
}

ParsedInput parse_csv(const std::string& path, ParserStats* stats) {
    if (stats) *stats = {};
    std::vector<std::string> lines = read_lines(path);
    if (lines.empty()) throw std::runtime_error(path + ": row 1: missing header row");
    // Spreadsheet exports sometimes start with a UTF-8 byte order mark.
    if (lines[0].compare(0, 3, "\xEF\xBB\xBF") == 0) lines[0].erase(0, 3);

    const std::vector<std::string> expected = {"task_id", "release_time", "duration", "dependencies"};
    std::vector<std::string> header = split(lines[0], ',');
    for (auto& name : header) name = trim(name);
    if (header != expected)
        fail(path, 1, "", "header must be exactly task_id,release_time,duration,dependencies");

    ParsedInput parsed;
    IdHashTable index;
    std::vector<PendingRow> pending;
    index.reserve(lines.size());

    // Pass 1: parse rows and register IDs.
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::size_t line = i + 1;
        std::vector<std::string> fields = split(lines[i], ',');
        if (fields.size() != 4)
            fail(path, line, "", "expected exactly 4 comma-separated fields, found " +
                                     std::to_string(fields.size()));
        for (auto& field : fields) field = trim(field);

        TaskSpec spec;
        spec.external_id = fields[0];
        if (!valid_id(spec.external_id))
            fail(path, line, "task_id", "ID must be non-empty letters, digits or underscores, found '" +
                                            spec.external_id + "'");
        spec.release = parse_integer(path, line, "release_time", fields[1]);
        if (spec.release < 0) fail(path, line, "release_time", "must be >= 0, found " + fields[1]);
        spec.duration = parse_integer(path, line, "duration", fields[2]);
        if (spec.duration <= 0) fail(path, line, "duration", "must be > 0, found " + fields[2]);
        try {
            checked_add(spec.release, spec.duration);
        } catch (const std::overflow_error&) {
            fail(path, line, "duration", "release_time + duration overflows");
        }

        if (!index.insert(spec.external_id, parsed.tasks.size()))
            fail(path, line, "task_id", "duplicate task ID '" + spec.external_id + "'");
        parsed.tasks.push_back(spec);

        PendingRow row{line, {}};
        if (!fields[3].empty()) {
            for (auto& dep : split(fields[3], ';')) {
                dep = trim(dep);
                if (!valid_id(dep))
                    fail(path, line, "dependencies", "invalid predecessor ID '" + dep + "'");
                row.dependencies.push_back(dep);
            }
        }
        pending.push_back(std::move(row));
    }

    // Pass 2: resolve predecessors (may be defined on later rows).
    IdHashTable seen_edges; // key "pred>succ", rejects duplicate dependencies
    for (std::size_t task = 0; task < pending.size(); ++task) {
        for (const std::string& dep : pending[task].dependencies) {
            const std::optional<TaskId> from = index.find(dep);
            if (!from) fail(path, pending[task].line, "dependencies", "unknown task ID '" + dep + "'");
            if (*from == task)
                fail(path, pending[task].line, "dependencies", "task '" + dep + "' depends on itself");
            if (!seen_edges.insert(dep + ">" + parsed.tasks[task].external_id, 0))
                fail(path, pending[task].line, "dependencies", "duplicate dependency '" + dep + "'");
            parsed.edges.push_back(EdgePair{*from, task});
        }
    }
    if (stats) *stats = {index.stats(), seen_edges.stats()};
    return parsed;
}
ValidatedWorkload load_and_validate(const std::string& path) {
    return validate_workload(parse_csv(path));
}
} // namespace gpu
