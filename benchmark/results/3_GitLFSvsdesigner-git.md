# 실험 3 Git LFS 10회 커밋 결과 정리

## 1. 실험 위치 및 조건

- 실험 위치: `~/designer_git/benchmark/exp3_git_lfs`
- 실행 환경: Ubuntu WSL2 홈 디렉터리 기준
- 금지 조건: `/mnt/c` 경로 사용하지 않음
- 입력 파일: `tex_v1_uncompressed.tif` ~ `tex_v10_uncompressed.tif`
- 커밋 방식: 매 회차마다 `texture.tiff`를 해당 버전으로 덮어쓴 뒤 Git LFS commit
- 측정 대상:
  - `.git/lfs` 누적 크기
  - `.git` 전체 크기
  - 작업 파일 크기
  - commit 시간

## 2. 원본 CSV

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

## 3. 정리 표

| commit | commit_id | lfs_bytes | lfs_MB | git_bytes | git_MB | work_file_MB | commit_sec |
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

## 4. Git LFS 객체 확인 결과

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

즉, Git LFS는 `texture.tiff`가 변경될 때마다 이전 버전과의 delta를 저장하지 않고, 각 버전의 TIFF 파일 전체를 LFS 객체로 저장한 것으로 확인된다.

## 5. 핵심 요약

- 원본 TIFF 1개 크기: **100,688,118 bytes ≈ 100.69 MB**
- 원본 10개를 통째로 저장할 경우: **1,006,881,180 bytes ≈ 1006.88 MB**
- Git LFS 최종 `.git/lfs` 크기: **1,006,881,180 bytes ≈ 1006.88 MB**
- Git 전체 `.git` 최종 크기: **1,006,915,983 bytes ≈ 1006.92 MB**
- 총 commit 시간: **2.53초**
- 평균 commit 시간: **0.25초**

## 6. Git LFS 누적 저장 특성

```text
TIFF 1개 크기 × 10회 커밋
= 100,688,118 × 10
= 1,006,881,180 bytes
```

실측된 Git LFS 최종 `.git/lfs` 크기는 다음과 같다.

```text
Git LFS 최종 .git/lfs 크기
= 1,006,881,180 bytes
```

따라서 이번 실험에서는 다음 관계가 성립한다.

```text
Git LFS 최종 .git/lfs 크기 = 원본 TIFF 10개 전체 크기
```

즉, Git LFS는 10회 커밋 동안 각 버전의 비압축 TIFF를 거의 그대로 누적 저장했다.

## 7. dgit 결과와 비교

이전에 수행한 dgit 결과와 비교하면 다음과 같다.

| 방식 | 기준 | 최종 크기 | MB 환산 | Git LFS 대비 절감률 |
|---|---|---:|---:|---:|
| Git LFS | `.git/lfs` | 1,006,881,180 bytes | 1006.88 MB | 0.00% |
| dgit | base + delta payload | 167,746,891 bytes | 167.75 MB | 83.34% |
| dgit | `.vcs` 전체, snapshot 포함 | 268,439,395 bytes | 268.44 MB | 73.34% |

dgit은 10번째 커밋에서 복원 성능을 위한 snapshot 파일을 생성했기 때문에 `.vcs` 전체 기준 크기는 payload 기준보다 커졌다. 그러나 snapshot을 포함한 실제 저장소 크기 기준으로도 Git LFS 대비 약 **73.34%** 더 적은 공간을 사용했다.

## 8. dgit snapshot 생성에 대한 추가 분석

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

즉, 10번째 커밋에서 `.vcs` 크기가 약 100MB 증가한 것은 delta 생성 실패나 fullcopy 전환 때문이 아니라, dgit의 snapshot 정책 때문에 `4167b0a32f545d11.snap` 파일이 추가로 저장되었기 때문이다.

| 항목 | 크기 | 의미 |
|---|---:|---|
| base 파일 | 100,688,118 bytes | 최초 기준 파일 저장 |
| v10 delta | 3,222,394 bytes | 10번째 커밋의 실제 delta |
| v10 snapshot | 100,688,118 bytes | 10번째 커밋에서 생성된 복원용 snapshot |

따라서 dgit 결과는 두 가지 기준으로 나누어 해석해야 한다.

| 기준 | 크기 | Git LFS 대비 절감률 | 해석 |
|---|---:|---:|---|
| base + delta payload | 167,746,891 bytes, 약 167.75 MB | 83.34% | 순수 delta 저장 효율 |
| `.vcs` 전체, snapshot 포함 | 268,439,395 bytes, 약 268.44 MB | 73.34% | 실제 디스크 사용량 |

이번 실험에서 dgit은 2~10회차를 delta로 저장했지만, 10회 커밋마다 snapshot을 하나 생성하는 정책 때문에 10번째 커밋에서 원본 크기와 동일한 약 100.69MB의 snapshot이 추가되었다. 따라서 dgit의 실제 저장소 크기는 순수 delta payload보다 커지지만, snapshot을 포함하더라도 Git LFS의 `.git/lfs` 크기 1,006.88MB보다 훨씬 작다.

이 snapshot은 저장 공간만 보면 추가 비용이지만, checkout 또는 복원 시 모든 delta를 처음부터 순차 적용하지 않도록 하는 중간 기준점 역할을 한다. 즉 dgit은 저장 공간 절감과 복원 성능 사이에서 절충을 수행하며, 이번 실험의 `.vcs` 전체 크기에는 이 복원 성능 최적화 비용이 포함되어 있다.

## 9. 보고서용 결론 초안

Git LFS는 10회 커밋 후 `.git/lfs` 디렉터리 크기가 **1,006.88 MB**로 증가했다. 이는 원본 TIFF 1개 크기인 **100.69 MB**가 10개 누적된 값과 정확히 일치한다. 또한 `.git/lfs/objects` 내부에 **100,688,118 bytes** 크기의 객체가 10개 생성된 것이 확인되었다.

반면 dgit은 동일한 10회 커밋에서 base + delta payload 기준 약 **167.75 MB**, snapshot을 포함한 실제 `.vcs` 전체 기준 약 **268.44 MB**를 사용했다. 특히 dgit은 10회 커밋마다 snapshot을 생성하는 정책을 사용하므로, 10번째 커밋에서 약 **100.69 MB**의 snapshot 파일이 추가되었다. 따라서 dgit은 Git LFS 대비 순수 payload 기준 약 **83.34%**, snapshot을 포함한 실제 저장소 크기 기준 약 **73.34%**의 저장 공간 절감을 보였다.

이 결과는 비압축 TIFF처럼 내부의 일부 영역만 수정되는 대용량 바이너리 파일에 대해, Git LFS는 버전별 전체 파일을 누적 저장하는 반면 dgit은 변경분 중심 저장으로 누적 저장소 증가량을 크게 줄일 수 있음을 보여준다. 다만 dgit은 일정 커밋 간격마다 snapshot을 추가로 저장하므로, 최종 저장소 크기를 해석할 때는 순수 delta payload와 snapshot 포함 실제 디스크 사용량을 구분해야 한다.
