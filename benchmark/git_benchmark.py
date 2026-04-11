import os
import csv
import time
import subprocess
from pathlib import Path
from datetime import datetime

# 벤치마크를 위해 반복할 커밋 횟수 설정
ITERATIONS = 5
# 결과 파일(CSV)을 저장할 폴더 이름 설정
BENCHMARK_DIR_NAME = "benchmark"


def get_folder_size_bytes(folder_path: Path) -> int:
    """특정 폴더의 전체 용량(바이트 단위)을 계산하는 함수"""
    total = 0
    if not folder_path.exists():
        return 0

    # 폴더 내의 모든 하위 폴더와 파일을 순회하며 파일 크기를 합산
    for root, _, files in os.walk(folder_path):
        for file_name in files:
            file_path = Path(root) / file_name
            try:
                total += file_path.stat().st_size
            # 파일 접근 권한이 없거나 삭제된 경우 무시하고 계속 진행
            except (FileNotFoundError, PermissionError, OSError):
                continue
    return total


def run_git_command(args, cwd: Path):
    """Git 명령어를 실행하고, 실행에 걸린 시간을 측정하는 함수"""
    start = time.perf_counter() # 시작 시간 기록
    result = subprocess.run(
        args,
        cwd=cwd,
        text=True,
        capture_output=True,
        shell=False,
        encoding="utf-8",
        errors="replace"
    )
    end = time.perf_counter() # 종료 시간 기록
    return result, (end - start) # 실행 결과와 소요 시간 반환


def find_repo_root(start_path: Path):
    """현재 위치를 기준으로 Git 저장소의 최상위(Root) 폴더 경로를 찾는 함수"""
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=start_path,
        text=True,
        capture_output=True,
        shell=False,
        encoding="utf-8",
        errors="replace"
    )
    if result.returncode == 0:
        return Path(result.stdout.strip())
    return None


def is_hidden_relpath(rel_path: Path) -> bool:
    """경로 중에 숨김 폴더나 파일(점(.)으로 시작)이 포함되어 있는지 확인하는 함수"""
    return any(part.startswith(".") for part in rel_path.parts)


def should_skip_file(file_path: Path, repo_path: Path) -> bool:
    """벤치마크 파일 수정 단계에서 제외해야 할 파일인지 판별하는 함수"""
    try:
        rel_to_repo = file_path.relative_to(repo_path)
    except ValueError:
        return True # 저장소 외부의 파일이면 스킵

    # 숨김 파일이거나, .git 폴더 내부, 또는 결과 저장용 benchmark 폴더 내부면 스킵
    if is_hidden_relpath(rel_to_repo):
        return True

    if ".git" in rel_to_repo.parts:
        return True

    if BENCHMARK_DIR_NAME in rel_to_repo.parts:
        return True

    return False


def append_one_byte(file_path: Path, iteration: int) -> str:
    """
    파일 끝에 1바이트 추가.
    매 회 다른 바이트가 들어가도록 A, B, C... 순환.
    """
    byte_to_add = bytes([(iteration % 26) + 65])  # A~Z
    # 파일을 바이너리 추가 모드('ab')로 열어 1바이트를 덧붙임
    with open(file_path, "ab") as f:
        f.write(byte_to_add)
    return byte_to_add.decode("ascii", errors="replace")


def modify_all_files_in_folder(target_dir: Path, repo_path: Path, iteration: int):
    """지정된 폴더 내의 모든 대상 파일에 1바이트씩 추가하여 변경 사항을 만드는 함수"""
    modified_files = []
    failed_files = []

    for root, _, files in os.walk(target_dir):
        for file_name in sorted(files):
            file_path = Path(root) / file_name

            # 제외 대상 파일인지 검사
            if should_skip_file(file_path, repo_path):
                continue

            try:
                # 파일 끝에 1바이트 추가 후, 성공 목록에 기록
                appended_char = append_one_byte(file_path, iteration)
                rel = file_path.relative_to(repo_path).as_posix()
                modified_files.append(f"{rel} (+1B:{appended_char})")
            except Exception as e:
                # 에러 발생 시 실패 목록에 기록
                rel = file_path.relative_to(repo_path).as_posix()
                failed_files.append(f"{rel} | {type(e).__name__}: {e}")

    return modified_files, failed_files


