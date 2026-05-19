# 벤치마크 실험 설계 문서

## 1. 실험 목적

designer_git의 핵심 가설을 수치로 검증한다.

> _"CDC 기반 delta 추출을 적용하면, 바이너리 파일을 반복 커밋할 때  
> Git LFS 대비 저장소 증가량을 90% 이상 절감할 수 있다."_

최종 발표에서 제시할 **스토리지 절감률 / commit 시간 / checkout 시간** 세 가지 지표를 객관적으로 측정하고, 그 결과를 CSV와 그래프로 산출한다.

---

## 2. 변수 정의

### 2.1 독립변수 (Independent Variables)

| 변수          | 수준                         | 설명                                                 |
| ------------- | ---------------------------- | ---------------------------------------------------- |
| **파일 형식** | `.fbx` (비압축 Binary FBX)   | 정점 데이터가 연속 배열로 저장되어 delta 추출에 적합 |
|               | `.jpg` (압축 포맷)           | delta 비효율 케이스 — 전체 저장 fallback 검증용      |
|               | `/dev/urandom` 생성 바이너리 | 최악 케이스 (완전 랜덤, delta 압축 불가)             |
| **파일 크기** | 100 MB                       | 소규모                                               |
|               | 1 GB                         | 중간 규모                                            |
|               | 10 GB                        | 대규모 (목표 성능 지표 기준)                         |
| **수정량**    | 1 바이트 (단일 정점 수정)    | 최선 케이스                                          |
|               | 파일의 1 % 수정              | 소규모 수정                                          |
|               | 파일의 10 % 수정             | 중간 수정                                            |
|               | 파일 전체 교체               | 최악 케이스                                          |

### 2.2 종속변수 (Dependent Variables)

| 변수              | 단위   | 측정 방법                                                 |
| ----------------- | ------ | --------------------------------------------------------- |
| **저장소 증가량** | MB     | `du -sb .vcs/` 또는 `.git/lfs/objects/` 를 커밋 전후 차분 |
| **commit 시간**   | 초 (s) | `time dgit commit -m "..."` / `time git commit`           |
| **checkout 시간** | 초 (s) | `time dgit checkout <id>` / `time git checkout`           |

### 2.3 통제변수 (Controlled Variables)

- 동일 머신, 동일 OS 환경에서 실행
- 파일 시스템 캐시 플러시 (`sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'`) 후 측정
- 매 실험 전 저장소 초기화 (clean slate)
- 동일한 수정 스크립트 사용 (재현 가능성 확보)

---

## 3. 비교군 (Comparison Groups)

| 비교군                | 설명                                                               | 측정 대상                                      |
| --------------------- | ------------------------------------------------------------------ | ---------------------------------------------- |
| **A. 원본 전체 저장** | 커밋마다 파일을 그대로 복사해 누적 저장 (no delta, no compression) | 베이스라인. delta의 절대적 필요성 근거         |
| **B. Git LFS**        | `git lfs track "*.fbx"` — 현재 업계 표준 방식                      | 매 커밋 전체 파일 재업로드. 발표의 핵심 대조군 |
| **C. designer_git**   | CDC Rolling Hash + COPY/INSERT delta                               |

---

## 4. 실험 시나리오

### 시나리오 A — 소규모 반복 수정 (핵심 시나리오)

> 작업자가 매일 조금씩 파일을 수정하는 실제 작업 패턴을 모사한다.

```
초기 커밋 (v1) → 1 % 수정 후 커밋 (v2) → ... → 1 % 수정 후 커밋 (v10)
```

- 파일 형식: `.fbx`
- 파일 크기: 100 MB, 1 GB
- 수정량: 커밋당 파일의 1 %
- 커밋 횟수: 10회
- 측정 항목: 커밋별 저장소 증가량, 누적 저장소 크기

---

### 시나리오 B — 단일 바이트 수정 (이론치 검증)

> delta 효율 이론치(99.9 %)를 실측으로 검증한다.

```
초기 커밋 (v1) → 1 바이트 수정 후 커밋 (v2) → checkout v1 → 원본과 바이트 비교
```

