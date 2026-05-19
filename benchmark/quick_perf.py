#!/usr/bin/env python3
"""
designer_git quick performance test
1GB 파일 1회 측정 — 빠른 성능 확인용

Usage:
    sudo python3 quick_perf.py --dgit ./build/dgit
"""

import os
import time
import shutil
import argparse
import subprocess
from pathlib import Path

REPO_DIR = Path("_quick_bench")


def drop_os_caches():
    subprocess.run("sync && echo 3 > /proc/sys/vm/drop_caches",
                   shell=True, check=False)


def run(cmd, cwd=None):
    drop_os_caches()
    start = time.perf_counter()
    result = subprocess.run(cmd, shell=True, cwd=cwd,
                            capture_output=True, text=True)
    elapsed = time.perf_counter() - start
    return result.returncode, result.stdout, result.stderr, elapsed


def make_exr(path, size_mb):
    header = bytes([0x76, 0x2F, 0x31, 0x01])
    with open(path, "wb") as f:
        f.write(header)
        remaining = size_mb * 1024 * 1024 - len(header)
        for _ in range(remaining // (1024 * 1024)):
            f.write(os.urandom(1024 * 1024))
        f.write(os.urandom(remaining % (1024 * 1024)))


def setup_repo(dgit, repo_path):
    if repo_path.exists():
        shutil.rmtree(repo_path)
    repo_path.mkdir(parents=True)
    subprocess.run(f"{dgit} init", shell=True, cwd=repo_path,
                   capture_output=True)


def main():
    parser = argparse.ArgumentParser(description="Quick perf test")
    parser.add_argument("--dgit", required=True)
    parser.add_argument("--size", type=int, default=1024,
                        help="File size in MB (default: 1024 = 1GB)")
    args = parser.parse_args()

    dgit = str(Path(args.dgit).resolve())
    size_mb = args.size

    print(f"dgit: {dgit}")
    print(f"file size: {size_mb}MB")
    print(f"{'='*50}")

    repo = REPO_DIR
    setup_repo(dgit, repo)
    src = repo / "texture.exr"

    print(f"1. Generating {size_mb}MB exr file ... ", end="", flush=True)
    t0 = time.perf_counter()
    make_exr(src, size_mb)
    print(f"{time.perf_counter()-t0:.2f}s")

    print(f"2. Initial commit (base) ... ", end="", flush=True)
    subprocess.run(f"{dgit} add texture.exr", shell=True, cwd=repo,
                   capture_output=True)
    _, _, _, t = run(f"{dgit} commit -m init texture.exr", cwd=repo)
    print(f"{t:.3f}s")

    print(f"3. Modifying 1 byte at middle ... ", end="", flush=True)
    size = os.path.getsize(src)
    with open(src, "r+b") as f:
        f.seek(size // 2)
        f.write(b"\x42")
    print("done")

    print(f"4. Commit (delta) ... ", end="", flush=True)
    _, _, _, commit_t = run(f"{dgit} commit -m modify texture.exr", cwd=repo)
    print(f"{commit_t:.3f}s")

    # delta 크기
    deltas = repo / ".vcs" / "objects" / "deltas"
    delta_kb = sum(f.stat().st_size for f in deltas.rglob("*") if f.is_file()) / 1024
    savings = (1 - delta_kb / (size_mb * 1024)) * 100
    print(f"   delta size: {delta_kb:.1f}KB  savings: {savings:.2f}%")

    print(f"5. Checkout ... ", end="", flush=True)
    head = (repo / ".vcs" / "HEAD").read_text().strip()
    _, _, _, checkout_t = run(f"{dgit} checkout {head}", cwd=repo)
    print(f"{checkout_t:.3f}s")

    print(f"{'='*50}")
    print(f"Results ({size_mb}MB file):")
    print(f"  commit time:   {commit_t:.3f}s")
    print(f"  checkout time: {checkout_t:.3f}s")
    print(f"  delta size:    {delta_kb:.1f}KB")
    print(f"  savings:       {savings:.2f}%")

    shutil.rmtree(repo)


if __name__ == "__main__":
    main()