# Delta 엔진 설계 문서

담당: 김민수 (역할 1)  
브랜치: feat/engine-rolling-hash

---

## 1. 핵심 아이디어

두 파일(버전 A, 버전 B)을 비교해서 달라진 부분만 추출하는 엔진이다.  
파일 전체를 저장하는 대신 "무엇이 어떻게 바뀌었는지"만 저장한다.

### 비유
책 1,000페이지 중 3페이지만 수정됐을 때, 수정된 3페이지만 우편으로 보내는 것.  
전체 책을 다시 보내지 않는다.

---

## 2. 알고리즘 선택: Rabin-Karp Rolling Hash

### 왜 고정 크기 분할이 안 되나
파일을 고정 크기로 자르면 앞에 데이터 1바이트가 삽입될 때  
이후 모든 블록이 전부 "변경됨"으로 감지된다.  
실제로는 1바이트만 바뀌었는데 전체가 delta에 잡히는 문제가 생긴다.

### 왜 Rolling Hash(CDC)인가
파일 내용을 기준으로 블록 경계를 정하기 때문에  
데이터가 앞에 삽입되어도 내용이 같은 블록은 동일하게 감지된다.

### 블록 크기: 16KB
- 8KB보다 작으면: 해시 연산 횟수 증가, 속도 저하
- 32KB보다 크면: 작은 수정도 큰 delta 발생
- 16KB가 균형점. 벤치마크 후 조정 가능.

---

## 3. 슈도코드

### delta_create(파일A, 파일B) → delta파일
```
함수 delta_create(path_a, path_b, out_delta):

  1. 파일 A를 16KB씩 읽으면서 각 블록의 해시값 계산
     → 해시맵에 저장: { 해시값 → (오프셋, 길이) }

  2. 파일 B를 16KB씩 슬라이딩 윈도우로 읽으면서:
     a. 현재 블록의 해시 계산
     b. 해시맵에 있으면 → COPY 명령 기록
     c. 해시맵에 없으면 → INSERT 명령 기록 + 실제 데이터

  3. 모든 명령을 이진 파일로 직렬화 → out_delta 저장
  4. out_delta를 zstd로 압축

  반환값: 0 성공, -1 실패
```

### delta_apply(베이스파일, delta파일) → 결과파일
```
함수 delta_apply(path_base, path_delta, out_file):

  1. delta 파일 읽기 (zstd 압축 해제)

  2. 명령어 순서대로 실행:
     a. COPY 명령 → path_base에서 해당 오프셋 데이터 읽어서 쓰기
     b. INSERT 명령 → delta에 포함된 데이터 그대로 쓰기

  3. out_file 완성

  반환값: 0 성공, -1 실패
```

### 스트리밍 처리 (OOM 방지)
```
const size_t CHUNK = 16 * 1024;  // 16KB
uint8_t buf[CHUNK];              // 스택에 고정 크기

while (file.read((char*)buf, CHUNK) || file.gcount() > 0) {
    size_t n = file.gcount();
    // 해시 계산 + 비교
    // buf는 다음 루프에서 덮어써짐 → 자동으로 버려짐
}
// 파일이 10GB든 100GB든 항상 16KB만 사용
```

---

## 4. 인터페이스

`src/engine/delta.h` 참고.

| 함수 | 만드는 사람 | 쓰는 사람 |
|------|------------|----------|
| delta_create() | 김민수 | 김채연 (commit 내부), 윤현우 (테스트) |
| delta_apply()  | 김민수 | 김채연 (checkout 내부), 윤현우 (테스트) |

---

## 5. 구현 순서

1. 파일 스트리밍 I/O (16KB 청크 읽기)
2. compute_hash() — 블록 하나의 해시 계산
3. delta_create() 골격 — COPY/INSERT 명령 생성
4. delta_apply() — 명령 역적용
5. zstd 압축 연동