- 파일 형식: `.fbx`
- 파일 크기: 100 MB, 1 GB, 10 GB
- 수정량: 1 바이트 (오프셋 지정)
- 측정 항목: delta 파일 크기, commit 시간, checkout 시간, SHA256 검증 통과 여부

---

### 시나리오 C — 대용량 파일 성능 (목표 지표 검증)

> "10 GB 파일 delta 연산 10초 이내" 목표 달성 여부를 확인한다.

```
10 GB 파일 초기 커밋 → 1 % 수정 → 재커밋 → checkout
```

- 파일 형식: `.fbx`, `/dev/urandom` 생성 바이너리
- 파일 크기: 10 GB
- 수정량: 1 바이트, 1 %, 10 %
- 측정 항목: commit 시간, checkout 시간, 피크 메모리 사용량
  (`/usr/bin/time -v` 또는 `valgrind --tool=massif`)

> 비고: delta.cpp에 Producer-Consumer 멀티스레딩(Thread 1: 4MB 버퍼 디스크 읽기, Thread 2: Rolling Hash 연산)이 현재 구현 완료된 상태로 측정한다.

---

### 시나리오 D — 압축 포맷 fallback 검증

> `.jpg` 등 압축 파일에서 전체 저장 분기가 정상 동작하는지 확인한다.

```
jpg 파일 초기 커밋 → 1 바이트 수정 → 재커밋 → .vcs/objects/ 구조 확인
```

- 파일 형식: `.jpg`
- 파일 크기: 100 MB
- 수정량: 1 바이트
- 측정 항목: delta 생성 여부 (delta 파일이 생성되지 않아야 함), 저장소 증가량

---

### 시나리오 E — delta 체인 복원 정확성 (통합 검증)

> delta 3개 이상 쌓은 후 checkout 시 바이트 단위 원본 일치를 확인한다.

```
v1 커밋 → v2 커밋 → v3 커밋 → checkout v1 → diff v1 원본
```

- 파일 형식: `.fbx`
- 파일 크기: 100 MB
- 수정량: 각 커밋마다 1 %
- 측정 항목: `diff` 또는 SHA256 일치 여부, checkout 시간

---

## 5. 반복 횟수 및 집계 방법

- 모든 시나리오에서 **3회 반복 측정**
- 집계: **3회 평균값** 사용 (이상값 존재 시 제외 후 명기)
- 표준편차를 함께 기록하여 편차가 큰 경우 원인 분석

```
측정값 = (run1 + run2 + run3) / 3
```

---

## 6. 측정 도구 및 환경

### 6.1 측정 도구

| 항목            | 도구                                               |
| --------------- | -------------------------------------------------- |
| 실행 시간       | `time` 명령어 (`real` 값 기준)                     |
| 메모리 사용량   | `/usr/bin/time -v` → `Maximum resident set size`   |
| 저장소 크기     | `du -sb <경로>`                                    |
| SHA256 검증     | `sha256sum`                                        |
| 바이트 비교     | `diff` / `cmp`                                     |
| 자동화 스크립트 | Python 3 (`subprocess`, `os`, `csv`, `matplotlib`) |

### 6.2 실험 환경

> ** WSL 사용자 필수**: 반드시 WSL 네이티브 경로(`~/...`)에서 실행하세요.  
> `/mnt/c/` 경로에서 실행하면 Windows NTFS ↔ WSL 파일시스템 변환 오버헤드로  
> I/O 병목이 발생해 측정값이 **10~13배 왜곡**됩니다.
>
> ```bash
> # 프로젝트를 WSL 홈으로 복사 (최초 1회)
> cp -r /mnt/c/.../designer_git ~/designer_git
>
> # WSL에서 빌드
> cd ~/designer_git
> mkdir -p build && cd build
> cmake .. && make -j4
>
> # 벤치마크 실행 (sudo 필요 — 파일 시스템 캐시 플러시용)
> cd ~/designer_git
> sudo python3 benchmark/measure.py --dgit ./build/dgit --scenarios 2,3,6,7,8
> ```

```
OS           : Ubuntu 22.04 LTS
CPU          : (실험 시 기재)
RAM          : (실험 시 기재)
Storage      : (SSD / HDD 및 읽기 속도 기재)
Compiler     : g++ -O2
Git version  : (Git LFS 대조군용)
```

