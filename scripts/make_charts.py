#!/usr/bin/env python3
"""Charts and summary tables of the evaluation (E1-E5) from the CSV files in benchmarks/results/.

    python3 scripts/make_charts.py

Writes:
    docs/images/evaluation/*.svg     one chart per question, for the documentation and the thesis
    benchmarks/results/summary.md    mean ± standard deviation over the runs of every case, and the
                                     ratios the evaluation discusses

A missing CSV skips its charts. Only the Python standard library is used: the SVG is written by
hand, so the charts can be regenerated anywhere.

Chart design: at most three series per chart in the fixed categorical order blue, orange, aqua
(validated for colour-vision deficiencies, all pairs, light and dark); every series also has its
own marker shape and a direct label, so identity never depends on colour alone (and survives a
greyscale print); one y-axis; thin 2 px lines; recessive grid. The SVG follows the viewer's
light/dark preference with its own dark steps.
"""

import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "benchmarks" / "results"
IMAGES = ROOT / "docs" / "images" / "evaluation"

# Light / dark steps of the three categorical slots, and the surface and ink tokens.
STYLE = """
.surface { fill: #fcfcfb; } .grid { stroke: #e4e3df; } .axis { stroke: #8a8984; }
.ink { fill: #0b0b0b; } .ink2 { fill: #52514e; } .ring { stroke: #fcfcfb; }
.l1 { stroke: #2a78d6; } .l2 { stroke: #eb6834; } .l3 { stroke: #1baf7a; }
.f1 { fill: #2a78d6; } .f2 { fill: #eb6834; } .f3 { fill: #1baf7a; }
.l1, .l2, .l3 { fill: none; }
@media (prefers-color-scheme: dark) {
  .surface { fill: #1a1a19; } .grid { stroke: #33332f; } .axis { stroke: #6f6e69; }
  .ink { fill: #f0efec; } .ink2 { fill: #c3c2b7; } .ring { stroke: #1a1a19; }
  .l1 { stroke: #3987e5; } .l2 { stroke: #d95926; } .l3 { stroke: #199e70; }
  .f1 { fill: #3987e5; } .f2 { fill: #d95926; } .f3 { fill: #199e70; }
}
text { font-family: system-ui, -apple-system, "Segoe UI", sans-serif; }
"""

WIDTH, HEIGHT = 680, 400
LEFT, RIGHT, TOP, BOTTOM = 70, 120, 64, 52  # plot margins; labels go into the right margin


# ---------------------------------------------------------------------------------------------
# Reading the results


def read(name):
    path = RESULTS / name
    if not path.exists():
        print(f"skipped: {path} not found")
        return None
    with path.open() as f:
        return list(csv.DictReader(f))


def summarize(rows, keys, value):
    """{key tuple: (mean, stddev, n)} of column `value` over rows grouped by `keys`."""
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row[k] for k in keys)].append(float(row[value]))
    return {k: (statistics.fmean(v), statistics.stdev(v) if len(v) > 1 else 0.0, len(v))
            for k, v in groups.items()}


def series(stats, name_index, x_index, names):
    """[(name, [(x, mean, sd)])] in the order of `names`, x ascending."""
    out = []
    for name in names:
        points = sorted((float(k[x_index]), m, sd) for k, (m, sd, _) in stats.items()
                        if k[name_index] == name)
        if points:
            out.append((name, points))
    return out


# ---------------------------------------------------------------------------------------------
# SVG


class Scale:
    def __init__(self, low, high, log, start, end):
        self.log = log
        if log:
            low, high = math.log10(low), math.log10(high)
        self.low, self.high, self.start, self.end = low, high, start, end

    def __call__(self, v):
        v = math.log10(v) if self.log else v
        return self.start + (v - self.low) / (self.high - self.low) * (self.end - self.start)


def log_range(values):
    low = 10 ** math.floor(math.log10(min(values)))
    high = 10 ** math.ceil(math.log10(max(values)))
    return low, high if high > low else low * 10


def linear_range(values):
    high = max(values)
    step = nice_step(high / 5)
    return 0.0, math.ceil(high / step) * step


