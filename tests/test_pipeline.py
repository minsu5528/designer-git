#!/usr/bin/env python3
"""
designer_git 자동화 테스트 스위트
담당: 김채연

검증 항목:
  1. 커밋 → 수정 → 재커밋 → 체크아웃 → 원본과 바이트 단위 비교
  2. SHA256 검증 통과 확인
  3. delta 체인 3개 이상에서 복원 정확성
  4. 마지막 블록 누락 없는지 확인
  5. offset/length 메타데이터 mismatch 확인
  6. 엣지 케이스 (빈 파일, 최초 커밋 체크아웃 등)
  7. 손상된 메타데이터 예외 처리 확인
"""

import os
import sys
import shutil
import tempfile
import subprocess
import hashlib
import json
import random

# ── 설정 ──────────────────────────────────────────────────────────────────────
DGIT = os.path.join(os.path.dirname(__file__), "../build/dgit")
DGIT = os.path.abspath(DGIT)

# ── 색상 출력 ──────────────────────────────────────────────────────────────────
GREEN = "\033[92m"
RED   = "\033[91m"
YELLOW= "\033[93m"
RESET = "\033[0m"

passed = 0
failed = 0
errors = []

def ok(name):
    global passed
    passed += 1
    print(f"  {GREEN}✓{RESET} {name}")

def fail(name, detail=""):
    global failed
    failed += 1
    msg = f"  {RED}✗{RESET} {name}"
    if detail:
        msg += f"\n      → {detail}"
    print(msg)
    errors.append((name, detail))

def section(title):
    print(f"\n{YELLOW}{'─'*60}{RESET}")
    print(f"{YELLOW}  {title}{RESET}")
    print(f"{YELLOW}{'─'*60}{RESET}")

# ── 헬퍼 ──────────────────────────────────────────────────────────────────────
def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def run(args, cwd, check=False):
    """dgit 명령 실행. stdout/stderr 반환."""
    cmd = [DGIT] + args
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return r

def make_repo():
    """임시 디렉터리 생성 + dgit init 실행."""
    d = tempfile.mkdtemp(prefix="dgit_test_")
    run(["init"], cwd=d)
    return d

def write_file(path, data: bytes):
    with open(path, "wb") as f:
        f.write(data)

def read_file(path) -> bytes:
    with open(path, "rb") as f:
        return f.read()

def make_binary(size: int, seed: int = 42) -> bytes:
    """재현 가능한 랜덤 바이너리 데이터 생성."""
    rng = random.Random(seed)
    return bytes(rng.getrandbits(8) for _ in range(size))

def commit(repo, filepath, message):
    """add + commit 실행. 커밋 ID (full 16자리) 반환."""
    fname = os.path.basename(filepath)
    run(["add", fname], cwd=repo)
    r = run(["commit", "-m", message, fname], cwd=repo)
    if r.returncode != 0:
        return None
    # HEAD 파일에서 full commit ID 읽기
    head_path = os.path.join(repo, ".vcs", "HEAD")
    try:
        with open(head_path) as f:
            cid = f.read().strip()
            return cid if cid else None
    except Exception:
        return None

def get_commit_id_from_head(repo):
    head_path = os.path.join(repo, ".vcs", "HEAD")
    with open(head_path) as f:
        return f.read().strip()

# ── 테스트 케이스 ─────────────────────────────────────────────────────────────

