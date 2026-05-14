# designer_git

> 3D·바이너리 파일 전용 오픈소스 버전관리 엔진 (공개SW프로젝트 2026)

## 프로젝트 소개

`designer_git`은 게임·영상 업계 아티스트를 위한 오픈소스 버전관리 도구입니다.

Git LFS는 바이너리 파일을 매 커밋마다 전체 재저장합니다. 정점 하나만 수정해도 10GB 파일 전체가 저장소에 누적됩니다. `designer_git`은 CDC(Content-Defined Chunking) 기반 Rolling Hash로 변경된 블록만 추출해 저장합니다.

```
정점 1개 수정 → 변경 블록만 INSERT, 나머지는 COPY 참조
→ 10GB 파일 기준 delta ~10MB (Git LFS 대비 ~99% 절감)
```

## 핵심 목표

- 10GB 파일 수정 시 수 MB의 Delta만 저장
- `dgit commit`, `dgit checkout` 등 Git과 동일한 CLI 인터페이스
- 오픈소스 (Perforce의 기능을 무료로)

## 기술 스택

- C++ (Delta 엔진, VCS 로직, CLI)
- Python (벤치마크 스크립트)

### 핵심 기술

- **Rabin-Karp Rolling Hash + CDC**: 파일 내용 기반 경계 탐지로 1바이트 삽입 시에도 블록 밀림 없음
- **이중 해시 블록 검증**: `h1 × h2` 조합으로 충돌 확률 ~10⁻¹⁸
- **Producer-Consumer 멀티스레딩**: Thread 1 디스크 읽기 / Thread 2 Rolling Hash 연산 병렬화
- **Double Buffer**: 4MB 버퍼 2개로 I/O와 연산 겹치기
- **압축 포맷 자동 분기**: jpg·png·zip 등은 전체 저장, fbx·exr·tiff 등 비압축 포맷은 delta 추출
- **스냅샷 체인**: 10커밋마다 자동 스냅샷 생성, checkout 시 최대 delta 9개만 적용

## 팀원

| 역할                                      | 담당   |
| ----------------------------------------- | ------ |
| 역할 1 — Rolling Hash + Delta 엔진        | 김민수 |
| 역할 2 — Delta 역적용 + VCS 로직 + 테스트 | 김채연 |
| 역할 3 — CLI + 벤치마크                   | 강민경 |

## 프로젝트 구조

    designer-git/
    ├── src/
    │   ├── engine/    # Rolling Hash + Delta 엔진
    │   ├── vcs/       # 커밋 시스템 + 저장소 관리
    │   └── cli/       # CLI 명령어
    ├── tests/         # 단위 테스트
    ├── benchmark/     # 벤치마크 스크립트 + 결과
    └── docs/          # 문서 및 발표자료

## 의존성

| 항목    | 버전       | 용도                                      |
| ------- | ---------- | ----------------------------------------- |
| CMake   | 3.15 이상  | 빌드 시스템                               |
| g++     | C++17 지원 | 컴파일러                                  |
| OpenSSL | -          | SHA256 해시 (커밋 ID 생성, checkout 검증) |

## 설치 방법

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install cmake g++ libssl-dev
```

### macOS (Homebrew)

```bash
brew install cmake openssl
```

### 빌드

```bash
git clone https://github.com/minsu5528/designer_git
cd designer_git
mkdir build && cd build
cmake ..
make
```

빌드 완료 후 `build/dgit` 실행 파일이 생성됩니다.

```bash
# PATH에 추가 (선택)
export PATH="$PATH:$(pwd)/build"
```

### 빌드 옵션

| 옵션                   | 기본값 | 설명                                                                                               |
| ---------------------- | ------ | -------------------------------------------------------------------------------------------------- |
| `DGIT_STRICT_FBX_ONLY` | `0`    | `1`로 설정 시 명시 파일도 `.fbx`만 허용. 기본값 `0`은 `.bin` 등 바이너리 허용 (자동화 테스트 호환) |

```bash
cmake .. -DDGIT_STRICT_FBX_ONLY=1
```

## 사용법

### `dgit init` — 저장소 초기화

현재 폴더에 `.vcs` 저장소를 생성합니다.

```bash
dgit init
```

성공 시 출력 없음. 이미 초기화된 경우:

```
오류: dgit 저장소가 이미 있거나 초기화할 수 없습니다
```

### `dgit add` — 파일 추적 등록

단일 파일 또는 폴더를 추적 목록에 추가합니다. 폴더 입력 시 내부 `.fbx` 파일만 재귀 수집합니다.

```bash
# 단일 파일
dgit add character.fbx

