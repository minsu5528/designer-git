# dgit CLI 명령어 인터페이스 정의서

## 1. 문서 목적

이 문서는 `dgit` 명령어의 사용자 입력 형식, 인자 파싱 방식, 출력 메시지, 에러 메시지 규칙을 정의한다.

이 문서는 CLI 인터페이스만 다루며, 저장소 내부 구조 생성, 커밋 메타데이터 저장, delta 생성 및 복원 알고리즘의 내부 구현 방식은 다루지 않는다.

## 2. 기본 실행 형식

```bash
dgit <command> [options] [arguments]
```

예시는 다음과 같다.

```bash
dgit init
dgit add model.fbx
dgit commit -m "update vertex position"
dgit log
dgit checkout a1b2c3d4
dgit diff a1b2c3d4 e5f6g7h8
dgit --help
```

## 3. 지원 명령어 요약

MVP 단계에서 지원할 CLI 명령어는 다음 6개이다.

| 명령어 | 실행 형식 | 설명 |
|---|---|---|
| `init` | `dgit init` | 현재 위치에서 dgit 저장소 초기화를 요청한다. |
| `add` | `dgit add <file>` | 지정한 파일을 추적 대상으로 등록한다. |
| `commit` | `dgit commit -m "message"` | 현재 변경 상태를 커밋한다. |
| `log` | `dgit log` | 커밋 히스토리를 출력한다. |
| `checkout` | `dgit checkout <commit_id>` | 지정한 커밋 시점으로 복원한다. |
| `diff` | `dgit diff <commit_id_1> <commit_id_2>` | 두 커밋 사이의 변경 요약을 출력한다. |

## 4. 명령어별 인터페이스 정의

### 4.1 `dgit init`

#### 형식

```bash
dgit init
```

#### 인자

없음.

#### 정상 출력 예시

```text
Initialized empty dgit repository.
```

#### 에러 출력 예시

이미 초기화된 위치에서 실행한 경우:

```text
Error: repository already initialized.
```

불필요한 인자가 추가된 경우:

```text
Error: init does not take arguments.
Usage: dgit init
```

---

### 4.2 `dgit add <file>`

#### 형식

```bash
dgit add <file>
```

#### 인자

| 인자 | 필수 여부 | 설명 |
|---|---|---|
| `<file>` | 필수 | 추적 대상으로 등록할 파일 경로 |

#### 정상 출력 예시

```text
Added: tree_model.fbx
```

#### 에러 출력 예시

파일 인자가 없는 경우:

```text
Error: missing file path.
Usage: dgit add <file>
```

파일이 존재하지 않는 경우:

```text
Error: file not found: tree_model.fbx
```

저장소가 초기화되지 않은 경우:

```text
Error: not a dgit repository. Run 'dgit init' first.
```

---

### 4.3 `dgit commit -m "message"`

#### 형식

```bash
dgit commit -m "message"
```

#### 인자 및 옵션

| 옵션/인자 | 필수 여부 | 설명 |
|---|---|---|
| `-m` | 필수 | 커밋 메시지를 입력하겠다는 옵션 |
| `"message"` | 필수 | 커밋 메시지 문자열 |

#### 정상 출력 예시

```text
Committed: 3f4a9c2b
Message: modify mesh vertex
```

#### 에러 출력 예시

`-m` 옵션이 없는 경우:

```text
Error: commit message is required. Use -m "message".
Usage: dgit commit -m "message"
```

`-m` 뒤에 메시지가 없는 경우:

```text
Error: empty commit message.
Usage: dgit commit -m "message"
```

추적 중인 파일이 없는 경우:

```text
Error: no tracked files. Use 'dgit add <file>' first.
```

저장소가 초기화되지 않은 경우:

```text
Error: not a dgit repository. Run 'dgit init' first.
```

---

### 4.4 `dgit log`

#### 형식

```bash
dgit log
```

#### 인자

없음.

#### 정상 출력 예시

```text
commit 3f4a9c2b
Date: 2026-05-03T14:20:11
Message: modify mesh vertex

commit a1b2c3d4
Date: 2026-05-03T13:55:02
Message: initial commit
```

#### 에러 출력 예시

커밋 기록이 없는 경우:

```text
No commits yet.
```

불필요한 인자가 추가된 경우:

```text
Error: log does not take arguments.
Usage: dgit log
```

저장소가 초기화되지 않은 경우:

```text
Error: not a dgit repository. Run 'dgit init' first.
```

---

### 4.5 `dgit checkout <commit_id>`

#### 형식

```bash
dgit checkout <commit_id>
```

#### 인자

| 인자 | 필수 여부 | 설명 |
|---|---|---|
| `<commit_id>` | 필수 | 복원할 대상 커밋 ID |

#### 정상 출력 예시

```text
Checked out commit: 3f4a9c2b
```

#### 에러 출력 예시

커밋 ID가 없는 경우:

```text
Error: missing commit ID.
Usage: dgit checkout <commit_id>
```

커밋 ID가 존재하지 않는 경우:

```text
Error: commit not found: 3f4a9c2b
```

저장소가 초기화되지 않은 경우:

```text
Error: not a dgit repository. Run 'dgit init' first.
```

---

### 4.6 `dgit diff <commit_id_1> <commit_id_2>`

#### 형식

