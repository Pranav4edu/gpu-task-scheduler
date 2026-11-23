#!/usr/bin/env python3
"""Optional development/CI audit; run.sh itself needs only C++17 and a POSIX shell."""
import csv
import math
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
results = root / 'code' / 'results'

def read(name):
    with (results / name).open(newline='') as f:
        return list(csv.DictReader(f))

def key(row):
    return row['workload'], int(row['compute_units']), row['config']

rows = read('metrics.csv')
assert len(rows) == 756 and len({key(r) for r in rows}) == 756
assert (results / 'metrics.csv').read_bytes() == (results / 'results.csv').read_bytes()
manifest = {key(r): r for r in read('experiment_manifest.csv')}
assert len(manifest) == 756 and set(manifest) == {key(r) for r in rows}
busy = {}
for r in read('per_unit.csv'):
    busy.setdefault(key(r), {})[int(r['unit'])] = int(r['busy_ticks'])
workloads = {}
for r in manifest.values():
    path = r['input_file']
    if path not in workloads:
        with (root / 'code' / path).open(newline='') as f:
            tasks = list(csv.DictReader(f))
        workloads[path] = tasks
    assert len(workloads[path]) == int(r['tasks'])
assert len(workloads) == 48
for r in rows:
    k = key(r)
    m = manifest[k]
    tasks = workloads[m['input_file']]
    total = sum(int(t['duration']) for t in tasks)
    p = k[1]
    duration = int(r['makespan'])
    assert set(busy[k]) == set(range(p))
    values = list(busy[k].values())
    assert sum(values) == total and all(0 <= x <= duration for x in values)
    assert math.isclose(float(r['utilization']), total / (p * duration), abs_tol=5.01e-7)
    mean = total / p
    cv = math.sqrt(sum((x - mean)**2 for x in values) / p) / mean
    assert math.isclose(float(r['busy_time_cv']), cv, abs_tol=5.01e-7)
    assert float(r['mean_ready_wait']) >= 0 and float(r['p95_ready_wait']) >= 0
    if m['family'] == 'chain':
        assert duration == 3 * len(tasks)
    if m['family'] == 'fork_join' and m['config'].startswith('central_'):
        assert duration == 6 + 3 * math.ceil((len(tasks) - 2) / p)
    if m['family'] == 'owner_skew' and m['config'] == 'static_fifo':
        assert duration == 10 * math.ceil(len(tasks) / p)
    if p == 1 and all(int(t['release_time']) == 0 for t in tasks):
        assert duration == total
    ET.parse(results / f"makespan_{k[0]}_P{p}.svg")
for r in read('ds_metrics.csv'):
    if r['instance'] == 'event_queue':
        n = int(manifest[key(r)]['tasks'])
        assert int(r['pushes']) == int(r['pops']) == 2 * n
for r in read('engine_metrics.csv'):
    m = manifest[key(r)]
    assert int(r['successor_visits']) == int(m['edges'])
    assert int(r['ready_tasks']) == int(m['tasks'])
for r in read('preprocessing.csv'):
    if r['instance'] == 'task_ids':
        m = next(m for m in manifest.values() if m['workload'] == r['workload'])
        assert int(r['inserts']) == int(m['tasks']) and int(r['finds']) == int(m['edges'])
traces = read('trace.csv')
assert len(traces) == 30
for config in {r['config'] for r in traces}:
    records = {r['external_id']: r for r in traces if r['config'] == config}
    assert len(records) == 6
    for task, preds in {'D': ['A', 'B'], 'E': ['C'], 'F': ['D', 'E']}.items():
        assert int(records[task]['ready']) == max(int(records[p]['finish']) for p in preds)
    assert max(int(r['finish']) for r in records.values()) == (11 if config == 'central_cp' else 13)
for path in results.glob('*.svg'):
    ET.parse(path)
print('PASS independent output audit: 756 runs, 48 inputs, metric formulas, bounds, counters, traces and SVG XML')
