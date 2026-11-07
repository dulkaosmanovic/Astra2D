#!/usr/bin/env python3
"""Evaluation E5: "Is the engine usable?"

Measures how much code a game needs on top of the engine, and which parts of the engine it uses:

    python3 scripts/bench_usability.py

Writes to benchmarks/results/:
    e5_loc.csv  lines of code per part: the engine, the tests, every example (the platformer is the
                showcase game). Code lines are non-blank lines that are not only a comment; comment
                lines are counted separately (the code is commented heavily on purpose).
    e5_api.csv  every engine type or function the platformer names (astra2d::Name), with the module
                and header that declare it.

Only the Python standard library is used.
"""

import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "benchmarks" / "results"
SOURCES = ("*.cpp", "*.hpp")


def files(*directories):
    out = []
    for directory in directories:
        for pattern in SOURCES:
            out += sorted((ROOT / directory).rglob(pattern))
    return out


def count(paths):
    """(code lines, comment lines) of C++ files; block comments are followed across lines."""
    code = comments = 0
    for path in paths:
        in_block = False
        for raw in path.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if not line:
                continue
            if in_block:
                comments += 1
                in_block = "*/" not in line
                continue
            if line.startswith("//"):
                comments += 1
            elif line.startswith("/*"):
                comments += 1
                in_block = "*/" not in line
            else:
                code += 1
    return code, comments


def loc_rows():
    parts = [("engine", "engine library (include/ + src/)", files("include", "src")),
             ("tests", "unit tests", files("tests"))]
    for example in sorted(p for p in (ROOT / "examples").iterdir()
                          if p.is_dir() and p.name not in ("assets", "common")):
        kind = "game" if example.name == "platformer" else "example"
        parts.append((f"examples/{example.name}", kind, files(f"examples/{example.name}")))
    rows = []
    for name, kind, paths in parts:
        code, comments = count(paths)
        rows.append({"part": name, "kind": kind, "files": len(paths), "code_lines": code,
                     "comment_lines": comments})
    return rows


# A declaration at namespace level (the code base does not indent namespace contents): a type that
# is defined (not just forward-declared), an alias, or a free function. Nested types (indented) and
# forward declarations (`class Renderer2D;`) do not count.
DECLARATION = (r"^(?:class|struct|enum\s+class|enum)\s+(?:\[\[nodiscard\]\]\s+)?{name}\b(?!\s*;)"
               r"|^using\s+{name}\s*="
               r"|^(?:\[\[nodiscard\]\]\s+)?\w[\w:<>&*, ]*[\s&*]{name}\s*\(")


def declaring_header(name, headers):
    pattern = re.compile(DECLARATION.format(name=re.escape(name)), re.MULTILINE)
    for header in headers:
        if pattern.search(header.read_text(encoding="utf-8")):
            return header
    return None


def api_rows():
    game = "\n".join(p.read_text(encoding="utf-8") for p in files("examples/platformer"))
    names = sorted(set(re.findall(r"astra2d::([A-Za-z_]\w*)", game)))
    headers = files("include")
    rows = []
    for name in names:
        header = declaring_header(name, headers)
        relative = header.relative_to(ROOT / "include") if header else None
        rows.append({"module": relative.parts[1] if relative else "?", "name": name,
                     "header": str(relative) if relative else "?"})
    rows.sort(key=lambda r: (r["module"], r["name"]))
    return rows


def write(path, rows):
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    RESULTS.mkdir(parents=True, exist_ok=True)
    loc = loc_rows()
    api = api_rows()
    write(RESULTS / "e5_loc.csv", loc)
    write(RESULTS / "e5_api.csv", api)
    engine = next(r for r in loc if r["part"] == "engine")
    game = next(r for r in loc if r["kind"] == "game")
    modules = sorted({r["module"] for r in api})
    print(f"engine {engine['code_lines']} code lines, platformer {game['code_lines']} "
          f"({100 * game['code_lines'] / engine['code_lines']:.0f} % of the engine)")
    print(f"platformer uses {len(api)} engine names from {len(modules)} modules: "
          f"{', '.join(modules)}")
    unknown = [r["name"] for r in api if r["module"] == "?"]
    if unknown:
        print("not found in include/:", ", ".join(unknown))
    print(f"Results: {RESULTS / 'e5_loc.csv'} {RESULTS / 'e5_api.csv'}")


if __name__ == "__main__":
    main()