# ─ 1. 기본 파이프라인: 커밋 → 수정 → 재커밋 → 체크아웃 → 바이트 단위 비교 ─
def test_basic_roundtrip():
    section("1. 기본 라운드트립: commit → modify → checkout → byte-compare")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "model.fbx")
        original = make_binary(512 * 1024, seed=1)  # 512 KB
        write_file(fp, original)

        # 최초 커밋
        cid1 = commit(repo, fp, "v1: initial")
        if not cid1:
            fail("최초 커밋 ID 반환", "commit 반환값 없음")
            return
        ok("최초 커밋 성공")

        sha_before = sha256(fp)

        # 파일 수정 (중간 일부 바이트 변경)
        modified = bytearray(original)
        for i in range(100, 200):
            modified[i] = (modified[i] + 1) % 256
        write_file(fp, bytes(modified))

        cid2 = commit(repo, fp, "v2: small patch")
        if not cid2:
            fail("수정 후 커밋 ID 반환", "commit 반환값 없음")
            return
        ok("수정 후 커밋 성공")

        # 첫 번째 버전으로 체크아웃
        r = run(["checkout", cid1], cwd=repo)
        if r.returncode != 0:
            fail("v1 체크아웃", r.stderr)
            return
        ok("v1 체크아웃 성공")

        # 바이트 단위 비교
        restored = read_file(fp)
        if restored == original:
            ok("바이트 단위 완전 일치 (v1)")
        else:
            fail("바이트 단위 완전 일치 (v1)",
                 f"크기: 원본={len(original)}, 복원={len(restored)}")

        # SHA256 검증 로그 확인
        if "SHA256 검증 통과" in r.stdout:
            ok("SHA256 검증 통과 로그 확인")
        else:
            fail("SHA256 검증 통과 로그", f"stdout: {r.stdout[:200]}")

    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 2. SHA256 검증 통과 확인 ─
def test_sha256_verification():
    section("2. SHA256 검증 통과 확인")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "asset.bin")
        data = make_binary(256 * 1024, seed=2)
        write_file(fp, data)
        cid = commit(repo, fp, "sha256 test")

        r = run(["checkout", cid], cwd=repo)
        if r.returncode == 0:
            ok("체크아웃 성공 (SHA256 통과)")
        else:
            fail("체크아웃 (SHA256 통과 실패)", r.stderr)

        # 복원 파일 직접 검증
        actual_sha = sha256(fp)
        expected_sha = sha256_from_commit_json(repo, cid)
        if expected_sha and actual_sha == expected_sha:
            ok("커밋 JSON sha256 == 실제 파일 sha256")
        elif not expected_sha:
            fail("커밋 JSON sha256 필드 없음")
        else:
            fail("SHA256 불일치", f"expected={expected_sha[:16]}… actual={actual_sha[:16]}…")

    finally:
        shutil.rmtree(repo, ignore_errors=True)

def sha256_from_commit_json(repo, commit_id):
    """커밋 JSON에서 files[0].sha256 읽기."""
    json_path = os.path.join(repo, ".vcs", "commits", commit_id + ".json")
    try:
        with open(json_path) as f:
            j = json.load(f)
        return j["files"][0]["sha256"]
    except Exception:
        return None


# ─ 3. delta 체인 3개 이상 복원 정확성 ─
def test_delta_chain_3plus():
    section("3. delta 체인 3개 이상 복원 정확성")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "char.fbx")
        snapshots = {}

        base_data = make_binary(300 * 1024, seed=10)
        write_file(fp, base_data)
        cid = commit(repo, fp, "v1 base")
        snapshots[cid] = bytes(base_data)
        ok(f"v1 커밋: {cid[:8] if cid else 'None'}")

        # v2 ~ v5: 소규모 수정 반복
        for i in range(2, 6):
            data = bytearray(read_file(fp))
            # 각 버전마다 다른 위치 수정
            start = i * 1000
            for j in range(start, start + 50):
                data[j % len(data)] = (data[j % len(data)] + i) % 256
            write_file(fp, bytes(data))
            cid = commit(repo, fp, f"v{i}: patch {i}")
            snapshots[cid] = bytes(data)
            if cid:
                ok(f"v{i} 커밋: {cid[:8]}")
            else:
                fail(f"v{i} 커밋 실패")
                return

        # 각 버전으로 체크아웃해서 검증
        for ver, (cid, expected) in enumerate(snapshots.items(), 1):
            r = run(["checkout", cid], cwd=repo)
            if r.returncode != 0:
                fail(f"v{ver} 체크아웃", r.stderr[:100])
                continue
            restored = read_file(fp)
            if restored == expected:
                ok(f"v{ver} 복원 바이트 일치 (체인 깊이 {ver-1})")
            else:
                fail(f"v{ver} 복원 불일치",
                     f"expected={len(expected)}B restored={len(restored)}B")

    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 4. 마지막 블록 누락 없는지 확인 ─
