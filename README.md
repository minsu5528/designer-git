# designer_git
3D·바이너리 파일 전용 오픈소스 버전관리 엔진 (공개SW프로젝트 2026)

# designer_git
> 3D·바이너리 파일 전용 오픈소스 버전관리 엔진

## 프로젝트 소개

Git과 Git LFS는 텍스트 코드에 최적화되어 있어, 10GB짜리 3D 파일(.fbx)에서
정점 하나만 수정해도 파일 전체를 다시 업로드해야 합니다.

designer-git은 Rolling Hash 기반 Delta 압축을 통해 변경된 바이트 조각만
추출·저장하는 바이너리 전용 버전관리 시스템입니다.

## 핵심 목표

- 10GB 파일 수정 시 수 MB의 Delta만 저장
- `dgit commit`, `dgit checkout` 등 Git과 동일한 CLI 인터페이스
- 오픈소스 (Perforce의 기능을 무료로)

## 기술 스택

- C++ (Delta 엔진, VCS 로직, CLI)
- Python (벤치마크 스크립트)
- zstd (Delta 압축)

## 팀원

| 역할 | 담당 |
|------|------|
| 역할 1 — Rolling Hash + Delta 엔진 | 김민수 |
| 역할 2 — Delta 역적용 + VCS 로직 | 김채연 |
| 역할 3 — CLI + 벤치마크 | 강민경 |
| 역할 4 — 테스트 + 발표 자료 | 윤현우 |


## 프로젝트 구조
    designer-git/
    ├── src/
    │   ├── engine/    # Rolling Hash + Delta 엔진
    │   ├── vcs/       # 커밋 시스템 + 저장소 관리
    │   └── cli/       # CLI 명령어
    ├── tests/         # 단위 테스트
    ├── benchmark/     # 벤치마크 스크립트 + 결과
    └── docs/          # 문서 및 발표자료

## 빌드 방법

> 추후 업데이트 예정

## 라이선스

MIT License
