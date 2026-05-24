# 실험 3. Git LFS vs designer-git 누적 저장소 비교

## 1. 실험 목적

본 실험은 **비압축 TIFF 텍스처 파일을 10회 반복 수정·커밋했을 때 Git LFS와 designer-git(dgit)의 누적 저장소 크기가 어떻게 달라지는지** 비교하기 위한 실험이다.

3D/VFX 및 게임 제작 파이프라인에서는 텍스처 파일이 수십~수백 MB 단위로 커질 수 있으며, 실제 후반 작업에서는 전체 파일을 새로 만드는 것이 아니라 얼굴, 의상, 장갑, 갑옷 등 특정 부위만 조금씩 수정하는 경우가 많다. 이때 Git LFS는 각 버전의 바이너리 파일을 LFS 객체로 저장하는 반면, dgit은 CDC 기반 delta 저장을 통해 변경분 중심 저장을 목표로 한다.

따라서 본 실험에서는 같은 텍스처 파일을 `v1`부터 `v10`까지 조금씩 수정하고, 이를 Git LFS와 dgit에 각각 10회 커밋하여 누적 저장소 크기를 비교했다.

---

## 2. 실험 전 파일 분석

### 2.1 사용 모델

실험에는 3D 모델링 에셋 사이트 FAB에서 다운로드한 `fantasy-knight-girl` 모델을 사용했다. 해당 모델은 하이폴리곤 캐릭터 에셋이며, 대용량 텍스처와 복잡한 UV 구조를 포함하고 있어 dgit의 delta 저장 효율을 확인하기에 적합하다고 판단했다.

![fantasy-knight-girl의 텍스처](images/image-1.png)

### 2.2 텍스처 구성

`fantasy-knight-girl` 모델의 텍스처는 크게 Hair, Knight, Mouth, Teeth 네 가지 텍스처 세트로 나뉘어 있었다. Substance 3D Painter에서 텍스처 작업을 수행했을 때 이 텍스처 세트들이 하나로 합쳐지지 않았으므로, 본 실험에서는 주요 캐릭터 텍스처인 **Knight 텍스처 세트만 사용**했다.

| 텍스처 세트 | 설명 |
|---|---|
| Hair | 머리카락 텍스처 |
| Knight | 갑옷, 의상, 피부 등 주요 캐릭터 텍스처 |
| Mouth | 입 내부 텍스처 |
| Teeth | 치아 텍스처 |

### 2.3 3D 텍스처 작업의 변수

일반적인 2D 이미지 편집에서는 화면에서 수정한 영역과 실제 파일 내부 변경 위치가 비교적 직관적으로 대응된다. 그러나 3D 텍스처 작업에서는 3D 표면이 UV 좌표를 통해 2D 텍스처 이미지에 펼쳐져 저장된다. 따라서 사용자가 3D 모델에서 특정 부위만 수정했다고 보더라도, 실제 TIFF 파일 내부에서는 여러 위치가 함께 변경될 수 있다.

본 실험에서도 다음과 같은 변수가 관찰되었다.

- 3D 모델에서 한 부위만 수정해도 UV 연결로 인해 다른 부위까지 함께 변경될 수 있다.
- 대칭 UV 또는 공유 UV 때문에 한쪽 수정이 반대쪽에도 반영될 수 있다.
- Substance 3D Painter의 projection, padding, dilation, anti-aliasing 등의 영향으로 주변 픽셀도 함께 변경될 수 있다.
- 결과적으로 시각적으로는 작은 수정이라도 파일 내부에서는 여러 블록에 분산되어 저장될 수 있다.

예를 들어 v1에서 v2로 넘어갈 때 얼굴에 블러셔를 추가했지만, UV 연결로 인해 눈 주변 텍스처 등 여러 위치가 함께 수정되었다. 따라서 실험 결과를 해석할 때는 **시각적 수정 면적**과 **파일 내부 변경 분포**를 구분해야 한다.

---

## 3. 실험 과정

### 3.1 버전별 수정 내용

원본 텍스처를 `v1`로 두고, 이후 얼굴, 옷, 장갑, 신발, 다리 갑옷 등 여러 부위를 조금씩 수정하여 `v10`까지 생성했다.

