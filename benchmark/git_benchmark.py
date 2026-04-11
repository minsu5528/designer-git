import os

import csv

import time

import subprocess

from pathlib import Path

from datetime import datetime



ITERATIONS = 5

BENCHMARK_DIR_NAME = "benchmark"





def get_folder_size_bytes(folder_path: Path) -> int:

    total = 0

    if not folder_path.exists():

        return 0



    for root, _, files in os.walk(folder_path):

        for file_name in files:

            file_path = Path(root) / file_name

            try:

                total += file_path.stat().st_size

            except (FileNotFoundError, PermissionError, OSError):

                continue

    return total





def run_git_command(args, cwd: Path):

    start = time.perf_counter()

    result = subprocess.run(

        args,

        cwd=cwd,

        text=True,

        capture_output=True,

        shell=False,

        encoding="utf-8",

        errors="replace"

    )

    end = time.perf_counter()

    return result, (end - start)





def find_repo_root(start_path: Path):

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

    return any(part.startswith(".") for part in rel_path.parts)





def should_skip_file(file_path: Path, repo_path: Path) -> bool:

    try:

        rel_to_repo = file_path.relative_to(repo_path)

    except ValueError:

        return True



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

    with open(file_path, "ab") as f:

        f.write(byte_to_add)

    return byte_to_add.decode("ascii", errors="replace")





def modify_all_files_in_folder(target_dir: Path, repo_path: Path, iteration: int):

    modified_files = []

    failed_files = []



    for root, _, files in os.walk(target_dir):

        for file_name in sorted(files):

            file_path = Path(root) / file_name



            if should_skip_file(file_path, repo_path):

                continue



            try:

                appended_char = append_one_byte(file_path, iteration)

                rel = file_path.relative_to(repo_path).as_posix()

                modified_files.append(f"{rel} (+1B:{appended_char})")

            except Exception as e:

                rel = file_path.relative_to(repo_path).as_posix()

                failed_files.append(f"{rel} | {type(e).__name__}: {e}")



    return modified_files, failed_files





def get_current_commit_hash(repo_path: Path) -> str:

    result, _ = run_git_command(["git", "rev-parse", "HEAD"], repo_path)

    if result.returncode != 0:

        return ""

    return result.stdout.strip()





def get_staged_file_list(repo_path: Path, target_relpath: str):

    result, _ = run_git_command(

        ["git", "diff", "--cached", "--name-only", "--", target_relpath],

        repo_path

    )

    if result.returncode != 0:

        return []

    return [line.strip() for line in result.stdout.splitlines() if line.strip()]





def main():

    script_dir = Path(__file__).resolve().parent

    repo_path = find_repo_root(script_dir)



    if repo_path is None:

        print("❌ 오류: Git 저장소를 찾지 못했습니다.")

        return



    git_dir = repo_path / ".git"

    lfs_objects_dir = git_dir / "lfs" / "objects"



    benchmark_dir = repo_path / BENCHMARK_DIR_NAME

    benchmark_dir.mkdir(parents=True, exist_ok=True)



    print("========================================")

    print("📦 Git LFS 벤치마크 (1바이트 변경 + 5회 커밋)")

    print("========================================\n")



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



    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    csv_path = benchmark_dir / f"{target_dir.name}_results_{timestamp}.csv"



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



    prev_lfs_size_mb = get_folder_size_bytes(lfs_objects_dir) / (1024 * 1024)



    with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:

        writer = csv.DictWriter(f, fieldnames=fieldnames)

        writer.writeheader()



        for i in range(1, ITERATIONS + 1):

            print(f"▶️ [{i}/{ITERATIONS}] 파일 수정 중...")



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



            current_lfs_size_mb = get_folder_size_bytes(lfs_objects_dir) / (1024 * 1024)

            lfs_increase_mb = current_lfs_size_mb - prev_lfs_size_mb

            prev_lfs_size_mb = current_lfs_size_mb



            commit_hash = get_current_commit_hash(repo_path)



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



            print(f"   - git add 시간: {round(add_time, 4)}초")

            print(f"   - git commit 시간: {round(commit_time, 4)}초")

            print(f"   - 이번 커밋 LFS 증가량: {round(lfs_increase_mb, 2)} MB")

            print(f"   - 누적 LFS 용량: {round(current_lfs_size_mb, 2)} MB\n")



    print("✅ 실험 완료")

    print(f"CSV 저장 위치: {csv_path}")





if __name__ == "__main__":

    main()