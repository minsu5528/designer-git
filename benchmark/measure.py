#!/usr/bin/env python3
"""
designer_git benchmark measurement script
Runs all scenarios and saves results to CSV.

Usage:
    python3 measure.py --dgit /path/to/dgit --output results/
"""

import os
import sys
import csv
import time
import shutil
import random
import argparse
import subprocess
from pathlib import Path

# ── 설정 ─────────────────────────────────────────────────────
REPEAT = 3          # 반복 횟수
RESULTS_DIR = Path("results")
REPO_DIR = Path("_bench_repo")  # 임시 저장소

# ── 유틸 ─────────────────────────────────────────────────────

def drop_os_caches():
    """OS 페이지 캐시 플러시 — 캐시 효과로 인한 측정값 왜곡 방지"""
    try:
        subprocess.run("sync && echo 3 > /proc/sys/vm/drop_caches",
                       shell=True, check=False)
    except Exception:
        pass  # 권한 없으면 무시 (sudo 없이 실행 시)


def run(cmd, cwd=None):
    """명령어 실행 전 OS 캐시 플러시 후 시간 측정"""
    drop_os_caches()
    start = time.perf_counter()
    result = subprocess.run(
        cmd, shell=True, cwd=cwd,
        capture_output=True, text=True
    )
    elapsed = time.perf_counter() - start
    return result.returncode, result.stdout, result.stderr, elapsed


def repo_size_kb(path):
    """디렉터리 전체 크기 KB 반환"""
    total = 0
    for f in Path(path).rglob("*"):
        if f.is_file():
            total += f.stat().st_size
    return total / 1024


def delta_size_kb(repo_path):
    """.vcs/objects/deltas/ 크기 KB 반환"""
    deltas = Path(repo_path) / ".vcs" / "objects" / "deltas"
    if not deltas.exists():
        return 0
    total = sum(f.stat().st_size for f in deltas.rglob("*") if f.is_file())
    return total / 1024


def vcs_size_kb(repo_path):
    """.vcs/ 전체 크기 KB 반환"""
    vcs = Path(repo_path) / ".vcs"
    if not vcs.exists():
        return 0
    total = sum(f.stat().st_size for f in vcs.rglob("*") if f.is_file())
    return total / 1024


def setup_repo(dgit, repo_path):
    """임시 저장소 초기화"""
    if repo_path.exists():
        shutil.rmtree(repo_path)
    repo_path.mkdir(parents=True)
    run(f"{dgit} init", cwd=repo_path)


def teardown_repo(repo_path):
    """임시 저장소 삭제"""
    if repo_path.exists():
        shutil.rmtree(repo_path)


def save_csv(filename, rows, fieldnames):
    """CSV 저장"""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    filepath = RESULTS_DIR / filename
    with open(filepath, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"  saved: {filepath}")


# ── 파일 생성 함수 ────────────────────────────────────────────

def make_random_file(path, size_mb):
    """난수 바이너리 파일 생성"""
    path = Path(path)
    with open(path, "wb") as f:
        for _ in range(size_mb):
            f.write(os.urandom(1024 * 1024))