def nice_step(raw):
    magnitude = 10 ** math.floor(math.log10(raw))
    for m in (1, 2, 2.5, 5, 10):
        if raw <= m * magnitude:
            return m * magnitude
    return 10 * magnitude


def label(v):
    if v >= 1000:
        return f"{v / 1000:g}k" if v < 1e6 else f"{v / 1e6:g}M"
    return f"{v:g}"


def ticks(low, high, log):
    if log:
        out, decade = [], low
        while decade <= high * 1.0001:
            out.append(decade)
            decade *= 10
        return out
    step = nice_step((high - low) / 5)
    return [low + i * step for i in range(int(round((high - low) / step)) + 1)]


def marker(shape, x, y, slot):
    ring = f'class="f{slot} ring" stroke-width="2"'
    if shape == 0:
        return f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" {ring}/>'
    if shape == 1:
        return f'<rect x="{x - 4.5:.1f}" y="{y - 4.5:.1f}" width="9" height="9" rx="1.5" {ring}/>'
    return (f'<path d="M{x:.1f} {y - 5.5:.1f} L{x + 5.2:.1f} {y + 3.5:.1f} '
            f'L{x - 5.2:.1f} {y + 3.5:.1f} Z" {ring}/>')


def svg_document(title, subtitle, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {HEIGHT}" '
            f'width="{WIDTH}" height="{HEIGHT}" role="img" aria-label="{escape(title)}">\n'
            f'<title>{escape(title)}</title>\n<style>{STYLE}</style>\n'
            f'<rect class="surface" width="{WIDTH}" height="{HEIGHT}" rx="8"/>\n'
            f'<text class="ink" x="20" y="28" font-size="16" font-weight="600">{escape(title)}</text>\n'
            f'<text class="ink2" x="20" y="47" font-size="12">{escape(subtitle)}</text>\n'
            f'{body}</svg>\n')


def line_chart(path, title, subtitle, x_label, y_label, data, x_log, y_log):
    xs = [p[0] for _, pts in data for p in pts]
    ys = [p[1] + p[2] for _, pts in data for p in pts] + [max(p[1] - p[2], 1e-9)
                                                          for _, pts in data for p in pts]
    x_low, x_high = (min(xs), max(xs)) if x_log else (0.0, max(xs))
    y_low, y_high = log_range([y for y in ys if y > 0]) if y_log else linear_range(ys)
    x = Scale(x_low, x_high, x_log, LEFT + 12, WIDTH - RIGHT)
    y = Scale(y_low, y_high, y_log, HEIGHT - BOTTOM, TOP + 8)
    parts = []
    for t in ticks(y_low, y_high, y_log):  # horizontal grid with y labels
        parts.append(f'<line class="grid" x1="{LEFT}" x2="{WIDTH - RIGHT}" y1="{y(t):.1f}" '
                     f'y2="{y(t):.1f}" stroke-width="1"/>')
        parts.append(f'<text class="ink2" x="{LEFT - 8}" y="{y(t) + 4:.1f}" font-size="11" '
                     f'text-anchor="end">{label(t)}</text>')
    for t in sorted(set(xs)):  # x labels at the measured values
        parts.append(f'<text class="ink2" x="{x(t):.1f}" y="{HEIGHT - BOTTOM + 18}" '
                     f'font-size="11" text-anchor="middle">{label(t)}</text>')
    parts.append(f'<line class="axis" x1="{LEFT}" x2="{WIDTH - RIGHT}" y1="{HEIGHT - BOTTOM}" '
                 f'y2="{HEIGHT - BOTTOM}" stroke-width="1"/>')
    parts.append(f'<text class="ink2" x="{(LEFT + WIDTH - RIGHT) / 2}" y="{HEIGHT - 12}" '
                 f'font-size="12" text-anchor="middle">{escape(x_label)}</text>')
    parts.append(f'<text class="ink2" transform="translate(18 {(TOP + HEIGHT - BOTTOM) / 2}) '
                 f'rotate(-90)" font-size="12" text-anchor="middle">{escape(y_label)}</text>')

    end_labels = []
    for i, (name, points) in enumerate(data):
        cls = f"l{i + 1}"
        coords = " ".join(f"{x(px):.1f},{y(py):.1f}" for px, py, _ in points)
        parts.append(f'<polyline class="{cls}" fill="none" stroke-width="2" '
                     f'stroke-linejoin="round" points="{coords}"/>')
        for px, py, sd in points:  # error bars: ± one standard deviation
            low, high = max(py - sd, y_low), py + sd
            if y(low) - y(high) > 3:
                parts.append(f'<line class="{cls}" x1="{x(px):.1f}" x2="{x(px):.1f}" '
                             f'y1="{y(low):.1f}" y2="{y(high):.1f}" stroke-width="1.5"/>')
        for px, py, _ in points:
            parts.append(marker(i, x(px), y(py), i + 1))
        end_labels.append([y(points[-1][1]), name, x(points[-1][0]), i])
    # Direct labels at the line ends, pushed apart when they would overlap.
    end_labels.sort()
    for a, b in zip(end_labels, end_labels[1:]):
        if b[0] - a[0] < 15:
            b[0] = a[0] + 15
    for ly, name, lx, i in end_labels:
        parts.append(f'<text class="ink" x="{lx + 10:.1f}" y="{ly + 4:.1f}" font-size="12">'
                     f'{escape(name)}</text>')
    # Legend row (identity also by marker shape).
    lx = 20
    for i, (name, _) in enumerate(data):
        parts.append(f'<line class="l{i + 1}" x1="{lx}" x2="{lx + 22}" y1="{TOP - 2}" '
                     f'y2="{TOP - 2}" stroke-width="2"/>')
        parts.append(marker(i, lx + 11, TOP - 2, i + 1))
        parts.append(f'<text class="ink" x="{lx + 28}" y="{TOP + 2}" font-size="12">'
                     f'{escape(name)}</text>')
        lx += 40 + 7 * len(name)
    path.write_text(svg_document(title, subtitle, "\n".join(parts) + "\n"))
    print(f"wrote {path.relative_to(ROOT)}")