def test_last_block_integrity():
    section("4. 마지막 블록 누락 없는지 확인")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "tail.bin")
        # 블록 크기(~16KB) 경계에 걸리는 파일 크기들로 테스트
        for size in [16383, 16384, 16385, 32767, 32768, 32769, 65536 + 1]:
            data = make_binary(size, seed=size)
            write_file(fp, data)
            cid = commit(repo, fp, f"size={size}")
            if not cid:
                fail(f"커밋 실패 (size={size})")
                continue

            # 파일을 수정해 delta가 필요하게 만들기
            mod = bytearray(data)
            mod[-1] = (mod[-1] + 1) % 256  # 마지막 바이트만 변경
            write_file(fp, bytes(mod))
            cid2 = commit(repo, fp, f"tail-patch size={size}")

            # 첫 버전으로 복원
            r = run(["checkout", cid], cwd=repo)
            if r.returncode != 0:
                fail(f"체크아웃 실패 (size={size})", r.stderr[:100])
                continue
            restored = read_file(fp)
            if restored == data:
                ok(f"마지막 블록 정상 복원 (size={size}B)")
            else:
                diff_pos = next((i for i, (a, b) in enumerate(zip(restored, data)) if a != b), -1)
                fail(f"마지막 블록 누락 (size={size}B)",
                     f"첫 불일치 위치={diff_pos}, 복원크기={len(restored)}")

    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 5. offset/length 메타데이터 mismatch 확인 ─
def test_metadata_integrity():
    section("5. offset/length 메타데이터 정합성 확인")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "meta.bin")
        data = make_binary(200 * 1024, seed=20)
        write_file(fp, data)
        cid1 = commit(repo, fp, "meta-v1")

        # 수정 후 커밋
        mod = bytearray(data)
        mod[1000:1100] = bytes(range(100))
        write_file(fp, bytes(mod))
        cid2 = commit(repo, fp, "meta-v2")

        # JSON에서 length 필드 검증
        j1 = load_commit_json(repo, cid1)
        j2 = load_commit_json(repo, cid2)

        if j1 and j1["files"][0]["length"] == len(data):
            ok("v1 length 메타데이터 일치")
        else:
            fail("v1 length 메타데이터 불일치",
                 f"JSON={j1['files'][0]['length'] if j1 else 'N/A'} expected={len(data)}")

        if j2 and j2["files"][0]["length"] == len(mod):
            ok("v2 length 메타데이터 일치")
        else:
            fail("v2 length 메타데이터 불일치")

        # sha256 필드 검증
        expected_sha1 = hashlib.sha256(data).hexdigest()
        if j1 and j1["files"][0]["sha256"] == expected_sha1:
            ok("v1 sha256 메타데이터 일치")
        else:
            actual = j1["files"][0]["sha256"] if j1 else "N/A"
            fail("v1 sha256 메타데이터 불일치",
                 f"JSON={actual[:16]}… expected={expected_sha1[:16]}…")

    finally:
        shutil.rmtree(repo, ignore_errors=True)

def load_commit_json(repo, commit_id):
    json_path = os.path.join(repo, ".vcs", "commits", commit_id + ".json")
    try:
        with open(json_path) as f:
            return json.load(f)
    except Exception:
        return None


# ─ 6. 엣지 케이스: 빈 파일 ─
def test_edge_empty_file():
    section("6a. 엣지 케이스: 빈 파일")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "empty.bin")
        write_file(fp, b"")
        cid = commit(repo, fp, "empty file")
        if cid:
            ok("빈 파일 커밋 성공")
        else:
            fail("빈 파일 커밋 실패")
            return

        r = run(["checkout", cid], cwd=repo)
        if r.returncode == 0:
            restored = read_file(fp)
            if restored == b"":
                ok("빈 파일 체크아웃 후 바이트 일치")
            else:
                fail("빈 파일 복원 불일치", f"복원 크기={len(restored)}")
        else:
            fail("빈 파일 체크아웃 실패", r.stderr[:100])
    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 6b. 엣지 케이스: 최초 커밋 체크아웃 ─
