# 벤치마크 실험 설계 문서

## 1. 실험 목적

designer_git의 핵심 가설을 수치로 검증한다.

> _"실제 3D·VFX 파이프라인 후반 작업에서 CDC delta 저장이 얼마나 효과적인가?  
> 그리고 어떤 조건에서 한계가 발생하는가?"_

초기 스컬핑/모델링 단계는 Vertex Buffer가 전체 재생성되므로 delta 효율이 낮다.  
텍스처링 이후 후반 단계(In-place 픽셀 수정, 키프레임 값 변경, Transform 수정)는 delta 효율이 높을 것으로 예측한다.  
**파이프라인 단계별 절감률 / commit 시간 / checkout 시간** 세 가지 지표를 객관적으로 측정하고, 그 결과를 CSV와 그래프로 산출한다

---

## 2. 실험 설계

### 독립변수 (Independent Variables)

| 변수            | 값                                                  |
| --------------- | --------------------------------------------------- |
| 파이프라인 단계 | 모델링 / 리깅 / 애니메이션 / 씬 어셈블리            |
| 사용 툴         | Maya / Blender                                      |
| 수정량          | 극소(1~2px) / 소량(~10%) / 중간(~50%) / 전체(~100%) |

### 종속변수 (Dependent Variables)

- 저장소 증가량 (KB/MB)
- delta 크기 (KB)
- delta 효율 (절감률 %)
- commit 시간 (초)
- checkout 시간 (초)
- SHA256 검증 통과 여부
- Early-exit 판단 시간 (초)

### 비교군

| 군             | 설명                  |
| -------------- | --------------------- |
| 원본 전체 저장 | fullcopy baseline     |
| Git LFS        | 현재 업계 표준 대조군 |
| designer-git   | 본 프로젝트           |

### 반복 횟수: 3회 평균

### 실험 파일 구성

하나의 캐릭터 프로젝트를 기반으로 모든 실험 진행

```
character_base.fbx      ← 기본 3D 메시 (모든 실험의 출발점)
character_rigged.fbx    ← 본 추가된 fbx
character_animated.fbx  ← 키프레임 추가된 fbx
character_scene.fbx     ← 씬에 배치된 fbx
texture_diffuse.tiff    ← 색상 텍스처 (비압축)
texture_normal.tiff     ← 법선 텍스처 (비압축)
```

---

## 3. 시나리오별 실험 계획

### 시나리오 1 — Maya vs Blender 직렬화 비교 (한계 조건 탐색)

| 항목      | 내용                                                                                                                    |
| --------- | ----------------------------------------------------------------------------------------------------------------------- |
| 대상      | `character_base.fbx` / 100MB (동일 모델, 두 툴)                                                                         |
| 핵심      | Maya(Autodesk 공식 FBX SDK) vs Blender(리버스 엔지니어링 구현)의 직렬화 차이                                            |
| 결과 활용 | Maya 효율 높음 → "공식 SDK 툴에서 완벽 동작" / 둘 다 낮음 → "폴리곤 편집 단계는 툴 무관하게 한계, 후반 파이프라인 타겟" |
| 담당      | 강민경                                                                                                                  |
| 스크립트  | 수동 실험 (Maya + Blender 실제 파일 필요)                                                                               |

**[케이스 A] 폴리곤 편집 (초기 모델링 단계)**

Maya 작업:

1. `File → Import → character_base.fbx` 열기
2. 우클릭 → Face 선택 모드
3. 폴리곤 하나 선택
4. W키 → 이동툴로 5~10 units 이동
5. `File → Export All → FBX` → `maya_poly_v2.fbx` 저장
   Blender 작업:
6. `File → Import → FBX → character_base.fbx` 열기
7. Tab키 → Edit Mode → 동일한 폴리곤 선택
8. G키 → 동일하게 이동
9. `File → Export → FBX` → `blender_poly_v2.fbx` 저장
   측정: `character_base.fbx → maya_poly_v2.fbx` delta 효율  
   　　　`character_base.fbx → blender_poly_v2.fbx` delta 효율