def bar_chart(path, title, subtitle, bars):
    """Horizontal bars of one series (no legend: the title names it), values labelled."""
    top, row = TOP + 4, 22
    height = top + row * len(bars) + 24
    left, right = 190, 80
    high = max(v for _, v in bars)
    scale = Scale(0, high, False, left, WIDTH - right)
    parts = []
    for i, (name, value) in enumerate(bars):
        y0 = top + i * row
        width = max(scale(value) - left, 2)
        parts.append(f'<text class="ink2" x="{left - 8}" y="{y0 + 14}" font-size="12" '
                     f'text-anchor="end">{escape(name)}</text>')
        parts.append(f'<path class="f1" d="M{left} {y0 + 3} h{width - 4:.1f} a4 4 0 0 1 4 4 '
                     f'v8 a4 4 0 0 1 -4 4 h{-(width - 4):.1f} Z"/>')
        parts.append(f'<text class="ink" x="{left + width + 6:.1f}" y="{y0 + 14}" '
                     f'font-size="12">{value:,}</text>')
    parts.append(f'<line class="axis" x1="{left}" x2="{left}" y1="{top}" '
                 f'y2="{top + row * len(bars)}" stroke-width="1"/>')
    doc = svg_document(title, subtitle, "\n".join(parts) + "\n")
    doc = doc.replace(f'viewBox="0 0 {WIDTH} {HEIGHT}" width="{WIDTH}" height="{HEIGHT}"',
                      f'viewBox="0 0 {WIDTH} {height}" width="{WIDTH}" height="{height}"')
    doc = doc.replace(f'height="{HEIGHT}" rx="8"', f'height="{height}" rx="8"')
    path.write_text(doc)
    print(f"wrote {path.relative_to(ROOT)}")


# ---------------------------------------------------------------------------------------------
# The experiments


def pm(mean, sd, digits=3):
    return f"{mean:.{digits}f} ± {sd:.{digits}f}"