# 폴더 (내부 .fbx 전체)
dgit add ./assets/
```

```
추가 완료: 1개 파일
추가 완료: 3개 파일 / 이미 추적 중: 1개 파일
```

### `dgit commit` — 커밋

추적 중인 파일의 현재 상태를 커밋합니다.

```bash
# 추적 파일 전체 커밋
dgit commit -m "캐릭터 초기 버전"

# 특정 파일만 커밋
dgit commit -m "손가락 정점 수정" character.fbx

# 특정 폴더 하위 파일만 커밋
dgit commit -m "에셋 업데이트" ./assets/
```

```
커밋 완료: 1개 파일 / 커밋 ID: a1b2c3d4
```

첫 번째 커밋은 파일 전체를 `.vcs/objects/base/`에 저장합니다. 이후 커밋은 변경 블록만 delta로 저장합니다. jpg·png 등 압축 포맷이나 변경률이 80% 이상인 경우 자동으로 전체 저장으로 전환합니다.

### `dgit log` — 커밋 히스토리

HEAD에서 시작해 부모 커밋을 역방향으로 출력합니다.

```bash
dgit log
```

```
commit  e5f6g7h8e5f6g7h8e5f6g7h8e5f6g7h8e5f6g7h8e5f6g7h8e5f6g7h8e5f6g7h8
Date:   2026-06-12T14:32:01Z
        손가락 정점 미세 조정

commit  a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4
Date:   2026-06-12T14:30:45Z
        캐릭터 초기 버전
```

### `dgit checkout` — 특정 버전 복원

지정한 커밋 시점으로 파일을 복원합니다. **커밋 ID는 64자리 전체**를 입력해야 합니다. `dgit log`에서 복사해 사용하세요.

```bash
dgit checkout a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4
```

```
SHA256 검증 통과
체크아웃 완료: a1b2c3d4
```

checkout은 내부적으로 가장 가까운 스냅샷을 찾아 delta를 순서대로 적용합니다. 적용 후 SHA256으로 복원 무결성을 자동 검증합니다.

### `dgit diff` — 두 버전 비교

두 커밋 간 변경 블록 수, 총 변경 용량, 변경 비율을 출력합니다. 커밋 ID는 64자리 전체 입력.

```bash
dgit diff <commit_id_1> <commit_id_2>
```

```
변경된 블록 수: 8 / 총 변경 용량: 120.00KB / 변경 비율: 0.12%
```

### `--help`

```bash
dgit --help
dgit commit --help
```

## 동작 원리 요약

```
dgit commit 실행
     │
     ├─ 압축 포맷? (jpg·png·zip 등)
     │     └─ YES → 전체 파일 저장
     │
     ├─ 변경률 > 80%?  (앞·중간·뒤 샘플링)
     │     └─ YES → 전체 파일 저장
     │
     └─ NO → CDC Rolling Hash delta 생성
                  │
                  ├─ COPY: 오프셋 8B + 길이 8B = 17바이트
                  └─ INSERT: 실제 변경 데이터
```

```
dgit checkout 실행
     │
     ├─ 가장 가까운 스냅샷 탐색
     │     (10커밋마다 자동 생성)
     │
     ├─ 스냅샷 → delta 순서대로 적용 (최대 9개)
     │
     └─ SHA256 검증 → 파일 복원 완료
```

## 테스트 실행

```bash
cd build && cmake .. && make
cd ../tests
python3 test_pipeline.py
```

테스트 항목: 기본 라운드트립, SHA256 검증, delta 체인 3개 이상 복원, 빈 파일, 손상된 메타데이터, 전체 파일 교체, 다양한 파일 크기 등 13개 시나리오.

## 라이선스

MIT License