def get_current_commit_hash(repo_path: Path) -> str:
    """가장 최근에 생성된 커밋의 고유 해시값을 가져오는 함수"""
    result, _ = run_git_command(["git", "rev-parse", "HEAD"], repo_path)
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def get_staged_file_list(repo_path: Path, target_relpath: str):
    """현재 'git add'되어 커밋 대기 중(Staged)인 파일들의 목록을 가져오는 함수"""
    result, _ = run_git_command(
        ["git", "diff", "--cached", "--name-only", "--", target_relpath],
        repo_path
    )
    if result.returncode != 0:
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main():
    """스크립트 실행 시 가장 먼저 호출되는 메인 로직"""
    script_dir = Path(__file__).resolve().parent
    repo_path = find_repo_root(script_dir)

    if repo_path is None:
        print("❌ 오류: Git 저장소를 찾지 못했습니다.")
        return

    # Git LFS가 파일을 저장하는 내부 경로 설정
    git_dir = repo_path / ".git"
    lfs_objects_dir = git_dir / "lfs" / "objects"

    # 결과물(CSV)을 저장할 benchmark 폴더 생성
    benchmark_dir = repo_path / BENCHMARK_DIR_NAME
    benchmark_dir.mkdir(parents=True, exist_ok=True)

    print("========================================")
    print("📦 Git LFS 벤치마크 (1바이트 변경 + 5회 커밋)")
    print("========================================\n")

    # 사용자로부터 테스트를 진행할 대상 폴더명 입력받기
    user_input = input(
        "커밋할 폴더 이름이나 경로를 입력하세요\n"
        "(예: tree_test_file): "
    ).strip()

    if not user_input:
        print("❌ 오류: 입력값이 비어 있습니다.")
        return

    target_dir = (repo_path / user_input).resolve()

    if not target_dir.exists() or not target_dir.is_dir():
        print(f"❌ 오류: 해당 폴더를 찾을 수 없습니다: {target_dir}")
        return

    try:
        target_relpath = target_dir.relative_to(repo_path).as_posix()
    except ValueError:
        print("❌ 오류: 폴더가 Git 저장소 내부에 있지 않습니다.")
        return

    # CSV 파일명에 들어갈 현재 시간 생성
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = benchmark_dir / f"{target_dir.name}_results_{timestamp}.csv"

    # CSV 파일의 헤더(열 이름) 정의
    fieldnames = [
        "iteration",
        "target_folder",
        "modified_file_count",
        "modified_files",
        "failed_file_count",
        "failed_files",
        "folder_size_mb",
        "add_time_sec",
        "commit_time_sec",
        "total_git_time_sec",
        "commit_lfs_increase_mb",
        "cumulative_lfs_storage_mb",
        "commit_hash"
    ]

    print(f"📁 대상 폴더: {target_relpath}")
    print(f"📝 결과 CSV: {csv_path}\n")

    # 초기 LFS 폴더 용량 측정
    prev_lfs_size_mb = get_folder_size_bytes(lfs_objects_dir) / (1024 * 1024)

    # CSV 파일 쓰기 모드로 열기
    with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        # 설정된 횟수(ITERATIONS)만큼 파일 수정 및 커밋 반복
        for i in range(1, ITERATIONS + 1):
            print(f"▶️ [{i}/{ITERATIONS}] 파일 수정 중...")

            # 1. 파일 수정 (1바이트 추가)
            modified_files, failed_files = modify_all_files_in_folder(
                target_dir=target_dir,
                repo_path=repo_path,
                iteration=i
            )

            if not modified_files:
                print("❌ 수정된 파일이 없습니다. 커밋을 중단합니다.")
                if failed_files:
                    print("실패 파일:")
                    for item in failed_files[:10]:
                        print("  -", item)
                return

            current_folder_size_mb = get_folder_size_bytes(target_dir) / (1024 * 1024)

            print(f"   - 수정 파일 수: {len(modified_files)}")
            print(f"   - 실패 파일 수: {len(failed_files)}")

            # 2. 변경된 파일들을 Git 스테이징 영역에 추가 (git add) 및 시간 측정
            add_result, add_time = run_git_command(
                ["git", "add", target_relpath],
                repo_path
            )
            if add_result.returncode != 0:
                print("❌ git add 실패")
                print(add_result.stdout)
                print(add_result.stderr)
                return

            staged_files = get_staged_file_list(repo_path, target_relpath)
            if not staged_files:
                print("❌ staging된 파일이 없습니다. 커밋 중단.")
                return

            # 3. 스테이징된 파일들을 커밋 (git commit) 및 시간 측정
            commit_msg = f"chore: benchmark {target_dir.name} auto-commit {i}"
            commit_result, commit_time = run_git_command(
                ["git", "commit", "-m", commit_msg],
                repo_path
            )

            if commit_result.returncode != 0:
                print("❌ git commit 실패")
                print(commit_result.stdout)
                print(commit_result.stderr)
                return

            # 4. 커밋 후 증가한 LFS 저장소 용량 계산
            current_lfs_size_mb = get_folder_size_bytes(lfs_objects_dir) / (1024 * 1024)
            lfs_increase_mb = current_lfs_size_mb - prev_lfs_size_mb
            prev_lfs_size_mb = current_lfs_size_mb

            commit_hash = get_current_commit_hash(repo_path)

            # 5. 측정된 데이터를 CSV 파일의 한 줄(Row)로 기록
            row = {
                "iteration": i,
                "target_folder": target_relpath,
                "modified_file_count": len(modified_files),
                "modified_files": " ; ".join(modified_files),
                "failed_file_count": len(failed_files),
                "failed_files": " ; ".join(failed_files),
                "folder_size_mb": round(current_folder_size_mb, 2),
                "add_time_sec": round(add_time, 4),
                "commit_time_sec": round(commit_time, 4),
                "total_git_time_sec": round(add_time + commit_time, 4),
                "commit_lfs_increase_mb": round(lfs_increase_mb, 2),
                "cumulative_lfs_storage_mb": round(current_lfs_size_mb, 2),
                "commit_hash": commit_hash
            }
            writer.writerow(row)
            f.flush()

            # 콘솔에 진행 상황 요약 출력
            print(f"   - git add 시간: {round(add_time, 4)}초")
            print(f"   - git commit 시간: {round(commit_time, 4)}초")
            print(f"   - 이번 커밋 LFS 증가량: {round(lfs_increase_mb, 2)} MB")
            print(f"   - 누적 LFS 용량: {round(current_lfs_size_mb, 2)} MB\n")

    print("✅ 실험 완료")
    print(f"CSV 저장 위치: {csv_path}")


if __name__ == "__main__":
    main()