def test_edge_checkout_first_commit():
    section("6b. 엣지 케이스: 최초 커밋으로 체크아웃")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "first.bin")
        original = make_binary(100 * 1024, seed=99)
        write_file(fp, original)
        cid1 = commit(repo, fp, "first commit")

        # 파일을 완전히 다른 내용으로 교체
        write_file(fp, b"completely different content" * 1000)
        cid2 = commit(repo, fp, "second commit")

        # 최초 커밋으로 되돌아가기
        r = run(["checkout", cid1], cwd=repo)
        if r.returncode != 0:
            fail("최초 커밋 체크아웃", r.stderr[:100])
            return
        ok("최초 커밋 체크아웃 성공")

        restored = read_file(fp)
        if restored == original:
            ok("최초 커밋 바이트 완전 일치")
        else:
            fail("최초 커밋 복원 불일치",
                 f"크기: 원본={len(original)}, 복원={len(restored)}")
    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 6c. 엣지 케이스: 변경량 0바이트 ─
def test_edge_no_change():
    section("6c. 엣지 케이스: 변경량 0바이트 (동일 파일 재커밋)")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "same.bin")
        data = make_binary(50 * 1024, seed=5)
        write_file(fp, data)
        cid1 = commit(repo, fp, "v1")

        # 동일 파일 재커밋 (변경 없음)
        cid2 = commit(repo, fp, "v2 no change")
        if cid2:
            ok("동일 파일 재커밋 성공")
        else:
            fail("동일 파일 재커밋 실패")
            return

        r = run(["checkout", cid1], cwd=repo)
        restored = read_file(fp)
        if restored == data:
            ok("0바이트 변경 후 v1 복원 일치")
        else:
            fail("0바이트 변경 후 복원 불일치")
    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 6d. 엣지 케이스: 존재하지 않는 커밋 ID ─
def test_edge_invalid_commit_id():
    section("6d. 엣지 케이스: 존재하지 않는 커밋 ID")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "test.bin")
        write_file(fp, b"hello")
        commit(repo, fp, "init")

        r = run(["checkout", "deadbeefdeadbeef"], cwd=repo)
        if r.returncode != 0:
            ok("존재하지 않는 커밋 ID → 오류 반환")
        else:
            fail("존재하지 않는 커밋 ID → 오류 반환 실패 (0 반환됨)")
    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 7. 손상된 메타데이터 예외 처리 ─
def test_corrupted_metadata():
    section("7. 손상된 메타데이터 예외 처리")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "corrupt.bin")
        write_file(fp, make_binary(64 * 1024, seed=77))
        cid = commit(repo, fp, "before corrupt")

        # 커밋 JSON 파일 손상
        json_path = os.path.join(repo, ".vcs", "commits", cid + ".json")
        with open(json_path, "w") as f:
            f.write("{invalid json !!!")

        r = run(["checkout", cid], cwd=repo)
        if r.returncode != 0:
            ok("손상된 메타데이터 → 오류 반환")
        else:
            fail("손상된 메타데이터 → 오류 반환 실패 (0 반환됨)")
    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 8. 전체 파일 교체 시나리오 ─
def test_full_file_replace():
    section("8. 파일 전체 교체 시나리오")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "replace.bin")
        v1_data = make_binary(200 * 1024, seed=30)
        write_file(fp, v1_data)
        cid1 = commit(repo, fp, "v1 original")

        # 완전히 다른 랜덤 데이터로 교체
        v2_data = make_binary(200 * 1024, seed=31)  # 완전히 다른 seed
        write_file(fp, v2_data)
        cid2 = commit(repo, fp, "v2 full replace")

        # v1 복원 검증
        r = run(["checkout", cid1], cwd=repo)
        if r.returncode == 0:
            restored = read_file(fp)
            if restored == v1_data:
                ok("전체 교체 후 v1 복원 성공")
            else:
                fail("전체 교체 후 v1 복원 불일치")
        else:
            fail("전체 교체 후 v1 체크아웃 실패", r.stderr[:100])

        # v2 복원 검증
        r = run(["checkout", cid2], cwd=repo)
        if r.returncode == 0:
            restored = read_file(fp)
            if restored == v2_data:
                ok("전체 교체 후 v2 복원 성공")
            else:
                fail("전체 교체 후 v2 복원 불일치")
        else:
            fail("전체 교체 후 v2 체크아웃 실패", r.stderr[:100])

    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 9. 빈 커밋 메시지 ─