def e1(md):
    rows = read("e1_renderer.csv")
    if not rows:
        return
    render = summarize(rows, ("mode", "sprites"), "render_mean_ms")
    frame = summarize(rows, ("mode", "sprites"), "frame_mean_ms")
    calls = summarize(rows, ("mode", "sprites"), "draw_calls")
    line_chart(IMAGES / "e1_renderer.svg", "E1: batched vs. naive rendering",
               "Render time per frame (mean ± sd over runs), 1280×720 off-screen target",
               "sprites", "render time per frame (ms, log)",
               series(render, 0, 1, ["batched", "naive"]), True, True)
    md.append("## E1: batched vs. naive rendering\n")
    md.append("| sprites | batched render ms | naive render ms | naive / batched | draw calls "
              "(batched / naive) | batched frame ms |")
    md.append("|---:|---:|---:|---:|---:|---:|")
    for sprites in sorted({int(k[1]) for k in render}):
        if ("batched", str(sprites)) not in render or ("naive", str(sprites)) not in render:
            continue  # a run in progress
        b = render[("batched", str(sprites))]
        n = render[("naive", str(sprites))]
        md.append(f"| {sprites:,} | {pm(b[0], b[1])} | {pm(n[0], n[1])} | {n[0] / b[0]:.1f}× "
                  f"| {calls[('batched', str(sprites))][0]:.0f} / "
                  f"{calls[('naive', str(sprites))][0]:.0f} "
                  f"| {pm(*frame[('batched', str(sprites))][:2])} |")
    md.append("")
    if "cpu_mean_ms" in rows[0]:
        e1_split(md, rows, render)


def e1_split(md, rows, render):
    """Where the render time goes: CPU submission vs. GPU execution (newer CSVs only)."""
    cpu = summarize(rows, ("mode", "sprites"), "cpu_mean_ms")
    gpu = summarize(rows, ("mode", "sprites"), "gpu_mean_ms")
    batched = [(name, series(stats, 0, 1, ["batched"])[0][1])
               for name, stats in (("render", render), ("CPU", cpu), ("GPU", gpu))]
    line_chart(IMAGES / "e1_split.svg", "E1: where the batched render time goes",
               "render ≈ CPU (build + submit vertices) + GPU (draw, timer query); mean ± sd",
               "sprites", "time per frame (ms, log)", batched, True, True)
    md.append("### E1: CPU and GPU share of the render time\n")
    md.append("| sprites | mode | render ms | CPU ms (build + submit) | GPU ms (draw) |")
    md.append("|---:|---|---:|---:|---:|")
    for sprites in sorted({int(k[1]) for k in render}):
        for mode in ("batched", "naive"):
            key = (mode, str(sprites))
            if key in render and key in cpu and key in gpu:
                md.append(f"| {sprites:,} | {mode} | {pm(*render[key][:2])} | {pm(*cpu[key][:2])} "
                          f"| {pm(*gpu[key][:2])} |")
    md.append("")


def e2(md):
    rows = read("e2_ecs.csv")
    if not rows:
        return
    per = summarize(rows, ("approach", "entities"), "ns_per_entity")
    total = summarize(rows, ("approach", "entities"), "mean_ms")
    line_chart(IMAGES / "e2_ecs.svg", "E2: sparse-set ECS vs. OOP game objects",
               "Time per object and update (mean ± sd over runs); lower is faster",
               "objects", "ns per object",
               series(per, 0, 1, ["ecs", "oop", "oop_shuffled"]), True, False)
    md.append("## E2: ECS vs. OOP game objects\n")
    md.append("| objects | ecs ns/object | oop ns/object | oop_shuffled ns/object | oop / ecs | "
              "shuffled / ecs | ecs ms per update |")
    md.append("|---:|---:|---:|---:|---:|---:|---:|")
    for n in sorted({int(k[1]) for k in per}):
        e, o, s = (per[(a, str(n))] for a in ("ecs", "oop", "oop_shuffled"))
        md.append(f"| {n:,} | {pm(e[0], e[1], 2)} | {pm(o[0], o[1], 2)} | {pm(s[0], s[1], 2)} "
                  f"| {o[0] / e[0]:.1f}× | {s[0] / e[0]:.1f}× "
                  f"| {pm(*total[('ecs', str(n))][:2])} |")
    md.append("")


