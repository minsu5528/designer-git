# designer_git

> 3D·VFX 파이프라인 후반 작업에서 CDC delta 저장 전략의 효율성을 검증하는 오픈소스 엔진 (공개SW프로젝트 2026)

## 프로젝트 소개

`designer_git`은 CDC(Content-Defined Chunking) 기반 Rolling Hash delta 엔진을 3D·VFX 바이너리 파일에 적용해, 후반 파이프라인 단계에서 delta 저장이 실제로 얼마나 효과적인지를 측정하고 검증하는 오픈소스 프로젝트입니다.

초기 스컬핑/모델링 단계는 Vertex Buffer가 전체 재생성되므로 delta 효율이 낮습니다. 이후 후반 단계는 In-place 픽셀 수정, 키프레임 값 변경, Transform 수정 등 국소적 변경이 주를 이루므로 delta 효율이 높을 것으로 예측하며, 이를 실측 벤치마크로 검증합니다.

비압축 TIFF 텍스처 파일 10회 반복 커밋 실험에서 Git LFS는 1,006.88MB로 선형 증가한 반면, dgit은 순수 delta payload 기준 167.75MB(83.34% 절감), snapshot 포함 실제 저장소 기준 268.44MB(73.34% 절감)를 달성하였습니다.

**파이프라인 단계별 실측 현황**

| 파이프라인 단계 | 판정 | 근거 수치 | 비고 |
|---|---|---|---|
| 스컬핑/모델링 | X | 변경률 100% | FBX 재직렬화 → fullcopy 처리 |
| 텍스처링 — 극소량 수정 | O | caseA delta, **99.90%** | 비압축 TIFF 실측 완료 |
| 텍스처링 — UV 소량 수정 | △ | caseB_uv delta, **62.68%** | UV island 분산으로 효율 감소 |
| 텍스처링 — 중간/전체 수정 | X | caseC/D fullcopy, **0.00%** | 변경 블록 80% 초과 → fullcopy 전환 |
| 씬 어셈블리 (Blender glTF .bin) | O | **99.9%** | 실험 1 케이스 B 완료 |
| 애니메이션 키프레임 (Blender glTF .bin) | O | **99.8%** | 실험 1 케이스 C 완료 |
| 리깅 (Blender FBX, Pose Mode 본 이동/회전) | O | **0.41%** 변경률 | 로컬 transform만 변경, 버텍스 유지 |
| 재질 변경 (Blender FBX) | O | **0.35%** 변경률 | 색상값 몇 바이트만 변경 |
| 씬 어셈블리 (Blender FBX) | △ | **30~37%** 변경률 | 바인드 행렬 갱신으로 중간 수준 |
| Maya FBX (전 케이스) | X | 변경률 100% | exporter 비결정적 직렬화 → fullcopy 처리 |

## 기술 스택

- C++ (Delta 엔진, VCS 로직, CLI)
- Python (벤치마크 스크립트)

## 핵심 기술

- **Rolling Hash (Rabin-Karp CDC)**: 콘텐츠 기반 블록 경계 탐지로 1바이트 삽입에도 전체 블록 밀림 방지 (평균 16KB, MIN 4KB, MAX 64KB, 슬라이딩 윈도우 64바이트)
- **COPY/INSERT 직렬화**: 변경 블록만 INSERT, 나머지는 17바이트 포인터(COPY)로 저장
- **압축 포맷 자동 분기**: magic bytes 기반 파일 헤더 판별 → jpg/png/zip/mp4는 fullcopy, exr/fbx/tiff는 delta
- **Early-exit 샘플링**: 변경 블록 비율 > 80%이면 즉시 fullcopy 전환
- **스냅샷 체크포인트**: 10커밋마다 전체 파일 저장 → 최악 체크아웃 = delta 9개 적용
- **Producer-Consumer 멀티스레딩**: 디스크 읽기와 Rolling Hash 연산 병렬화로 commit 성능 3배 개선

## 검증 목표 수치 및 실측 결과

| 측정 항목 | 목표 | 실측값 | 달성 |
|---|---|---|---|
| 텍스처링 절감률 (TIFF 비압축, 극소량 수정) | 90% 이상 | **99.90%** | O |
| 텍스처링 절감률 (UV 소량 수정) | 90% 이상 | **62.68%** | X (UV 분산 영향) |
| 텍스처링 절감률 (중간/전체 수정) | — | **0.00%** | fullcopy 전환 |
| 씬 어셈블리 절감률 (Blender glTF .bin) | 90% 이상 | **99.9%** | O |
| 애니메이션 키프레임 절감률 (Blender glTF .bin) | 90% 이상 | **99.8%** | O |
| Git LFS 대비 누적 절감률 (payload 기준) | 90% 이상 | **83.34%** | △ |
| Git LFS 대비 누적 절감률 (.vcs 전체 기준) | 90% 이상 | **73.34%** | X (snapshot 포함) |
| fullcopy 전환 기준 (수정량 임계치) | 실측 후 기재 | **변경 블록 비율 80%** | O |
| commit 시간 (1GB, WSL2 네이티브) | 15초 이내 | **14초** | O |
| checkout 시간 (1GB, WSL2 네이티브) | 30초 이내 | **2.8초** | O |
| SHA256 검증 통과율 | 100% | **100%** | O |