## 7. 자동화 스크립트 구조

README 기준 프로젝트 구조 내 `benchmark/` 폴더에 위치한다.

```
designer-git/
├── src/
│   ├── engine/    # Rolling Hash + Delta 엔진
│   ├── vcs/       # 커밋 시스템 + 저장소 관리
│   └── cli/       # CLI 명령어
├── tests/         # 단위 테스트
├── benchmark/                        ← 현재 폴더
│   ├── run_all.sh                    # 전체 실험 일괄 실행 (WSL 네이티브 경로에서 실행할 것)
│   ├── setup.py                      # 테스트 파일 생성 (fbx 모의, 랜덤 바이너리)
│   ├── measure.py                    # 각 시나리오 실행 및 CSV 저장
│   ├── plot.py                       # CSV → 그래프 생성
│   ├── lfs-experiment.md             # Git LFS 실측 수치 기록
│   ├── results/
│   │   ├── scenario_a.csv
│   │   ├── scenario_b.csv
│   │   ├── scenario_c.csv
│   │   ├── scenario_d.csv
│   │   └── scenario_e.csv
│   └── graphs/
│       ├── storage_comparison.png    # 누적 저장소 크기 비교
│       ├── commit_time.png           # commit 시간 비교
│       └── checkout_time.png        # checkout 시간 비교
└── docs/          # 문서 및 발표자료

```

### CSV 출력 형식 예시 (`scenario_a.csv`)

```csv
scenario,group,file_type,file_size_mb,modify_ratio,commit_no,run,storage_delta_mb,delta_file_kb,commit_time_s,checkout_time_s
A,designer_git,fbx,100,0.01,1,1,0.12,120.0,0.43,0.21
A,designer_git,fbx,100,0.01,1,2,0.11,118.5,0.41,0.20
A,designer_git,fbx,100,0.01,1,3,0.13,121.2,0.44,0.22
A,git_lfs,fbx,100,0.01,1,1,100.00,,1.20,0.95
...
```

---

## 8. 목표 수치 및 판정 기준

| 지표                                          | 목표 수치      | 판정 기준                                 |
| --------------------------------------------- | -------------- | ----------------------------------------- |
| 스토리지 절감률 (vs Git LFS, 1 % 수정 × 10회) | **90 % 이상**  | `(LFS 누적 - dgit 누적) / LFS 누적 × 100` |
| commit 시간 (10 GB, 1 % 수정)                 | **10초 이내**  | `time` real 값                            |
| checkout 시간 (delta 체인 9개 이하)           | **30초 이내**  | `time` real 값                            |
| 피크 메모리 사용량 (10 GB 파일 처리 중)       | **50 MB 이하** | `/usr/bin/time -v` Maximum RSS            |
| SHA256 검증 통과율                            | **100 %**      | checkout 후 원본과 해시 일치              |

---

## 9. 예상 리스크 및 대응

| 리스크               | 내용                                                   | 대응 방안                                                                                                                                 |
| -------------------- | ------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| FBX 내부 재정렬      | 3D 툴 저장 시 메타데이터 재배치로 delta 효율 저하 가능 | 실측 후 효율 저하 원인 분석. 목표 미달 시 CDC 경계 조건 튜닝                                                                              |
| 10 GB 처리 시간 초과 | I/O 병목으로 10초 초과 가능                            | Producer-Consumer 멀티스레딩 이미 적용됨. 초과 시 mmap 기반 I/O 최적화 추가 검토                                                          |
| 메모리 초과          | 해시맵이 파일 크기에 비례 증가 (10GB 기준 ~36MB)       | IO 버퍼(8MB) + 해시맵(~36MB) 합산 기준 목표를 50MB로 조정. 추가 절감이 필요하면 해시맵 엔트리 크기 압축(BlockHash를 u128 1개로 통합) 검토 |
| 실측치 ≠ 이론치      | 99.9 % 절감은 단일 정점 수정 + 비압축 FBX 가정 기반    | 이론치와 실측치를 별도 명기. 차이 원인을 발표에서 솔직하게 제시                                                                           |

---
