# VCS Design Document

## .vcs 폴더 구조

digit init 실행 시 아래 구조가 생성된다.

```
.vcs/
├── objects/
│ ├── base/ # 최초 원본 파일(SHA256해시 파일명)
│ └── deltas/ # 커밋 간 delta 파일 ({commit_id}.delta)
├── commits/ # 커밋 메타데이터 JSON ({commit_id}.json)
├── index # 추적 중인 파일 경로 목록 (텍스트, 줄 구분)
└── HEAD # 현재 커밋 ID (텍스트 한 줄, 초기값 "")
```

## 커밋 JSON 스키마

파일 경로: .vcs/commits/{commit_id}.json

```
{
"id": "a3f9c1...", // SHA256 (메시지+타임스탬프+parent_id 해싱)
"message": "Update rig",
"timestamp": "2025-04-10T14:32:00Z",
"parent_id": "", // 최초 커밋은 빈 문자열
"files": [
{
"path": "character.fbx",
"delta": "objects/deltas/a3f9c1.delta",
"is_base": false // 최초 커밋이면 true, base/ 경로 사용
}
]
}
```