| 버전 | 수정 내용 | 이미지 |
|---|---|---|
| v1 | 원본 텍스처 | ![v1 원본](images/image-2.png) |
| v2 | 얼굴 블러셔 수정 | ![v2 얼굴 수정](images/image-3.png) |
| v3 | 옷 부위 수정 | ![v3 옷 수정](images/image-4.png) |
| v4 | 장갑 국소 수정 | ![v4 장갑 수정](images/image-5.png) |
| v5 | 다리 국소 수정 | ![v5 다리 수정](images/image-6.png) |
| v6 | 얼굴 코끝 블러셔 수정 | ![v6 얼굴 수정](images/image-7.png) |
| v7 | 어깨 갑옷 국소 수정 | ![v7 어깨 갑옷 수정](images/image-8.png) |
| v8 | 장갑 국소 수정 | ![v8 장갑 수정](images/image-9.png) |
| v9 | 부츠 국소 수정 | ![v9 부츠 수정](images/image-10.png) |
| v10 | 다리 갑옷 부위 국소 수정 | ![v10 다리 갑옷 수정](images/image-11.png) |

v3의 경우 3D 모델에서는 옷의 두 군데가 수정된 것처럼 보였지만, 실제로는 UV 좌표 및 대칭 구조 때문에 텍스처의 한 영역이 여러 3D 표면에 반영된 결과였다.

### 3.2 커밋 방식

각 버전은 모두 동일한 파일명 `texture.tiff`로 덮어쓴 뒤 커밋했다.

```text
v1 → texture.tiff → commit
v2 → texture.tiff → commit
...
v10 → texture.tiff → commit
```

이 방식은 Git LFS와 dgit 모두에서 “동일한 파일이 10번 수정되어 커밋되는 상황”을 재현하기 위한 것이다.

---

## 4. 실험 위치 및 조건

| 항목 | 내용 |
|---|---|
| 실험 위치 | `~/designer_git/benchmark` |
| Git LFS 실험 위치 | `~/designer_git/benchmark/exp3_git_lfs` |
| dgit 실험 위치 | `~/designer_git/benchmark/exp3_dgit` |
| 실행 환경 | Ubuntu WSL2 홈 디렉터리 |
| 금지 조건 | `/mnt/c` 경로 사용 금지 |
| 입력 파일 | `tex_v1_uncompressed.tif` ~ `tex_v10_uncompressed.tif` |
| 파일 형식 | 비압축 TIFF |
| 작업 파일명 | `texture.tiff` |
| 측정 대상 | 누적 저장소 크기, 저장 방식, commit 시간 |

`/mnt/c` 경로는 WSL2에서 I/O 병목이 커 측정값이 왜곡될 수 있으므로 사용하지 않았다. 실험 파일은 Ubuntu 홈 디렉터리 아래에서 측정했다.

---

## 5. Git LFS 10회 커밋 결과

### 5.1 원본 CSV

```csv
commit_no,commit_id,lfs_bytes,git_bytes,work_file_bytes,commit_sec
1,85a941042bd0c095cc2912aca1432497ecb266c6,100688118,100716946,100688118,0.06
2,398c5472d1f44921014a67ca4106d51f4b33932d,201376236,201405728,100688118,0.09
3,d01910d5a0e4d48a98a66814061e2618d567af8d,302064354,302094507,100688118,1.67
4,201a6cad207f7722175e02fb3ae273ea694ef29e,402752472,402783287,100688118,0.05
5,57d2008167eb6f627fea9ae6a72381deed637349,503440590,503472067,100688118,0.05
6,39688e415acd5dca86af848bb24a8757c6405f64,604128708,604160847,100688118,0.10
7,a2c7d30a8635c9d4dd03b2a1822ad07dbf0b7b0b,704816826,704849632,100688118,0.15
8,01b989b0b11fe3a2f4716ff392e43c4a3d9605a7,805504944,805538416,100688118,0.08
9,2e3ef50fc58a2a39e3f8ce5fb391c1d38a474bd2,906193062,906227197,100688118,0.12
10,e71c05c40329ae2aee7b7398a9ebd49c7eed3b9f,1006881180,1006915983,100688118,0.16
```

### 5.2 정리 표

