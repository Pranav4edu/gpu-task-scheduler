# Presentation and defense notes (Aayush)

Study notes for explaining the ring deque and the scheduler. Written as an aid; the
numbers come from the unit tests and can be re-run with `sh run.sh`. Be ready to
explain every line you present in your own words, and to say plainly which parts were
AI-assisted.

## 1. The ring deque in one picture

Capacity 4. Logical element k lives at slot `(head + k) % capacity`.

```
push_back 1,2,3,4        slots: [1][2][3][4]   head=0 count=4 (full)
pop_front, pop_front     slots: [ ][ ][3][4]   head=2 count=2
push_back 5              slot (2+2)%4 = 0      slots: [5][ ][3][4]
push_back 6              slot (2+3)%4 = 1      slots: [5][6][3][4]   head=2 count=4 (full, WRAPPED)
                         logical order: 3,4,5,6
push_back 7  -> full: grow
   allocate 8 slots, copy in LOGICAL order, head=0:
                         slots: [3][4][5][6][ ][ ][ ][ ]  then 7 goes to slot (0+4)%8 = 4
```

`tests/test_ring_deque.cpp` "growth while wrapped" is this scenario.

## 2. Why each design choice (likely questions)

- **Why keep `count_` and not a tail index?** With only head and tail, "empty" and
  "full" look identical (head == tail). A count removes the ambiguity.
- **Why `(head + capacity - 1) % capacity` in `push_front`?** `head - 1` underflows for
  unsigned `head == 0`. Adding `capacity` first keeps it non-negative.
- **Why copy in logical order when growing?** A wrapped layout cannot be extended in
  place; copying in order puts head at 0 and the new space at the end.
- **Why copy the value before growing?** `push_back(d.front())` passes a reference into
  the array that growth is about to free (aliasing bug). Tested.
- **Why is push amortized O(1) although growing is O(Q)?** Doubling: total copies for n
  pushes are 8 + 16 + ... < 2n. Measured: 1,000 pushes -> 10 resizes (from capacity 1),
  1,023 copies. Worst case for one push is still O(Q); say both.
- **Why no shrinking?** Simpler; storage is O(peak size), not O(current size).
- **Why is `std::vector` allowed inside?** It is only the raw array. All index math,
  wraparound and growth are ours. `std::deque` appears only in tests as an independent
  reference.
- **Why a deque and not a queue?** FIFO needs `push_back` + `pop_front`. `pop_back` is
  what work stealing needs: a thief takes the newest task, the owner keeps the oldest.

## 3. How the deque is used in the running simulator

- Central FIFO: one `RingDeque<TaskId>` holds every eligible task; idle units pop the front.
- Static FIFO / stealing: one `RingDeque` per unit; owner = `TaskId % P`.
- SJF / CP reuse Pranav's `BinaryHeap` with comparators from `priority.hpp`. Do not
  claim heap authorship; claim the policy integration.
- Call sites: `src/policies.cpp` (`enqueue_ready` / `dispatch`), constructed by
  `make_scheduler`.

## 4. Numbers to quote (hand-derived, checked by tests; not engine results)

- Six-task example, P=2: FIFO/SJF makespan 13, CP 11, static FIFO 13 (F on unit 1).
- Skew example (durations 5,1,5,1; P=2): central 6, stealing 7, static 10.
- Owner-skew N=100: static makespan is `10 * ceil(N/P)` (250 at P=4); central stays
  within `max(ceil(W/P), 10) .. floor(W/P) + 10`.
- Deque counters, central FIFO (owner-skew, all tasks queued at t=0, start capacity 8):
  N=100 -> 100 pushes, 100 pops, peak 100, 4 resizes, 120 elements copied;
  N=1000 -> 1000 / 1000 / 1000, 7 resizes, 1016 copied.
- Static FIFO P=4: N=100 -> 25 pushes per unit, 2 resizes, 24 copied; N=1000 -> 250
  per unit, 5 resizes, 248 copied.

## 5. Scheduler concepts to be able to draw

- Static placement idles units: unit 0 owns all the heavy tasks, the others finish and
  wait, even while unit 0 has queued work. (Skew oracle: makespan 10 vs 6.)
- Stealing order: owners dispatch first (so a low-id thief cannot take a task its owner
  is about to run), then thieves probe `u+1, u+2, ...` cyclically, at most P-1 probes,
  steal from the back, never touch running tasks.
- Strict ascending `idle_units` is validated before any pop so a bad call cannot leave a
  queue half drained.

## 6. Honest limits to state

- All results so far are from unit tests and a test-only mini simulator, not from the
  project engine (not written yet).
- Fixtures were not yet loaded through the project parser.
- Parts of this code were drafted with AI assistance (see `CONTRIBUTIONS.md`).
