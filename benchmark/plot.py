#!/usr/bin/env python3
"""
designer_git benchmark plot script
Reads CSV results and generates graphs.

Usage:
    python3 plot.py --results results/ --output graphs/
"""

import os
import csv
import argparse
from pathlib import Path

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
except ImportError:
    print("matplotlib not found. Install: pip install matplotlib")
    exit(1)

# ── 설정 ─────────────────────────────────────────────────────
COLORS = {
    "designer_git": "#2563eb",
    "git_lfs":      "#dc2626",
    "fixed_chunk":  "#f59e0b",
    "rolling_hash": "#2563eb",
}

plt.rcParams.update({
    "font.size": 11,
    "axes.titlesize": 13,
    "axes.labelsize": 11,
    "figure.dpi": 150,
})


def load_csv(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    return rows


def avg(values):
    vals = [float(v) for v in values if v != ""]
    return sum(vals) / len(vals) if vals else 0


def save(fig, output_dir, filename):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / filename
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    print(f"  saved: {path}")


# ── 그래프 1: 시나리오 2 누적 저장소 크기 ─────────────────────

def plot_scenario2_storage(results_dir, output_dir):
    """Git LFS vs designer-git 누적 저장소 크기 비교"""
    path = Path(results_dir) / "scenario_2.csv"
    if not path.exists():
        print(f"[SKIP] {path} not found")
        return

    rows = load_csv(path)

    # commit_no별 vcs_total_kb 평균
    commits = sorted(set(int(r["commit_no"]) for r in rows))

    # Git LFS: 1GB 파일 × commit 횟수 (전체 재저장)
    lfs_sizes = [c * 1024 for c in commits]  # MB
    lfs_sizes_gb = [s / 1024 for s in lfs_sizes]  # GB
    dgit_sizes_gb = [avg([r["vcs_total_kb"]
                         for r in rows if int(r["commit_no"]) == c]) / 1024 / 1024
                     for c in commits]

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(commits, lfs_sizes_gb,
            marker="o", color=COLORS["git_lfs"],
            linewidth=2, label="Git LFS")
    ax.plot(commits, dgit_sizes_gb,
            marker="s", color=COLORS["designer_git"],
            linewidth=2, label="designer-git")

    ax.set_xlabel("Commit count")
    ax.set_yscale('log')
    ax.set_ylabel("Cumulative storage (GB, Log Scale)")
    ax.set_title("Git LFS vs designer-git\nCumulative storage (1GB exr, 1% modify per commit)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_xticks(commits)

    save(fig, output_dir, "scenario2_storage.png")


# ── 그래프 2: 시나리오 1 Rolling Hash 당위성 ─────────────────

def plot_scenario1_rolling_hash(results_dir, output_dir):
    """Rolling Hash vs 고정 청크 delta 크기 비교"""
    path = Path(results_dir) / "scenario_1.csv"
    if not path.exists():
        print(f"[SKIP] {path} not found")
        return

    rows = load_csv(path)

    # Rolling Hash: 실측 delta 크기
    rh_delta = avg([r["delta_kb"] for r in rows])

    # 고정 청크 추정: Insert 1KB → 절반 이후 전부 INSERT
    # 100MB 파일, 16KB 청크 기준 → 절반인 50MB = 3200 청크 INSERT
    estimated_fixed = 50 * 1024  # 약 50MB in KB

    fig, ax = plt.subplots(figsize=(6, 5))
    bars = ax.bar(
        ["Fixed Chunk\n(estimated)", "Rolling Hash\n(actual)"],
        [estimated_fixed, rh_delta],
        color=[COLORS["fixed_chunk"], COLORS["rolling_hash"]],
        width=0.5
    )

    for bar, val in zip(bars, [estimated_fixed, rh_delta]):
        ax.text(bar.get_x() + bar.get_width() / 2,
                bar.get_height() + 200,
                f"{val/1024:.1f} MB",
                ha="center", fontsize=11)

    ax.set_ylabel("Delta size (KB)")
    ax.set_title("Rolling Hash vs Fixed Chunk\nDelta size after 1KB Insert (100MB abc)")
    ax.grid(True, alpha=0.3, axis="y")

    save(fig, output_dir, "scenario1_rolling_hash.png")


# ── 그래프 3: 시나리오 3 텍스처 In-place 효율 ────────────────

def plot_scenario3_texture(results_dir, output_dir):
    """exr vs tiff In-place 수정 절감률"""
    path = Path(results_dir) / "scenario_3.csv"
    if not path.exists():
        print(f"[SKIP] {path} not found")
        return

    rows = load_csv(path)
    file_types = sorted(set(r["file_type"] for r in rows))
    sizes = sorted(set(int(r["size_mb"]) for r in rows))

    fig, ax = plt.subplots(figsize=(8, 5))
    x = list(range(len(sizes)))
    width = 0.35

    for i, ft in enumerate(file_types):
        savings = []
        for sz in sizes:
            vals = [r["savings_pct"]
                    for r in rows
                    if r["file_type"] == ft and int(r["size_mb"]) == sz]
            savings.append(avg(vals))
        offset = (i - len(file_types) / 2 + 0.5) * width
        ax.bar([xi + offset for xi in x], savings,
               width=width, label=f".{ft}")

    ax.set_xlabel("File size")
    ax.set_ylabel("Storage savings (%)")
    ax.set_title("Texture In-place Modify\nStorage savings by file type and size")
    ax.set_xticks(x)
    ax.set_xticklabels([f"{s}MB" if s < 1024 else f"{s//1024}GB" for s in sizes])
    ax.set_ylim(0, 105)
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")

    save(fig, output_dir, "scenario3_texture.png")


# ── 그래프 4: 시나리오 7 압축 포맷 fallback ──────────────────

def plot_scenario7_fallback(results_dir, output_dir):
    """jpg vs png fullcopy 분기 확인"""
    path = Path(results_dir) / "scenario_7.csv"
    if not path.exists():
        print(f"[SKIP] {path} not found")
        return

    rows = load_csv(path)
    file_types = sorted(set(r["file_type"] for r in rows))

    vcs_sizes = []
    for ft in file_types:
        vals = [float(r["vcs_kb"]) for r in rows if r["file_type"] == ft]
        vcs_sizes.append(avg(vals) / 1024)  # KB → MB

    fig, ax = plt.subplots(figsize=(6, 5))
    bars = ax.bar(file_types, vcs_sizes,
                  color=[COLORS["git_lfs"]] * len(file_types),
                  width=0.4)

    for bar, val in zip(bars, vcs_sizes):
        ax.text(bar.get_x() + bar.get_width() / 2,
                bar.get_height() + 0.5,
                f"{val:.1f} MB\n(fullcopy)",
                ha="center", fontsize=10)

    ax.set_ylabel("Storage size after 2nd commit (MB)")
    ax.set_title("Compressed Format Fallback\njpg / png → fullcopy (no delta)")
    ax.grid(True, alpha=0.3, axis="y")

    save(fig, output_dir, "scenario7_fallback.png")


# ── 그래프 5: 시나리오 8 이론치 ──────────────────────────────

def plot_scenario8_theory(results_dir, output_dir):
    """1바이트 수정 delta 크기"""
    path = Path(results_dir) / "scenario_8.csv"
    if not path.exists():
        print(f"[SKIP] {path} not found")
        return

    rows = load_csv(path)
    sizes = sorted(set(int(r["size_mb"]) for r in rows))

    delta_kbs = []
    savings = []
    for sz in sizes:
        vals_d = [float(r["delta_kb"]) for r in rows if int(r["size_mb"]) == sz]
        vals_s = [float(r["savings_pct"]) for r in rows if int(r["size_mb"]) == sz]
        delta_kbs.append(avg(vals_d))
        savings.append(avg(vals_s))

    fig, ax1 = plt.subplots(figsize=(7, 5))
    ax2 = ax1.twinx()

    x = list(range(len(sizes)))
    ax1.bar(x, delta_kbs, color=COLORS["designer_git"],
            alpha=0.7, width=0.4, label="Delta size (KB)")
    ax2.plot(x, savings, marker="o", color=COLORS["git_lfs"],
             linewidth=2, label="Savings (%)")

    ax1.set_xlabel("File size")
    ax1.set_ylabel("Delta size (KB)", color=COLORS["designer_git"])
    ax2.set_ylabel("Storage savings (%)", color=COLORS["git_lfs"])
    ax1.set_xticks(x)
    ax1.set_xticklabels([f"{s}MB" if s < 1024 else f"{s//1024}GB" for s in sizes])
    ax1.set_title("1-byte Modify: Delta size & Savings\n(Theory max efficiency)")

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="center right")
    ax1.grid(True, alpha=0.3, axis="y")

    save(fig, output_dir, "scenario8_theory.png")


# ── 그래프 6: 파이프라인 단계별 적용 가능성 표 ───────────────

def plot_pipeline_table(output_dir):
    """파이프라인 단계별 적용 가능성 요약 표"""
    stages = [
        ("Initial Modeling",   "Sculpting, polygon edit",        "❌ Low",  "#fee2e2"),
        ("Texturing",          "Pixel color/material modify",     "✅ High", "#dcfce7"),
        ("Rigging",            "Bone structure addition",         "✅ High", "#dcfce7"),
        ("Animation",          "Keyframe value modify",           "✅ High", "#dcfce7"),
        ("Scene Assembly",     "Object placement/transform",      "✅ High", "#dcfce7"),
        ("VFX Cache",          "Alembic frame insertion",         "✅ High", "#dcfce7"),
    ]

    fig, ax = plt.subplots(figsize=(10, 4))
    ax.axis("off")

    headers = ["Pipeline Stage", "Work Pattern", "Delta Efficiency"]
    data = [[s[0], s[1], s[2]] for s in stages]
    colors = [["#f1f5f9", "#f1f5f9", "#f1f5f9"]] + \
             [[s[3], s[3], s[3]] for s in stages]

    table = ax.table(
        cellText=[headers] + data,
        cellLoc="center",
        loc="center",
        cellColours=colors
    )
    table.auto_set_font_size(False)
    table.set_fontsize(11)
    table.scale(1, 2)

    # 헤더 스타일
    for j in range(3):
        table[0, j].set_facecolor("#1e293b")
        table[0, j].set_text_props(color="white", fontweight="bold")

    ax.set_title("Pipeline Stage vs Delta Efficiency",
                 fontsize=13, pad=20)

    save(fig, output_dir, "pipeline_table.png")


# ── main ──────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="designer_git benchmark plots")
    parser.add_argument("--results", default="results",
                        help="Directory with CSV result files")
    parser.add_argument("--output", default="graphs",
                        help="Directory to save graph images")
    args = parser.parse_args()

    print(f"results: {args.results}")
    print(f"output:  {args.output}")

    plot_scenario1_rolling_hash(args.results, args.output)
    plot_scenario2_storage(args.results, args.output)
    plot_scenario3_texture(args.results, args.output)
    plot_scenario7_fallback(args.results, args.output)
    plot_scenario8_theory(args.results, args.output)
    plot_pipeline_table(args.output)

    print("\nAll plots done.")


if __name__ == "__main__":
    main()


# python3 benchmark/plot.py --results benchmark/results --output benchmark/graphs

# 생성되는 그래프
# scenario1_rolling_hash.png  ← Rolling Hash vs 고정 청크
# scenario2_storage.png       ← Git LFS vs designer-git 누적 크기
# scenario3_texture.png       ← 텍스처 In-place 절감률
# scenario7_fallback.png      ← 압축 포맷 fullcopy 확인
# scenario8_theory.png        ← 1바이트 수정 이론치
# pipeline_table.png          ← 파이프라인 단계별 적용 가능성 표