예상: Maya — 변경 폴리곤만 직렬화 → 효율 높을 수 있음  
　　　Blender — Vertex Buffer 전체 재배치 → 효율 낮음

**[케이스 B] 씬 어셈블리 (후반 파이프라인)**

Maya 작업:

1. `File → Import → character_base.fbx` 열기
2. 오브젝트 선택 → W키 → X축으로 100 units 이동
3. `File → Export All → FBX` → `maya_scene_v2.fbx` 저장
   Blender 작업:
4. `File → Import → FBX → character_base.fbx` 열기
5. G키 → X키 → 100 입력 → Enter
6. `File → Export → FBX` → `blender_scene_v2.fbx` 저장
   예상: Transform 값만 변경 → 두 툴 모두 효율 높을 가능성  
   　　　→ "폴리곤 편집만 비효율, 후반 작업은 툴 무관" 결론 가능

**[케이스 C] 애니메이션 키프레임 수정 (후반 파이프라인)**

Maya 작업:

1. `File → Import → character_base.fbx` 열기
2. `Window → Animation Editors → Graph Editor` 열기
3. 30프레임 → 특정 Joint 선택 → 회전값 45° → 60° 수정
4. `File → Export All → FBX` → `maya_anim_v2.fbx` 저장
   Blender 작업:
5. `File → Import → FBX → character_base.fbx` 열기
6. 30프레임으로 이동 → R키 → 동일하게 회전값 수정 → I키 → 키프레임 삽입
7. `File → Export → FBX` → `blender_anim_v2.fbx` 저장
   예상: AnimationCurve 데이터만 변경 → 두 툴 모두 효율 높을 가능성

---

### 시나리오 2 — 파이프라인 단계별 delta 효율 (핵심 증명)

| 항목     | 내용                                                               |
| -------- | ------------------------------------------------------------------ |
| 대상     | 각 단계별 파일 / Maya 실제 작업                                    |
| 목적     | 후반 파이프라인 각 단계에서 실제 현업 툴로 작업 후 delta 효율 실측 |
| 예상     | 텍스처링·리깅·애니메이션·씬 어셈블리 모두 90%+ 절감                |
| 담당     | 강민경                                                             |
| 스크립트 | `scenario2_pipeline_efficiency.py`                                 |

**[2-1] 텍스처링 — Substance Painter**

> 비압축 TIFF 출력 필수: `Export Textures → Compression: None`  
> 기본값이 압축일 수 있으니 반드시 확인 (압축 선택 시 delta 효율 0%로 하락)

작업: 캐릭터 특정 부위(옷 일부 등) 색상 수정 후 TIFF 비압축으로 내보내기  
측정: `texture_v1.tiff → texture_v2.tiff` delta 효율  
예상: 수정 픽셀 영역만 변경 → **효율 90%+**

**[2-2] 리깅 — Maya**

작업:

1. `File → Import → character_base.fbx` 열기
2. `Skeleton → Create Joints` → 허리/어깨/팔꿈치 Joint 3~4개 추가
3. `File → Export All → FBX` → `character_rigged.fbx` 저장
   측정: `character_base.fbx → character_rigged.fbx` delta 효율  
   예상: 수백MB 메시 데이터 유지, 본 데이터(수KB)만 추가 → **효율 90%+**

**[2-3] 애니메이션 — Maya**

작업:

1. `File → Import → character_rigged.fbx` 열기
2. `Graph Editor` → 30프레임 → 어깨 Joint 회전값 45° → 60° 수정
3. `File → Export All → FBX` → `character_animated.fbx` 저장
   측정: `character_rigged.fbx → character_animated.fbx` delta 효율  
   예상: AnimationCurve 숫자값만 수정, 메시/본 구조 유지 → **효율 90%+**

**[2-4] 씬 어셈블리 — Maya**

작업:

1. `File → Import → character_animated.fbx` 열기
2. 캐릭터 오브젝트 선택 → W키 → X축 100 units 이동, R키 → Y축 45° 회전
3. `File → Export All → FBX` → `character_scene.fbx` 저장
   측정: `character_animated.fbx → character_scene.fbx` delta 효율  
   예상: Transform 값(위치/회전) 몇 바이트만 변경 → **효율 90%+**

---

### 시나리오 3 — Git LFS vs designer-git 누적 저장소 비교 (임팩트 시각화)

| 항목     | 내용                                                                         |
| -------- | ---------------------------------------------------------------------------- |
| 대상     | `texture.tiff` (비압축) / 반복 커밋 10회                                     |
| 목적     | 반복 커밋 시 누적 저장소 크기 차이를 수치와 그래프로 시각화                  |
| 예상     | Git LFS: 100MB × 10 = ~1GB / designer-git: 100MB + delta 수십KB × 9 ≈ ~100MB |
| 담당     | 강민경                                                                       |
| 스크립트 | `scenario3_storage_comparison.py`                                            |

작업: Substance Painter에서 텍스처를 부위별로 조금씩 수정해 TIFF 비압축으로 10회 내보내기

designer-git 측정:

```bash
dgit add texture.tiff
dgit commit -m "v1"  # → dgit commit -m "v10"
# 커밋마다 .vcs/ 크기 기록
```

Git LFS 측정:

```bash
git lfs track "*.tiff" && git add texture.tiff
git commit -m "v1"   # → git commit -m "v10"
# 커밋마다 .git/lfs/ 크기 기록
```

측정: 커밋 횟수 vs 누적 저장소 크기 (로그 스케일 그래프)

---

### 시나리오 4 — 수정량별 delta 효율 임계치 탐색 (경계 조건)

| 항목     | 내용                                                      |
| -------- | --------------------------------------------------------- |
| 대상     | `texture_v1.tiff` (100MB, 비압축)                         |
| 목적     | 수정량이 얼마까지 증가해도 delta가 효율적인지 임계치 실측 |
| 담당     | 강민경                                                    |
| 스크립트 | `scenario4_threshold.py`                                  |

| 케이스 | 수정 범위    | 작업                               | 예상 절감률               |
| ------ | ------------ | ---------------------------------- | ------------------------- |
| A      | 극소 (1~2px) | 브러시 1px, 눈 부위 점 하나        | **99%+**                  |
| B      | 소량 (~10%)  | 브러시 중간, 얼굴 전체             | **90%+**                  |
| C      | 중간 (~50%)  | 브러시 크게, 상반신 전체 색상 변경 | 절감률 감소 (임계치 근처) |
| D      | 전체 (~100%) | 전체 레이어 색상/재질 전면 교체    | fullcopy 전환 확인        |

측정: 수정 면적 비율 vs delta 크기 꺾은선 그래프 → 효율 임계치 도출

---

### 시나리오 5 — Early-exit 판단 시간 측정

| 항목     | 내용                            |
| -------- | ------------------------------- |
| 대상     | `.fbx`, `.exr` / 100MB, 1GB     |
| 작업     | 80%+ 변경 (Early-exit 트리거)   |
| 예상     | 조기 종료 후 fullcopy 전환      |
| **목표** | **1GB 기준 판단 시간 3초 이내** |
| 측정     | Early-exit 판단 소요 시간       |
| 스크립트 | `scenario5_early_exit.py`       |

---

### 시나리오 6 — 압축 포맷 fullcopy fallback 검증

| 항목     | 내용                                           |
| -------- | ---------------------------------------------- |
| 대상     | `.jpg`, `.png` / 100MB (대조군: `.exr`)        |
| 작업     | 1바이트 수정                                   |
| 예상     | jpg/png: fullcopy 분기 동작 (delta 생성 안 됨) |
| 측정     | delta 생성 여부, 저장소 증가량                 |
| 스크립트 | `scenario6_compressed_fallback.py`             |

---

### 시나리오 7 — 1바이트 수정 이론치 검증