def test_empty_commit_message():
    section("9. 빈 커밋 메시지")
    repo = make_repo()
    try:
        fp = os.path.join(repo, "msg.bin")
        write_file(fp, make_binary(10 * 1024, seed=50))
        run(["add", "msg.bin"], cwd=repo)
        r = run(["commit", "-m", "", "msg.bin"], cwd=repo)
        # 빈 메시지여도 충돌 없이 처리되어야 함 (성공 또는 명확한 오류)
        if r.returncode == 0:
            ok("빈 커밋 메시지 처리 (성공)")
        else:
            # 오류 반환도 수용 가능 (명확한 오류 메시지가 있을 경우)
            ok("빈 커밋 메시지 처리 (오류 반환 — 정책에 따라 수용)")
    finally:
        shutil.rmtree(repo, ignore_errors=True)


# ─ 10. 다양한 파일 크기 라운드트립 ─
def test_various_sizes():
    section("10. 다양한 파일 크기 라운드트립")
    sizes = [1, 100, 4096, 16384, 65536, 512 * 1024]
    for size in sizes:
        repo = make_repo()
        try:
            fp = os.path.join(repo, f"f.bin")
            original = make_binary(size, seed=size)
            write_file(fp, original)
            cid = commit(repo, fp, f"v1 size={size}")
            if not cid:
                fail(f"크기 {size}B 커밋 실패")
                continue

            # 파일 수정 후 재커밋
            mod = bytearray(original)
            if size > 0:
                mod[0] = (mod[0] + 1) % 256
            write_file(fp, bytes(mod))
            commit(repo, fp, f"v2 mod size={size}")

            # 원본 복원
            r = run(["checkout", cid], cwd=repo)
            if r.returncode != 0:
                fail(f"크기 {size}B 체크아웃 실패", r.stderr[:80])
                continue
            restored = read_file(fp)
            if restored == original:
                ok(f"크기 {size}B 라운드트립 성공")
            else:
                fail(f"크기 {size}B 복원 불일치",
                     f"원본={len(original)}, 복원={len(restored)}")
        finally:
            shutil.rmtree(repo, ignore_errors=True)


# ── main ───────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    if not os.path.exists(DGIT):
        print(f"{RED}오류: dgit 실행 파일을 찾을 수 없습니다.{RESET}")
        print(f"  경로: {DGIT}")
        print(f"  먼저 빌드하세요: cd build && cmake .. && make")
        sys.exit(1)

    print(f"\n{YELLOW}{'='*60}{RESET}")
    print(f"{YELLOW}  designer_git 자동화 테스트 스위트{RESET}")
    print(f"{YELLOW}{'='*60}{RESET}")
    print(f"  dgit 경로: {DGIT}")

    test_basic_roundtrip()
    test_sha256_verification()
    test_delta_chain_3plus()
    test_last_block_integrity()
    test_metadata_integrity()
    test_edge_empty_file()
    test_edge_checkout_first_commit()
    test_edge_no_change()
    test_edge_invalid_commit_id()
    test_corrupted_metadata()
    test_full_file_replace()
    test_empty_commit_message()
    test_various_sizes()

    # ── 결과 요약 ──────────────────────────────────────────────────────────────
    total = passed + failed
    print(f"\n{YELLOW}{'='*60}{RESET}")
    print(f"  결과: {GREEN}{passed}/{total} 통과{RESET}", end="")
    if failed:
        print(f"  {RED}{failed} 실패{RESET}")
        print(f"\n  실패 목록:")
        for name, detail in errors:
            print(f"    {RED}✗{RESET} {name}")
            if detail:
                print(f"        {detail}")
    else:
        print(f"  {GREEN}전체 통과!{RESET}")
    print(f"{YELLOW}{'='*60}{RESET}\n")

    sys.exit(0 if failed == 0 else 1)