## CLI 명령어

```bash
dgit init                    # 저장소 초기화
dgit add <file>              # 파일 추적 등록
dgit add ./assets/           # 바이너리 파일 일괄 등록
dgit commit -m "메시지"      # 커밋 (delta 추출 → .vcs 저장)
dgit log                     # 커밋 히스토리
dgit checkout <commit-id>    # 특정 버전 복원
dgit diff <id1> <id2>        # 변경 블록 수 / 용량 / 비율 출력
```

### 저장소 삭제

```bash
rm -rf .vcs
```

`.vcs` 디렉토리를 삭제하면 저장소와 모든 버전 이력이 제거됩니다.

## 저장소 구조

```
.vcs/
├── objects/
│   ├── base/        # 최초 버전 원본
│   ├── deltas/      # 버전 간 delta 파일 (COPY/INSERT 직렬화)
│   └── snapshots/   # 10커밋 주기 스냅샷
├── commits/         # 커밋 메타데이터 JSON
├── index            # 추적 중인 파일 목록
└── HEAD             # 현재 커밋 ID
```

## 설치 방법

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install cmake g++ libssl-dev nlohmann-json3-dev
```

### macOS (Homebrew)

```bash
brew install cmake openssl nlohmann-json
```

## 빌드

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

빌드 완료 후 `build/dgit` 실행 파일이 생성됩니다.

```bash
# PATH에 추가 (선택)
export PATH="$PATH:$(pwd)/build"
```

### 빌드 옵션

| 옵션 | 기본값 | 설명 |
|---|---|---|
| `DGIT_STRICT_FBX_ONLY` | `0` | `1`로 설정 시 명시 파일도 `.fbx`만 허용. 기본값 `0`은 `.bin` 등 바이너리 허용 (자동화 테스트 호환) |

```bash
cmake .. -DDGIT_STRICT_FBX_ONLY=1
```

## 사용법

### `dgit init` — 저장소 초기화

```bash
dgit init
```

성공 시 출력 없음. 이미 초기화된 경우:

```
오류: dgit 저장소가 이미 있거나 초기화할 수 없습니다
```

### `dgit add` — 파일 추적 등록

```bash
dgit add character.fbx
dgit add ./assets/
```

### `dgit commit` — 커밋

```bash
dgit commit -m "캐릭터 초기 버전"
dgit commit -m "손가락 정점 수정" character.fbx
dgit commit -m "에셋 업데이트" ./assets/
```

첫 번째 커밋은 파일 전체를 `.vcs/objects/base/`에 저장합니다. 이후 커밋은 변경 블록만 delta로 저장합니다. 변경 블록 비율이 80% 이상이거나 jpg·png 등 압축 포맷이면 자동으로 전체 저장으로 전환합니다.

### `dgit log` — 커밋 히스토리

```bash
dgit log
```

### `dgit checkout` — 특정 버전 복원

```bash
dgit checkout a1b2c3d4
```

checkout은 내부적으로 가장 가까운 스냅샷을 찾아 delta를 순서대로 적용합니다. 적용 후 SHA256으로 복원 무결성을 자동 검증합니다.

### `dgit diff` — 두 버전 비교

```bash
dgit diff <commit_id_1> <commit_id_2>
```

```
변경된 블록 수: 8 / 총 변경 용량: 120.00KB / 변경 비율: 0.12%
```

## 동작 원리 요약

```
dgit commit 실행
     │
     ├─ 압축 포맷? (jpg·png·zip·mp4 등 magic bytes 판별)
     │     └─ YES → 전체 파일 저장
     │
     ├─ 변경 블록 비율 > 80%?  (앞·중간·뒤 샘플링)
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

```bash
# 1. 프로젝트를 WSL 홈으로 복사
cp -r /mnt/c/.../designer_git ~/designer_git

# 2. WSL에서 빌드
cd ~/designer_git
mkdir -p build && cd build
cmake .. && make -j4

# 3. 벤치마크 실행 (sudo 필요 — 파일 시스템 캐시 플러시용)
cd ~/designer_git
sudo python3 benchmark/measure.py --dgit ./build/dgit --scenarios 2,3,6,7,8
```

## 한계 및 향후 과제

- **포맷 의존성**: Maya FBX처럼 exporter가 매 export마다 전체 재직렬화하는 경우 delta 효율이 낮아 fullcopy 처리 필요
- **압축 포맷**: PNG, JPG, ZIP 등 이미 압축된 포맷은 delta 효율이 낮음
- **단일 파일 중심**: 현재 로컬 CLI 도구이며 폴더 단위 프로젝트 전체 관리는 향후 과제
- **향후 과제**: 원격 저장소 연동, GUI 클라이언트, 스냅샷 주기 자동 결정(early-exit sampling), USD/USDA 파이프라인 추가 실험

## 팀원

| 역할 | 담당 |
|---|---|
| Rolling Hash + Delta 엔진 (delta.cpp) | 김민수 |
| VCS 로직 + SHA-256 검증 (vcs.cpp) | 김채연 |
| CLI + 벤치마크 (main.cpp) | 강민경 |

## 라이선스

MIT License