| 항목     | 내용                                     |
| -------- | ---------------------------------------- |
| 대상     | `.exr`, `.fbx` (Maya) / 100MB, 1GB       |
| 작업     | 1바이트 수정                             |
| 예상     | delta 수 KB → **절감률 99.9%+**          |
| 측정     | delta 크기, commit/checkout 시간, SHA256 |
| 스크립트 | `scenario7_single_byte.py`               |

---

## 4. 시각화 계획

1. **파이프라인 단계별 delta 효율 막대 그래프** (시나리오 2)  
   텍스처링 / 리깅 / 애니메이션 / 씬 어셈블리 각 단계의 절감률
2. **Git LFS vs designer-git 누적 저장소 크기 그래프** (시나리오 3)  
   x축: 커밋 횟수, y축: 누적 저장소 크기(MB), 로그 스케일
3. **Maya vs Blender delta 효율 비교 표** (시나리오 1)  
   폴리곤 편집 / 씬 어셈블리 / 애니메이션 3가지 케이스 비교
4. **수정량 vs delta 효율 꺾은선 그래프** (시나리오 4)  
   수정 면적 비율에 따른 임계치 시각화
5. **파이프라인 단계별 적용 가능성 요약 표**
   | 단계 | delta 효율 | 이유 |
   | ------------- | ---------- | --------------------------------- |
   | 스컬핑/모델링 | X | Vertex Buffer 전체 재생성 |
   | 텍스처링 | O | 픽셀 In-place 수정 (비압축 TIFF 기준) |
   | 리깅 | O | 본 데이터만 추가 (Maya 기준) |
   | 애니메이션 | O | 키프레임 값만 변경 |
   | 씬 어셈블리 | O | Transform 값만 변경 |
   | VFX 캐시 | O | 프레임 Insert (Rolling Hash 효과) |

---

## 5. 목표 수치

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

---

## 6. 실험 환경

| 항목     | 사양                                      |
| -------- | ----------------------------------------- |
| OS       | Ubuntu 22.04 LTS (WSL2)                   |
| CPU      | Intel Core i5-1155G7 @ 2.50GHz (11th Gen) |
| RAM      | 8GB                                       |
| Storage  | Samsung NVMe SSD 256GB                    |
| Compiler | g++ -O2                                   |
| Python   | 3.10+                                     |

> **WSL 사용자 필수**: 반드시 WSL 네이티브 경로(`~/...`)에서 실행.  
> `/mnt/c/` 경로에서 실행하면 I/O 병목으로 측정값이 **10~13배 왜곡**됩니다.

---

## 7. 스크립트 실행 방법

```bash
# 1. 프로젝트 WSL 홈으로 복사
cp -r /mnt/c/.../designer_git ~/designer_git

# 2. WSL에서 빌드
cd ~/designer_git
mkdir -p build && cd build
cmake .. && make -j4

# 3. 벤치마크 실행 (sudo 필요 — 파일 시스템 캐시 플러시용)
cd ~/designer_git
sudo python3 benchmark/measure.py --dgit ./build/dgit --scenarios 2,3,6,7,8
```

---

## 8. 스크립트 파일 구조

```
benchmark/
├── utils.py                          # 공통 유틸 (파일 생성, dgit 래퍼, CSV 저장)
├── scenario2_pipeline_efficiency.py  # 파이프라인 단계별 delta 효율
├── scenario3_storage_comparison.py   # LFS vs designer-git 누적 저장소 비교
├── scenario4_threshold.py            # 수정량별 임계치 탐색
├── scenario5_early_exit.py           # Early-exit 판단 시간
├── scenario6_compressed_fallback.py  # 압축 포맷 fullcopy 분기
├── scenario7_single_byte.py          # 1바이트 수정 이론치
├── measure.py                        # 일괄 실행 (--dgit, --scenarios 옵션)
└── results/
    ├── scenario2_pipeline_efficiency.csv
    ├── scenario3_storage_comparison.csv
    ├── scenario4_threshold.csv
    ├── scenario5_early_exit.csv
    ├── scenario6_compressed_fallback.csv
    └── scenario7_single_byte.csv
```