| commit | commit_id | `.git/lfs` bytes | `.git/lfs` MB | `.git` bytes | `.git` MB | 작업 파일 MB | commit sec |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | `85a9410` | 100,688,118 | 100.69 | 100,716,946 | 100.72 | 100.69 | 0.06 |
| 2 | `398c547` | 201,376,236 | 201.38 | 201,405,728 | 201.41 | 100.69 | 0.09 |
| 3 | `d01910d` | 302,064,354 | 302.06 | 302,094,507 | 302.09 | 100.69 | 1.67 |
| 4 | `201a6ca` | 402,752,472 | 402.75 | 402,783,287 | 402.78 | 100.69 | 0.05 |
| 5 | `57d2008` | 503,440,590 | 503.44 | 503,472,067 | 503.47 | 100.69 | 0.05 |
| 6 | `39688e4` | 604,128,708 | 604.13 | 604,160,847 | 604.16 | 100.69 | 0.10 |
| 7 | `a2c7d30` | 704,816,826 | 704.82 | 704,849,632 | 704.85 | 100.69 | 0.15 |
| 8 | `01b989b` | 805,504,944 | 805.50 | 805,538,416 | 805.54 | 100.69 | 0.08 |
| 9 | `2e3ef50` | 906,193,062 | 906.19 | 906,227,197 | 906.23 | 100.69 | 0.12 |
| 10 | `e71c05c` | 1,006,881,180 | 1006.88 | 1,006,915,983 | 1006.92 | 100.69 | 0.16 |

### 5.3 Git LFS 객체 확인

아래 명령어로 Git LFS 객체 파일을 확인했다.

```bash
find .git/lfs -type f -printf '%s %p\n' | sort -nr | head -20
```

확인 결과, **100,688,118 bytes** 크기의 LFS 객체가 총 10개 생성되었다.

```text
100688118 .git/lfs/objects/dd/28/dd28af6e96a7ed04f13eaba4503686817456da6ba37200b098848d38d5ddf3f9
100688118 .git/lfs/objects/cd/c5/cdc5ce15337eed8aede0aeeeac12f3ad9da4ae716992820273fa6ae7bfa4c5c5
100688118 .git/lfs/objects/c8/62/c862a2979ef469a6ee75b4f624605e9e08a4ddfc986f386637e8618d79abb655
100688118 .git/lfs/objects/bd/d9/bdd9a8cfe86a1c8020d3345b5a088762cedb885d629b9bde026cb2a29b6600e7
100688118 .git/lfs/objects/84/c6/84c686b479e3cb1b196fca87c69caf796772ef11cc60161e26e7cf9395c7df86
100688118 .git/lfs/objects/81/d6/81d6a69b8f5dd93e2481aa8a5add7e7e8770598e05324dc2aae6b94aec5f4665
100688118 .git/lfs/objects/67/bf/67bf97a310f00a75f17afcccda24c93dfabc51b3b5ccc79f6245784ecfd8c14e
100688118 .git/lfs/objects/56/73/5673729cba862796083485169ecb7054892ddc803016e76fa2976a50d3094160
100688118 .git/lfs/objects/45/2f/452f434d6cb7b5ee43463fb60dceb8baa2e49fdc7cfc61cfedc407e8a20379f9
100688118 .git/lfs/objects/21/43/2143037e52240e2245222438884e346303031bc334a83f9e0231bd9aa6f1b17
```

즉, Git LFS는 `texture.tiff`가 변경될 때마다 이전 버전과의 delta를 저장하지 않고, 각 버전의 TIFF 파일 전체를 LFS 객체로 저장했다.

---

## 6. dgit 결과와 Git LFS 비교

| 방식 | 기준 | 최종 크기 | MB 환산 | Git LFS 대비 절감률 |
|---|---|---:|---:|---:|
| Git LFS | `.git/lfs` | 1,006,881,180 bytes | 1006.88 MB | 0.00% |
| dgit | base + delta payload | 167,746,891 bytes | 167.75 MB | 83.34% |
| dgit | `.vcs` 전체, snapshot 포함 | 268,439,395 bytes | 268.44 MB | 73.34% |

Git LFS는 10회 커밋 후 `.git/lfs` 크기가 원본 TIFF 10개를 그대로 누적한 값과 동일하게 증가했다. 반면 dgit은 base와 delta를 중심으로 저장하여 순수 payload 기준 약 **83.34%**, snapshot 포함 실제 저장소 크기 기준 약 **73.34%**의 저장 공간 절감을 보였다.

---

## 7. dgit snapshot 생성 분석

dgit의 `.vcs` 전체 크기를 해석할 때는 delta payload와 snapshot을 구분해야 한다. 이번 실험에서 dgit은 10번째 커밋 시점에 복원 성능을 위한 snapshot 파일을 추가로 생성했다.

`.vcs` 내부 큰 파일을 확인한 결과는 다음과 같았다.