def e3(md):
    rows = read("e3_broadphase.csv")
    if not rows:
        return
    names = ["brute", "sap", "grid"]
    step = summarize(rows, ("method", "bodies"), "step_ms")
    broad = summarize(rows, ("method", "bodies"), "broadphase_ms")
    tests = summarize(rows, ("method", "bodies"), "pair_tests")
    pairs = summarize(rows, ("method", "bodies"), "candidate_pairs")
    line_chart(IMAGES / "e3_step.svg", "E3: physics step time by broad phase",
               "Whole step, bodies moving like a gas (mean ± sd over runs)",
               "bodies", "step time (ms, log)", series(step, 0, 1, names), True, True)
    line_chart(IMAGES / "e3_pair_tests.svg", "E3: bounding box tests per step",
               "The work the broad phase does; all three find the same pairs",
               "bodies", "box tests per step (log)", series(tests, 0, 1, names), True, True)
    md.append("## E3: broad phase cost\n")
    md.append("| bodies | method | step ms | broad phase ms | box tests | pairs found |")
    md.append("|---:|---|---:|---:|---:|---:|")
    for n in sorted({int(k[1]) for k in step}):
        for m in names:
            key = (m, str(n))
            md.append(f"| {n:,} | {m} | {pm(*step[key][:2])} | {pm(*broad[key][:2])} "
                      f"| {tests[key][0]:,.0f} | {pairs[key][0]:,.0f} |")
    md.append("")
    md.append("| bodies | brute / sap step | brute / grid step |")
    md.append("|---:|---:|---:|")
    for n in sorted({int(k[1]) for k in step}):
        b = step[("brute", str(n))][0]
        md.append(f"| {n:,} | {b / step[('sap', str(n))][0]:.1f}× | "
                  f"{b / step[('grid', str(n))][0]:.1f}× |")
    md.append("")


def e4(md):
    stack = read("e4_stack.csv")
    bounce = read("e4_bounce.csv")
    if stack:
        md.append("## E4: stability\n")
        md.append("| " + " | ".join(stack[0]) + " |")
        md.append("|" + "---:|" * len(stack[0]))
        for row in stack:
            md.append("| " + " | ".join(row.values()) + " |")
        md.append("")
    if bounce:
        md.append("| " + " | ".join(bounce[0]) + " |")
        md.append("|" + "---:|" * len(bounce[0]))
        for row in bounce:
            md.append("| " + " | ".join(row.values()) + " |")
        md.append("")


def e5(md):
    loc = read("e5_loc.csv")
    api = read("e5_api.csv")
    if not loc:
        return
    rows = sorted(loc, key=lambda r: -int(r["code_lines"]))
    bar_chart(IMAGES / "e5_loc.svg", "E5: lines of code",
              "Code lines without blank and comment-only lines; the platformer is the game",
              [(r["part"].replace("examples/", ""), int(r["code_lines"])) for r in rows])
    engine = next(int(r["code_lines"]) for r in loc if r["part"] == "engine")
    md.append("## E5: usability\n")
    md.append("| part | files | code lines | comment lines | share of the engine |")
    md.append("|---|---:|---:|---:|---:|")
    for r in rows:
        md.append(f"| {r['part']} | {r['files']} | {int(r['code_lines']):,} | "
                  f"{int(r['comment_lines']):,} | {100 * int(r['code_lines']) / engine:.0f} % |")
    md.append("")
    if api:
        by_module = defaultdict(list)
        for r in api:
            by_module[r["module"]].append(r["name"])
        md.append("| module | engine names the platformer uses |")
        md.append("|---|---|")
        for module in sorted(by_module):
            md.append(f"| {module} | {', '.join(sorted(by_module[module]))} |")
        md.append("")


def main():
    IMAGES.mkdir(parents=True, exist_ok=True)
    md = ["# Evaluation results", "",
          "Generated by `scripts/make_charts.py` from the CSV files in this directory: mean ± "
          "sample standard deviation over the runs of each case. Machine and settings are in the "
          "`*.machine.txt` files.", ""]
    for experiment in (e1, e2, e3, e4, e5):
        experiment(md)
    (RESULTS / "summary.md").write_text("\n".join(md))
    print(f"wrote {(RESULTS / 'summary.md').relative_to(ROOT)}")


if __name__ == "__main__":
    main()