def make_exr(path, size_mb):
    """exr magic 헤더 + 난수 데이터"""
    path = Path(path)
    header = bytes([0x76, 0x2F, 0x31, 0x01])
    with open(path, "wb") as f:
        f.write(header)
        remaining = size_mb * 1024 * 1024 - len(header)
        for _ in range(remaining // (1024 * 1024)):
            f.write(os.urandom(1024 * 1024))
        f.write(os.urandom(remaining % (1024 * 1024)))


def make_tiff(path, size_mb):
    """tiff magic 헤더 + 난수 데이터"""
    path = Path(path)
    header = bytes([0x49, 0x49, 0x2A, 0x00])
    with open(path, "wb") as f:
        f.write(header)
        remaining = size_mb * 1024 * 1024 - len(header)
        for _ in range(remaining // (1024 * 1024)):
            f.write(os.urandom(1024 * 1024))
        f.write(os.urandom(remaining % (1024 * 1024)))


def make_jpg(path, size_mb):
    """jpg magic 헤더 + 난수 데이터"""
    path = Path(path)
    header = bytes([0xFF, 0xD8, 0xFF, 0xE0])
    with open(path, "wb") as f:
        f.write(header)
        remaining = size_mb * 1024 * 1024 - len(header)
        f.write(os.urandom(remaining))


def make_png(path, size_mb):
    """png magic 헤더 + 난수 데이터"""
    path = Path(path)
    header = bytes([0x89, 0x50, 0x4E, 0x47,
                    0x0D, 0x0A, 0x1A, 0x0A])
    with open(path, "wb") as f:
        f.write(header)
        remaining = size_mb * 1024 * 1024 - len(header)
        f.write(os.urandom(remaining))


def make_abc(path, size_mb):
    """abc (Alembic) 난수 파일"""
    make_random_file(path, size_mb)


# ── 수정 함수 ─────────────────────────────────────────────────

def modify_1byte(src, dst):
    """중간 지점 1바이트 수정"""
    shutil.copy2(src, dst)
    size = os.path.getsize(dst)
    with open(dst, "r+b") as f:
        f.seek(size // 2)
        f.write(b"\x42")


def modify_percent(src, dst, percent):
    """파일의 percent% 구간 난수로 덮어쓰기 (In-place)"""
    shutil.copy2(src, dst)
    size = os.path.getsize(dst)
    modify_size = max(1, int(size * percent / 100))
    offset = random.randint(0, size - modify_size)
    with open(dst, "r+b") as f:
        f.seek(offset)
        f.write(os.urandom(modify_size))


def modify_insert(src, dst, insert_kb=1):
    """중간 지점에 데이터 삽입 (Insert — 뒤쪽 밀림)"""
    with open(src, "rb") as f:
        data = bytearray(f.read())
    insert_pos = len(data) // 2
    dummy = bytearray(os.urandom(insert_kb * 1024))
    data[insert_pos:insert_pos] = dummy
    with open(dst, "wb") as f:
        f.write(data)


def modify_full(src, dst):
    """전체 교체"""
    size = os.path.getsize(src)
    with open(dst, "wb") as f:
        f.write(os.urandom(size))


# ── 시나리오 함수 ─────────────────────────────────────────────

def scenario_1(dgit):
    """
    시나리오 1: Rolling Hash 당위성 — abc Insert 수정
    abc 파일 중간에 1KB 삽입 → delta 크기 측정
    """
    print("\n[Scenario 1] Rolling Hash — abc Insert")
    rows = []
    repo = REPO_DIR / "s1"

    for run_no in range(1, REPEAT + 1):
        for size_mb in [100]:
            setup_repo(dgit, repo)
            src = repo / "simulation.abc"
            dst = repo / "simulation_v2.abc"

            print(f"  run={run_no} size={size_mb}MB ... ", end="", flush=True)
            make_abc(src, size_mb)

            # 최초 커밋
            run(f"{dgit} add simulation.abc", cwd=repo)
            _, _, _, commit_t1 = run(
                f"{dgit} commit -m init simulation.abc", cwd=repo)

            # Insert 수정
            modify_insert(src, dst, insert_kb=1)
            shutil.move(str(dst), str(src))

            # 두 번째 커밋
            _, _, _, commit_t2 = run(
                f"{dgit} commit -m modify simulation.abc", cwd=repo)

            d_kb = delta_size_kb(repo)
            v_kb = vcs_size_kb(repo)
            savings = (1 - d_kb / (size_mb * 1024)) * 100

            print(f"delta={d_kb:.1f}KB savings={savings:.2f}%")
            rows.append({
                "scenario": 1,
                "run": run_no,
                "file_type": "abc",
                "size_mb": size_mb,
                "modify_type": "insert_1KB",
                "commit_time_s": round(commit_t2, 3),
                "delta_kb": round(d_kb, 2),
                "vcs_kb": round(v_kb, 2),
                "savings_pct": round(savings, 2),
            })
            teardown_repo(repo)

    save_csv("scenario_1.csv", rows, list(rows[0].keys()))


def scenario_2(dgit):
    """
    시나리오 2: 스토리지 절감 핵심 — exr 1GB × 5회 반복 커밋
    """
    print("\n[Scenario 2] Storage comparison — exr 1GB x5 commits")
    rows = []
    repo = REPO_DIR / "s2"

    for run_no in range(1, REPEAT + 1):
        setup_repo(dgit, repo)
        src = repo / "texture.exr"
        print(f"  run={run_no} generating 1GB exr ... ", end="", flush=True)
        make_exr(src, 1024)
        run(f"{dgit} add texture.exr", cwd=repo)

        for commit_no in range(1, 6):
            tmp = repo / "texture_mod.exr"
            modify_percent(src, tmp, 1)
            shutil.move(str(tmp), str(src))
            _, _, _, commit_t = run(
                f"{dgit} commit -m v{commit_no} texture.exr", cwd=repo)
            v_kb = vcs_size_kb(repo)
            d_kb = delta_size_kb(repo)
            print(f"commit{commit_no} vcs={v_kb/1024:.2f}GB ", end="", flush=True)
            rows.append({
                "scenario": 2,
                "run": run_no,
                "file_type": "exr",
                "size_mb": 1024,
                "commit_no": commit_no,
                "modify_type": "1pct",
                "commit_time_s": round(commit_t, 3),
                "delta_kb": round(d_kb, 2),
                "vcs_total_kb": round(v_kb, 2),
            })
        print()
        teardown_repo(repo)

    save_csv("scenario_2.csv", rows, list(rows[0].keys()))


def scenario_3(dgit):
    """
    시나리오 3: 텍스처 In-place 수정 — exr, tiff
    """
    print("\n[Scenario 3] Texture In-place — exr, tiff")
    rows = []
    repo = REPO_DIR / "s3"
    makers = [("exr", make_exr), ("tiff", make_tiff)]

    for run_no in range(1, REPEAT + 1):
        for size_mb in [100, 1024]:
            for ext, maker in makers:
                setup_repo(dgit, repo)
                src = repo / f"texture.{ext}"
                print(f"  run={run_no} {ext} {size_mb}MB ... ",
                      end="", flush=True)
                maker(src, size_mb)
                run(f"{dgit} add texture.{ext}", cwd=repo)
                run(f"{dgit} commit -m init texture.{ext}", cwd=repo)

                tmp = repo / f"texture_mod.{ext}"
                modify_percent(src, tmp, 0.01)
                shutil.move(str(tmp), str(src))
                _, _, _, commit_t = run(
                    f"{dgit} commit -m modify texture.{ext}", cwd=repo)

                d_kb = delta_size_kb(repo)
                savings = (1 - d_kb / (size_mb * 1024)) * 100
                print(f"delta={d_kb:.1f}KB savings={savings:.2f}%")

                rows.append({
                    "scenario": 3,
                    "run": run_no,
                    "file_type": ext,
                    "size_mb": size_mb,
                    "modify_type": "inplace_0.01pct",
                    "commit_time_s": round(commit_t, 3),
                    "delta_kb": round(d_kb, 2),
                    "savings_pct": round(savings, 2),
                })
                teardown_repo(repo)

    save_csv("scenario_3.csv", rows, list(rows[0].keys()))


def scenario_6(dgit):
    """
    시나리오 6: Early-exit 판단 시간 측정
    80%+ 변경 파일에서 should_use_full_copy() 소요 시간
    """
    print("\n[Scenario 6] Early-exit timing")
    rows = []
    repo = REPO_DIR / "s6"

    for run_no in range(1, REPEAT + 1):
        for size_mb in [100, 1024]:
            setup_repo(dgit, repo)
            src = repo / "model.fbx"
            print(f"  run={run_no} fbx {size_mb}MB ... ", end="", flush=True)
            make_random_file(src, size_mb)
            run(f"{dgit} add model.fbx", cwd=repo)
            run(f"{dgit} commit -m init model.fbx", cwd=repo)

            # 85% 변경 → Early-exit 트리거
            tmp = repo / "model_mod.fbx"
            modify_percent(src, tmp, 85)
            shutil.move(str(tmp), str(src))
            _, _, _, commit_t = run(
                f"{dgit} commit -m modify model.fbx", cwd=repo)

            d_kb = delta_size_kb(repo)
            print(f"commit_time={commit_t:.3f}s delta={d_kb:.1f}KB")

            rows.append({
                "scenario": 6,
                "run": run_no,
                "file_type": "fbx",
                "size_mb": size_mb,
                "modify_type": "85pct_earlyexit",
                "commit_time_s": round(commit_t, 3),
                "delta_kb": round(d_kb, 2),
            })
            teardown_repo(repo)

    save_csv("scenario_6.csv", rows, list(rows[0].keys()))


def scenario_7(dgit):
    """
    시나리오 7: 압축 포맷 fallback — jpg, png
    delta 파일이 생성되지 않아야 함
    """
    print("\n[Scenario 7] Compressed format fallback — jpg, png")
    rows = []
    repo = REPO_DIR / "s7"
    makers = [("jpg", make_jpg), ("png", make_png)]

    for run_no in range(1, REPEAT + 1):
        for ext, maker in makers:
            setup_repo(dgit, repo)
            src = repo / f"texture.{ext}"
            print(f"  run={run_no} {ext} 100MB ... ", end="", flush=True)
            maker(src, 100)
            run(f"{dgit} add texture.{ext}", cwd=repo)
            run(f"{dgit} commit -m init texture.{ext}", cwd=repo)

            tmp = repo / f"texture_mod.{ext}"
            modify_1byte(src, tmp)
            shutil.move(str(tmp), str(src))
            _, _, _, commit_t = run(
                f"{dgit} commit -m modify texture.{ext}", cwd=repo)

            d_kb = delta_size_kb(repo)
            v_kb = vcs_size_kb(repo)
            fullcopy = d_kb == 0  # delta 없으면 fullcopy

            print(f"delta={d_kb:.1f}KB fullcopy={fullcopy}")

            rows.append({
                "scenario": 7,
                "run": run_no,
                "file_type": ext,
                "size_mb": 100,
                "modify_type": "1byte",
                "commit_time_s": round(commit_t, 3),
                "delta_kb": round(d_kb, 2),
                "vcs_kb": round(v_kb, 2),
                "fullcopy": fullcopy,
            })
            teardown_repo(repo)

    save_csv("scenario_7.csv", rows, list(rows[0].keys()))


def scenario_8(dgit):
    """
    시나리오 8: 1바이트 수정 이론치 검증
    """
    print("\n[Scenario 8] 1-byte modify — theory max efficiency")
    rows = []
    repo = REPO_DIR / "s8"
    makers = [("exr", make_exr)]

    for run_no in range(1, REPEAT + 1):
        for size_mb in [100, 1024]:
            for ext, maker in makers:
                setup_repo(dgit, repo)
                src = repo / f"file.{ext}"
                print(f"  run={run_no} {ext} {size_mb}MB ... ",
                      end="", flush=True)
                maker(src, size_mb)
                run(f"{dgit} add file.{ext}", cwd=repo)
                run(f"{dgit} commit -m init file.{ext}", cwd=repo)

                tmp = repo / f"file_mod.{ext}"
                modify_1byte(src, tmp)
                shutil.move(str(tmp), str(src))
                rc, stdout, stderr, commit_t = run(
                    f"{dgit} commit -m modify file.{ext}", cwd=repo)
                print(f"  rc={rc} stdout={stdout.strip()} stderr={stderr.strip()}")

                d_kb = delta_size_kb(repo)
                savings = (1 - d_kb / (size_mb * 1024)) * 100

                # checkout 시간 측정
                head_path = repo / ".vcs" / "HEAD"
                if not head_path.exists():
                    print("HEAD not found, skipping checkout")
                    checkout_t = 0.0
                else:
                    head = head_path.read_text().strip()
                    _, _, _, checkout_t = run(
                        f"{dgit} checkout {head}", cwd=repo)

                print(f"delta={d_kb:.1f}KB savings={savings:.2f}% "
                      f"checkout={checkout_t:.3f}s")

                rows.append({
                    "scenario": 8,
                    "run": run_no,
                    "file_type": ext,
                    "size_mb": size_mb,
                    "modify_type": "1byte",
                    "commit_time_s": round(commit_t, 3),
                    "checkout_time_s": round(checkout_t, 3),
                    "delta_kb": round(d_kb, 2),
                    "savings_pct": round(savings, 2),
                })
                teardown_repo(repo)

    save_csv("scenario_8.csv", rows, list(rows[0].keys()))


# ── main ──────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="designer_git benchmark")
    parser.add_argument("--dgit", required=True,
                        help="Path to dgit executable")
    parser.add_argument("--scenarios", default="1,2,3,6,7,8",
                        help="Comma-separated scenario numbers to run")
    args = parser.parse_args()

    dgit = str(Path(args.dgit).resolve())
    scenarios = [int(s) for s in args.scenarios.split(",")]

    print(f"dgit: {dgit}")
    print(f"scenarios: {scenarios}")
    print(f"repeat: {REPEAT}")

    scenario_map = {
        1: scenario_1,
        2: scenario_2,
        3: scenario_3,
        6: scenario_6,
        7: scenario_7,
        8: scenario_8,
    }

    for s in scenarios:
        if s in scenario_map:
            scenario_map[s](dgit)
        else:
            print(f"[SKIP] scenario {s} not implemented yet")

    print("\nAll done.")


if __name__ == "__main__":
    main()