# Joining the project

1. Accept the collaborator invitation with your own GitHub account.
2. Clone the repository and enter it:

   ```sh
   git clone https://github.com/Pranav4edu/gpu-task-scheduler.git
   cd gpu-task-scheduler
   ./run.sh
   ```

3. Configure your own commit name and a verified GitHub email or your GitHub
   noreply address. Never use another member's identity.
4. Read CONTRACTS.md, pick your row in TASKS.md and branch from current main.
   If this foundation is still under review, check out its PR branch first.
5. Implement your data structure in its assigned header/source files, add tests,
   and run `./run.sh` before pushing.
6. Open a pull request with tests and a small example. Another member reviews it.
   Resolve interface changes together before merging.

Suggested branch names: `feature/ring-deque`, `feature/directed-graph`,
`feature/id-hash-table`, `feature/event-engine`. Keep unrelated changes separate.

## What you should see today

The foundation tests pass and a heap-ordering demonstration is written. It is
normal that no full simulation charts exist yet: the engine and three structures
are pending. The declaration-only headers deliberately provide no STL fallback.

## First test cases by owner

- Pranav: heap mixed-operation tests, event ordering, duration overflow; next,
  batched arrivals/completions and exactly-once readiness.
- Aayush: wraparound, resize while wrapped, both-end operations, drain/refill;
  compare to std::deque only in tests.
- Shaswitha: edge counts, indegrees, invalid endpoints, duplicate edges, cycles,
  six-task ranks and unchanged indegrees across runs.
- Divyaa: forced collisions, duplicate IDs, missing lookups, repeated rehash and
  forward references; compare mappings to a test-only reference.

Check in source and small fixtures, not build products. Update the contribution
record with actual PRs/tests/reviews. Generated documentation or AI-assisted
starter code does not by itself establish independent implementation authorship.