```bash
dgit diff <commit_id_1> <commit_id_2>
```

#### 인자

| 인자 | 필수 여부 | 설명 |
|---|---|---|
| `<commit_id_1>` | 필수 | 비교 기준 커밋 ID |
| `<commit_id_2>` | 필수 | 비교 대상 커밋 ID |

#### 정상 출력 예시

```text
Diff: a1b2c3d4 -> 3f4a9c2b
File: tree_model.fbx
Delta size: 12.4 MB
Changed blocks: 128
Unchanged blocks: 654912
```

#### 에러 출력 예시

인자가 부족한 경우:

```text
Error: diff requires two commit IDs.
Usage: dgit diff <commit_id_1> <commit_id_2>
```

커밋 ID가 존재하지 않는 경우:

```text
Error: commit not found: <commit_id>
```

저장소가 초기화되지 않은 경우:

```text
Error: not a dgit repository. Run 'dgit init' first.
```

## 5. 인자 파싱 규칙

CLI는 `argc`와 `argv`를 기준으로 입력을 해석한다.

기본 파싱 규칙은 다음과 같다.

1. `argc`가 1이면 사용법을 출력한다.
2. `argv[1]`을 명령어 이름으로 판단한다.
3. 명령어가 `init`, `add`, `commit`, `log`, `checkout`, `diff`, `help`, `--help` 중 하나인지 확인한다.
4. 명령어별 필수 인자 개수를 검사한다.
5. 잘못된 명령어, 잘못된 옵션, 부족한 인자는 에러 메시지와 사용법을 함께 출력한다.
6. 정상 입력이면 해당 명령어 처리를 실행한다.

예를 들어 다음 입력은:

```bash
dgit commit -m "update vertex position"
```

다음과 같이 해석한다.

| 위치 | 값 | 의미 |
|---|---|---|
| `argv[0]` | `dgit` | 실행 파일 이름 |
| `argv[1]` | `commit` | 명령어 |
| `argv[2]` | `-m` | 커밋 메시지 옵션 |
| `argv[3]` | `update vertex position` | 커밋 메시지 |

## 6. 공통 에러 메시지 규칙

모든 에러 메시지는 `Error:`로 시작한다.

| 상황 | 에러 메시지 예시 |
|---|---|
| 알 수 없는 명령어 | `Error: unknown command: <command>` |
| 인자 부족 | `Error: missing argument.` |
| 불필요한 인자 | `Error: too many arguments.` |
| 파일 없음 | `Error: file not found: <file>` |
| 커밋 메시지 없음 | `Error: commit message is required. Use -m "message".` |
| 커밋 ID 없음 | `Error: commit not found: <commit_id>` |
| 저장소 초기화 필요 | `Error: not a dgit repository. Run 'dgit init' first.` |
| 명령어 실행 실패 | `Error: command failed.` |

## 7. 종료 코드 규칙

| 상황 | 종료 코드 |
|---|---|
| 정상 실행 | `0` |
| 사용자 입력 오류 | `1` |
| 명령어 실행 실패 | `1` |

## 8. 도움말 인터페이스

### 8.1 전체 도움말

```bash
dgit --help
```

또는:

```bash
dgit help
```

출력 예시는 다음과 같다.

```text
Usage: dgit <command> [options]

Commands:
  init                         Initialize a dgit repository
  add <file>                   Track a file
  commit -m "message"          Save current changes
  log                          Show commit history
  checkout <commit_id>         Restore a specific version
  diff <commit1> <commit2>     Show difference summary
  help                         Show this help message
```

### 8.2 명령어별 도움말

```bash
dgit commit --help
```

출력 예시는 다음과 같다.

```text
Usage: dgit commit -m "message"

Create a new commit with the given message.
```

## 9. CLI 처리 흐름

```text
사용자 명령어 입력
        ↓
argc / argv 확인
        ↓
명령어 종류 판단
        ↓
필수 인자 및 옵션 검사
        ↓
명령어 실행
        ↓
성공 또는 실패 메시지 출력
        ↓
종료 코드 반환
```

## 10. 구현 시 주의사항

- 파일 경로에는 공백이 포함될 수 있으므로 문자열 전체를 하나의 경로로 처리한다.
- 커밋 메시지에는 공백이 포함될 수 있으므로 `-m` 뒤의 문자열 전체를 하나의 메시지로 처리한다.
- 알 수 없는 명령어를 입력해도 프로그램이 비정상 종료되지 않고 사용법을 출력해야 한다.
- 모든 실패 상황은 사용자가 원인을 알 수 있도록 구체적인 에러 메시지를 출력해야 한다.
- 명령어 이름과 옵션 이름은 Git과 유사하게 직관적으로 유지한다.

## 11. 완료 기준

이 문서 기준으로 CLI 인터페이스 문서화의 완료 기준은 다음과 같다.

- 지원 명령어 6개(`init`, `add`, `commit`, `log`, `checkout`, `diff`)의 사용 형식이 정의되어 있다.
- 각 명령어의 필수 인자와 옵션이 정의되어 있다.
- 정상 출력 예시와 에러 출력 예시가 정의되어 있다.
- 공통 인자 파싱 규칙이 정의되어 있다.
- `--help` 출력 형식이 정의되어 있다.
- 종료 코드 규칙이 정의되어 있다.
