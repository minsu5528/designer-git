# 실험 5: FBX 포맷 delta 효율 분석 (Maya vs Blender)

## 1. 실험 개요

본 실험은 게임/VFX 업계 표준 포맷인 FBX가 파이프라인 단계별 수정에서 delta 효율이 어떻게 나타나는지를 Maya와 Blender 두 DCC 툴로 비교 분석한다. 동일한 Fantasy Knight Girl 캐릭터 에셋을 사용하여 동일한 9가지 수정 케이스를 각 툴에서 수행하고, dgit의 변경 비율을 측정했다.

## 2. 실험 환경

| 항목 | 내용 |
|---|---|
| 캐릭터 에셋 | Fantasy Knight Girl (리깅 완료, 158개 본) |
| Maya FBX | 바이너리 압축, Autodesk FBX SDK |
| Blender FBX | 바이너리 비압축, Python 자체 구현 |
| Blender 설정 | 스페이스 트랜스폼: OFF, 트랜스폼 적용: OFF |
| 파일 크기 | Maya ~30.5MB, Blender ~29MB |
| 비교 기준 | 각 케이스를 v1_base와 독립 비교 |

## 3. 케이스 정의

| case | 파이프라인 단계 | 수정 내용 |
|---|---|---|
| `v2_modeling_small` | 모델링 | 버텍스 소량 편집 |
| `v3_modeling_large` | 모델링 | 버텍스 대량 편집 |
| `v4_rigging_move` | 리깅 | 본 위치 이동 (Pose Mode) |
| `v5_rigging_rotate` | 리깅 | 본 회전 (Pose Mode) |
| `v6_anim_add_key` | 애니메이션 | 키프레임 추가 |
| `v7_anim_edit_key` | 애니메이션 | 키프레임 수정 |
| `v8_scene_translate` | 씬 어셈블리 | 오브젝트 위치 이동 |
| `v9_scene_rotate` | 씬 어셈블리 | 오브젝트 회전 |
| `v10_material` | 재질 변경 | Base Color 수정 |

## 4. 측정 결과

| case | 파이프라인 단계 | Maya FBX | Blender FBX |
|---|---|---:|---:|
| `v2_modeling_small` | 모델링 소량 | 100.00% | 100.00% |
| `v3_modeling_large` | 모델링 대량 | 100.00% | 100.00% |
| `v4_rigging_move` | 리깅 이동 | 100.00% | **0.41%** ✅ |
| `v5_rigging_rotate` | 리깅 회전 | 100.00% | **0.41%** ✅ |
| `v6_anim_add_key` | 애니메이션 추가 | 100.00% | 100.00% |
| `v7_anim_edit_key` | 애니메이션 수정 | 100.00% | 100.00% |
| `v8_scene_translate` | 씬 어셈블리 이동 | 100.00% | 37.20% |
| `v9_scene_rotate` | 씬 어셈블리 회전 | 100.00% | 30.48% |
| `v10_material` | 재질 변경 | 100.00% | **0.35%** ✅ |

## 5. 원인 분석

### 5.1 Maya FBX — 전 케이스 100%

Maya는 Autodesk 공식 FBX SDK를 사용하며, SDK가 노드 식별자로 런타임 메모리 주소를 사용한다. 이 주소는 매 export마다 달라지므로 수정 내용과 무관하게 파일 전체가 변경된다. 이는 SDK 설계상 비활성화 불가능한 구조적 특성이다.

```
Maya FBX 노드 ID 예시:
Geometry: 2437310735680  ← 1회차 export
Geometry: 2438901234560  ← 2회차 export (동일 씬, 다른 ID)
```

### 5.2 Blender FBX — 케이스에 따라 다름

Blender는 Python으로 자체 구현한 FBX exporter를 사용하며, 결정론적(deterministic) ID를 사용한다. 동일 씬을 두 번 export하면 ~171바이트(타임스탬프)만 다른 거의 동일한 파일이 생성된다.

**delta 효율이 낮은 케이스 (100%):**
- 버텍스 편집: Blender가 변형된 버텍스를 FBX에 직접 저장 → 전체 변경
- 애니메이션 포함: 158개 본 전체 키프레임 베이크 → 전체 변경

**delta 효율이 높은 케이스:**
- 본 회전/이동 (0.41%): Pose Mode 로컬 transform만 변경, 버텍스 유지
- 재질 변경 (0.35%): 색상값 몇 바이트만 변경
- 씬 이동/회전 (30~37%): 바인드 행렬(절대좌표) 갱신으로 중간 수준

### 5.3 씬 어셈블리가 30~37%인 이유

```
Object Mode에서 아마츄어 이동/회전
→ 아마츄어 월드 좌표 변경
→ 158개 본의 바인드 행렬(절대좌표) 전부 갱신
→ 30~37%

반면 Pose Mode 본 회전(v4, v5)
→ 로컬 포즈만 변경
→ 바인드 행렬 유지
→ 0.41%
```

## 6. 결론

| 관점 | 결론 |
|---|---|
| Maya FBX | SDK 수준의 volatile ID로 모든 케이스 100%, delta 불가 |
| Blender FBX | 결정론적 구현으로 리깅·재질 변경 delta 가능 (0.35~0.41%) |
| FBX 포맷 자체 | 동일 포맷이라도 DCC 툴 구현에 따라 delta 효율이 크게 달라짐 |

designer-git은 Blender FBX의 리깅 작업(본 회전/이동)과 재질 변경에서 delta 효율을 발휘하며, Maya FBX는 fullcopy로 처리한다.

## 7. 보고서용 요약 문장

실험 5에서는 동일한 FBX 포맷에 대해 Maya와 Blender 두 DCC 툴의 delta 효율을 9가지 파이프라인 케이스로 비교했다. Maya FBX는 Autodesk SDK의 런타임 메모리 주소 기반 ID로 인해 모든 케이스에서 100% 변경률이 나타났다. 반면 Blender FBX는 Python 자체 구현의 결정론적 특성으로, 본 회전(0.41%)과 재질 변경(0.35%)에서 높은 delta 효율을 보였다. 씬 어셈블리 이동/회전은 바인드 행렬 갱신으로 30~37% 수준이었으며, 버텍스 편집과 애니메이션은 100%였다. 이 결과는 FBX 포맷의 delta 효율이 포맷 자체보다 DCC 툴의 구현 방식에 크게 의존함을 보여준다.
