# designer_git

> 3D·VFX 파이프라인 후반 작업에서 CDC delta 저장 전략의 효율성을 검증하는 오픈소스 엔진 (공개SW프로젝트 2026)

## 프로젝트 소개

`designer_git`은 CDC(Content-Defined Chunking) 기반 Rolling Hash delta 엔진을 3D·VFX 바이너리 파일에 적용해, 후반 파이프라인 단계(텍스처링 · 리깅 · 애니메이션 · 씬 어셈블리)에서 delta 저장이 실제로 얼마나 효과적인지를 측정하고 검증하는 오픈소스 프로젝트입니다.

초기 스컬핑/모델링 단계는 Vertex Buffer가 전체 재생성되므로 delta 효율이 낮습니다. 이후 후반 단계는 In-place 픽셀 수정, 키프레임 값 변경, Transform 수정 등 국소적 변경이 주를 이루므로 delta 효율이 높을 것으로 예측하며, 이를 실측 벤치마크로 검증합니다.

| 파이프라인 단계 | delta 효율 | 이유                              |
| --------------- | ---------- | --------------------------------- |
| 스컬핑/모델링   | X          | Vertex Buffer 전체 재생성         |
| 텍스처링        | O          | 픽셀 In-place 수정                |
| 리깅            | O          | 본 데이터만 추가                  |
| 애니메이션      | O          | 키프레임 값만 변경                |
| 씬 어셈블리     | O          | Transform 값만 변경               |
| VFX 캐시        | O          | 프레임 Insert (Rolling Hash 효과) |

## 기술 스택

- C++ (Delta 엔진, VCS 로직, CLI)
- Python (벤치마크 스크립트)

## 핵심 기술

- **Rolling Hash (Rabin-Karp CDC)**: 콘텐츠 기반 블록 경계 탐지로 1바이트 삽입에도 전체 블록 밀림 방지
- **COPY/INSERT 직렬화**: 변경 블록만 INSERT, 나머지는 17바이트 포인터(COPY)로 저장
- **압축 포맷 자동 분기**: 매직 넘버 기반 파일 헤더 판별 → jpg/png는 fullcopy, exr/fbx는 delta
- **Early-exit 샘플링**: 앞부분만 스캔해 INSERT 비율 > 50% 이면 즉시 fullcopy 전환
- **스냅샷 체크포인트**: 10커밋마다 전체 파일 저장 → 최악 체크아웃 = delta 9개 적용

## 검증 목표 수치

| 측정 항목                                | 목표          |
| ---------------------------------------- | ------------- |
| 텍스처링 절감률 (TIFF 비압축, 소량 수정) | **90% 이상**  |
| 리깅 절감률 (본 추가)                    | **90% 이상**  |
| 애니메이션 절감률 (키프레임 수정)        | **90% 이상**  |
| 씬 어셈블리 절감률 (Transform 수정)      | **90% 이상**  |
| Git LFS 대비 누적 저장소 절감률          | **90% 이상**  |
| commit 시간 (1GB)                        | **15초 이내** |
| checkout 시간 (delta 체인 9개)           | **30초 이내** |
| Early-exit 판단 시간 (1GB)               | **3초 이내**  |
| SHA256 검증 통과율                       | **100%**      |
| Maya vs Blender delta 효율 차이          | 실측 후 기재  |
| 수정량 임계치 (fullcopy 전환 기준)       | 실측 후 기재  |

## CLI 명령어

```bash
dgit init                    # 저장소 초기화
dgit add <file>              # 파일 추적 등록
dgit add ./assets/           # .fbx 파일 일괄 등록
dgit commit -m "메시지"       # 커밋 (delta 추출 → .vcs 저장)
dgit log                     # 커밋 히스토리
dgit checkout <commit-id>    # 특정 버전 복원
dgit diff <id1> <id2>        # 변경 블록 수 / 용량 / 비율 출력
```

## 저장소 구조

```
.vcs/
├── objects/
│   ├── base/        # 최초 버전 원본
│   ├── deltas/      # 버전 간 delta 파일 (zstd 압축)
│   └── snapshots/   # 10커밋 주기 스냅샷
├── commits/         # 커밋 메타데이터 JSON
├── index            # 추적 중인 파일 목록
└── HEAD             # 현재 커밋 ID
```

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

## 빌드

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

의존성: `libzstd-dev`, `nlohmann-json3-dev`

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

## 벤치마크 실행

> **WSL 사용자 필수**: 반드시 WSL 네이티브 경로(`~/...`)에서 실행  
> `/mnt/c/` 경로에서 실행하면 I/O 병목으로 측정값이 **10~13배 왜곡**됩니다.

### 1. 프로젝트를 WSL 홈으로 복사

```bash
cp -r /mnt/c/.../designer_git ~/designer_git
```

### 2. WSL에서 빌드

```bash
cd ~/designer_git
mkdir -p build && cd build
cmake .. && make -j4
```

### 3. 벤치마크 실행 (`sudo` 필요 — 파일 시스템 캐시 플러시용)

```bash
cd ~/designer_git
sudo python3 benchmark/measure.py --dgit ./build/dgit --scenarios 2,3,6,7,8
```

## 팀원

| 역할                               | 담당   |
| ---------------------------------- | ------ |
| 역할 1 — Rolling Hash + Delta 엔진 | 김민수 |
| 역할 2 — Delta 역적용 + VCS 로직   | 김채연 |
| 역할 3 — CLI + 벤치마크            | 강민경 |

## 라이선스

MIT License
