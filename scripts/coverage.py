#!/usr/bin/env python3
"""Test coverage of the engine (include/ + src/) by the unit tests, per module.

    python3 scripts/coverage.py

Configures build-cov (-Db_coverage=true, no sanitizers) if it does not exist, builds it, deletes
old counters, runs the unit tests once and reads the counters with GCC's gcov (--json-format). Only
gcov is needed, no gcovr or lcov. Writes benchmarks/results/coverage.csv and prints the table.

A source line, branch or function counts as covered when any test executed it. Every compiled engine
file is counted, also the ones no test links (they count as not covered). Lines in headers are
merged over all translation units that include them.
"""

import csv
import json
import subprocess
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build-cov"
OUT = ROOT / "benchmarks" / "results" / "coverage.csv"


def run_tests():
    if not (BUILD / "build.ninja").exists():
        subprocess.run(["meson", "setup", str(BUILD), "-Db_coverage=true", "-Db_sanitize=none"],
                       cwd=ROOT, check=True)
    subprocess.run(["ninja", "-C", str(BUILD)], check=True)
    for old in BUILD.rglob("*.gcda"):
        old.unlink()
    subprocess.run(["meson", "test", "-C", str(BUILD)], check=True)


def module_of(relative):
    """include/astra2d/<module>/x.hpp or src/<module>/x.cpp -> module."""
    return relative.parts[2] if relative.parts[0] == "include" else relative.parts[1]


def collect():
    lines = defaultdict(dict)     # file -> line -> executed
    branches = defaultdict(dict)  # file -> (line, index) -> taken
    functions = defaultdict(dict)  # file -> name -> executed
    notes = [*(BUILD / "src").rglob("*.gcno"), *(BUILD / "tests").rglob("*.gcno")]
    # A note without its object is left over from a file that was moved or renamed (ninja does not
    # delete it); it describes old code and would add lines that can never be covered.
    notes = [note for note in notes if note.with_suffix(".o").exists()]
    for note in notes:
        # Without a .gcda (the object was never linked into a test), gcov reports zero counts.
        result = subprocess.run(["gcov", "--json-format", "--stdout", "--branch-probabilities",
                                 note.name], cwd=note.parent, capture_output=True, text=True,
                                check=True)
        for document in filter(str.strip, result.stdout.splitlines()):
            data = json.loads(document)
            base = Path(data["current_working_directory"])
            for entry in data["files"]:
                path = (base / entry["file"]).resolve()
                if not path.is_relative_to(ROOT):
                    continue
                relative = path.relative_to(ROOT)
                if relative.parts[0] not in ("src", "include"):
                    continue
                for line in entry["lines"]:
                    number = line["line_number"]
                    lines[relative][number] = lines[relative].get(number, False) or line["count"] > 0
                    for index, branch in enumerate(line.get("branches", [])):
                        if branch.get("throw"):
                            continue  # exception edges of every call: not decisions in the code
                        key = (number, index)
                        branches[relative][key] = (branches[relative].get(key, False)
                                                   or branch["count"] > 0)
                for function in entry["functions"]:
                    name = function["demangled_name"]
                    functions[relative][name] = (functions[relative].get(name, False)
                                                 or function["execution_count"] > 0)
    return lines, branches, functions


def percent(covered, total):
    return round(100.0 * covered / total, 1) if total else 100.0


def main():
    run_tests()
    lines, branches, functions = collect()
    modules = defaultdict(lambda: defaultdict(int))
    for relative in lines:
        m = modules[module_of(relative)]
        for kind, table in (("lines", lines), ("branches", branches), ("functions", functions)):
            m[f"{kind}_covered"] += sum(table[relative].values())
            m[kind] += len(table[relative])
    total = defaultdict(int)
    for m in modules.values():
        for key, value in m.items():
            total[key] += value
    rows = []
    for name, m in [*sorted(modules.items()), ("total", total)]:
        row = {"module": name}
        for kind in ("lines", "branches", "functions"):
            row[f"{kind}_covered"] = m[f"{kind}_covered"]
            row[kind] = m[kind]
            row[f"{kind}_pct"] = percent(m[f"{kind}_covered"], m[kind])
        rows.append(row)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"{'module':<10}" + "".join(f"  {k:<18}" for k in ("lines", "branches", "functions")))
    for row in rows:
        print(f"{row['module']:<10}"
              + "".join(f"  {row[k + '_covered']:>5}/{row[k]:<5} {row[k + '_pct']:5.1f}%"
                        for k in ("lines", "branches", "functions")))
    print(f"Results: {OUT}")


if __name__ == "__main__":
    main()
