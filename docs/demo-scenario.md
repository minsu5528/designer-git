# 시연 시나리오

## 시연 흐름 요약

```
dgit init  →  dgit add  →  dgit commit  →  (파일 수정)
→  dgit commit  →  dgit log  →  dgit checkout  →  dgit diff
```

---

## Step 0. 사전 준비

```bash
# 작업 폴더 생성 및 테스트 파일 준비
mkdir demo && cd demo
cp /path/to/character.fbx .          # 100MB 이상 fbx 파일 준비
ls -lh character.fbx                 # 파일 크기 확인 (발표 중 보여줌)
```

---

## Step 1. `dgit init` — 저장소 초기화

```bash
dgit init
```

**예상 출력**:

```
(출력 없음 — 성공 시 종료 코드 0만 반환)
```

**확인**:

```bash
ls .vcs/
# objects/  commits/  index  HEAD
```

---

## Step 2. `dgit add` — 파일 추적 등록

```bash
dgit add character.fbx
```

**예상 출력**:

```
추가 완료: 1개 파일
```

**확인**:

```bash
cat .vcs/index
# character.fbx
```

---

## Step 3. `dgit commit` — 첫 번째 커밋 (베이스 저장)

```bash
dgit commit -m "캐릭터 초기 버전"
```

**예상 출력**:

```
커밋 완료: 1개 파일 / 커밋 ID: a1b2c3d4
```

**확인**:

```bash
ls .vcs/objects/base/
# character.fbx

cat .vcs/HEAD
# a1b2c3d4...

du -sh .vcs/
# XXX MB   (첫 커밋은 원본 전체 저장 — 예상 수치)
```

---

## Step 4. 파일 수정 (정점 하나 변경 시뮬레이션)

```bash
# 스크립트로 파일 내 특정 오프셋 1바이트 수정
python3 -c "
with open('character.fbx', 'r+b') as f:
    f.seek(1024)
    f.write(b'\x42')
print('수정 완료: 오프셋 1024에 1바이트 변경')
"
```

**확인**:

```bash
ls -lh character.fbx    # 파일 크기는 동일함을 보여줌
```

---

## Step 5. `dgit commit` — 두 번째 커밋 (delta 저장)

```bash
dgit commit -m "손가락 정점 미세 조정"
```

**예상 출력**:

```
커밋 완료: 1개 파일 / 커밋 ID: e5f6g7h8
```

**확인**:

```bash
ls .vcs/objects/deltas/
# e5f6g7h8[커밋ID전체64자리].delta

du -sh .vcs/objects/deltas/
# ~120 KB
```

---

## Step 6. `dgit log` — 커밋 히스토리 출력

```bash
dgit log
```

**예상 출력**:

```
commit  e5f6g7h8e5f6g7h8...
Date:   2026-06-12T14:32:01Z
        손가락 정점 미세 조정

commit  a1b2c3d4a1b2c3d4...
Date:   2026-06-12T14:30:45Z
        캐릭터 초기 버전
```

---

## Step 7. `dgit checkout` — 첫 번째 버전으로 복원

```bash
# 현재 파일을 별도 저장 (비교용)
cp character.fbx character_modified.fbx

# dgit log 출력에서 "캐릭터 초기 버전"의 커밋 ID 64자리를 복사해서 사용
dgit checkout <Step 6 log에서 복사한 v1 전체 커밋ID>
```

**예상 출력**:

```
SHA256 검증 통과
체크아웃 완료: a1b2c3d4
```

**확인**:

```bash
# 복원된 파일과 원본이 완전히 일치하는지 바이트 단위 비교
sha256sum character.fbx
# 원본의 sha256 값과 동일해야 함

cmp character.fbx character_modified.fbx && echo "동일" || echo "다름"
# 다름 — 수정 전 버전으로 정확히 복원됨
```

---

## Step 8. `dgit diff` — 두 버전 간 변경 블록 비교

```bash
# dgit log에서 full 커밋 ID 두 개를 복사해서 사용
dgit diff <v1 전체 커밋ID 64자리> <v2 전체 커밋ID 64자리>
```

**예상 출력**:

```
변경된 블록 수: 8 / 총 변경 용량: 120.00 KB / 변경 비율: 0.12%
```

---

## 시연 종료 후 저장소 크기 비교

```bash
# designer_git 저장소 크기
du -sh .vcs/
# 예: ~100.1 MB  (베이스 100MB + delta ~120KB)

# Git LFS 대조군 (사전 측정값 슬라이드로 제시)
# 동일 조건 2회 커밋 기준: 600 MB
```

| 비교군              | 2회 커밋 후 저장소 크기 | 절감률              |
| ------------------- | ----------------------- | ------------------- |
| Git LFS             | ~600 MB                 | —                   |
| designer_git        | ~100.1 MB               | ~50 % (베이스 공유) |
| 10회 커밋 시 (예측) | ~101 MB vs ~1,000 MB    | ~90 %               |

---