```text
100688118 .vcs/objects/snapshots/4167b0a32f545d11.snap
100688118 .vcs/objects/base/texture.tiff
24765666 .vcs/objects/deltas/8f8933aa55969d4f.delta
9515322 .vcs/objects/deltas/4cd651749dcb43a8.delta
6807720 .vcs/objects/deltas/ac27674126c69c74.delta
6425474 .vcs/objects/deltas/6bf60b78ae9c3c85.delta
5632015 .vcs/objects/deltas/9f329faa6bb8b8bd.delta
4757810 .vcs/objects/deltas/73b219124372e99f.delta
4134526 .vcs/objects/deltas/cbc59784eeb3484a.delta
3222394 .vcs/objects/deltas/4167b0a32f545d11.delta
1797846 .vcs/objects/deltas/dbe4b9405db665fa.delta
```

10번째 커밋에서 `.vcs` 크기가 약 100MB 증가한 것은 delta 생성 실패나 fullcopy 전환 때문이 아니라, dgit의 snapshot 정책 때문에 `4167b0a32f545d11.snap` 파일이 추가로 저장되었기 때문이다.

| 항목 | 크기 | 의미 |
|---|---:|---|
| base 파일 | 100,688,118 bytes | 최초 기준 파일 저장 |
| v10 delta | 3,222,394 bytes | 10번째 커밋의 실제 delta |
| v10 snapshot | 100,688,118 bytes | 10번째 커밋에서 생성된 복원용 snapshot |

dgit은 2~10회차를 delta로 저장했지만, 10회 커밋마다 snapshot을 하나 생성하는 정책 때문에 10번째 커밋에서 원본 크기와 동일한 약 100.69MB의 snapshot이 추가되었다. 따라서 dgit의 실제 저장소 크기는 순수 delta payload보다 커진다. 그러나 snapshot을 포함하더라도 Git LFS의 `.git/lfs` 크기 1,006.88MB보다 훨씬 작다.

이 snapshot은 저장 공간만 보면 추가 비용이지만, checkout 또는 복원 시 모든 delta를 처음부터 순차 적용하지 않도록 하는 중간 기준점 역할을 한다. 즉, dgit은 저장 공간 절감과 복원 성능 사이에서 절충을 수행하며, 이번 실험의 `.vcs` 전체 크기에는 이 복원 성능 최적화 비용이 포함되어 있다.

---

## 8. 핵심 요약

- 원본 TIFF 1개 크기: **100,688,118 bytes ≈ 100.69 MB**
- 원본 10개를 통째로 저장할 경우: **1,006,881,180 bytes ≈ 1006.88 MB**
- Git LFS 최종 `.git/lfs` 크기: **1,006,881,180 bytes ≈ 1006.88 MB**
- Git 전체 `.git` 최종 크기: **1,006,915,983 bytes ≈ 1006.92 MB**
- dgit base + delta payload 크기: **167,746,891 bytes ≈ 167.75 MB**
- dgit snapshot 포함 `.vcs` 전체 크기: **268,439,395 bytes ≈ 268.44 MB**
- dgit 순수 payload 기준 Git LFS 대비 절감률: **83.34%**
- dgit snapshot 포함 실제 저장소 기준 Git LFS 대비 절감률: **73.34%**

---

## 9. 결론

Git LFS는 10회 커밋 후 `.git/lfs` 디렉터리 크기가 **1,006.88 MB**로 증가했다. 이는 원본 TIFF 1개 크기인 **100.69 MB**가 10개 누적된 값과 정확히 일치한다. 또한 `.git/lfs/objects` 내부에 **100,688,118 bytes** 크기의 객체가 10개 생성된 것이 확인되었다.

반면 dgit은 동일한 10회 커밋에서 base + delta payload 기준 약 **167.75 MB**, snapshot을 포함한 실제 `.vcs` 전체 기준 약 **268.44 MB**를 사용했다. 특히 dgit은 10회 커밋마다 snapshot을 생성하는 정책을 사용하므로, 10번째 커밋에서 약 **100.69 MB**의 snapshot 파일이 추가되었다. 그럼에도 dgit은 Git LFS 대비 순수 payload 기준 약 **83.34%**, snapshot을 포함한 실제 저장소 크기 기준 약 **73.34%**의 저장 공간 절감을 보였다.

이 결과는 비압축 TIFF처럼 내부의 일부 영역만 수정되는 대용량 바이너리 파일에 대해, Git LFS는 버전별 전체 파일을 누적 저장하는 반면 dgit은 변경분 중심 저장으로 누적 저장소 증가량을 크게 줄일 수 있음을 보여준다. 다만 dgit은 일정 커밋 간격마다 snapshot을 추가로 저장하므로, 최종 저장소 크기를 해석할 때는 순수 delta payload와 snapshot 포함 실제 디스크 사용량을 구분해야 한다.
