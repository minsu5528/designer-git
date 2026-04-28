#pragma once
#include <cstdint>

// ── 오류 코드 ─────────────────────────────────────────────────
// CLI에서 원인별 메시지 출력에 활용
enum DeltaError {
    DELTA_OK             =  0,
    ERR_OPEN_SRC         = -1,   // 원본 파일 열기 실패
    ERR_OPEN_DELTA       = -2,   // delta 파일 열기 실패
    ERR_OPEN_OUT         = -3,   // 출력 파일 열기 실패
    ERR_BAD_CMD          = -4,   // delta 내 알 수 없는 명령 (손상 감지)
    ERR_TRUNCATED_DELTA  = -5,   // delta 파일 중간 잘림
    ERR_BASE_READ        = -6,   // base 파일 읽기 실패 / 부족
    ERR_OUT_WRITE        = -7,   // 출력 쓰기 실패
    ERR_BAD_MAGIC        = -8,   // delta 파일 magic 불일치
    ERR_BAD_VERSION      = -9,   // delta 파일 버전 불일치
    ERR_INVALID_LEN      = -10,  // 명령 길이 값이 비정상 (손상 또는 악성)
};

/**
 * delta_create
 * 파일 A(이전 버전)와 파일 B(현재 버전)를 비교해 delta 파일을 생성한다.
 *
 * 동작 방식:
 *   1. 파일 A를 CDC로 분할 → 이중 해시 해시맵 구성 (데이터 비저장, ~20MB)
 *   2. 파일 B를 CDC로 분할하면서 즉시 COPY/INSERT 명령 출력 (스트리밍)
 *   - COPY  : cmd(1B) + offset(uint64 8B) + length(uint64 8B) = 17바이트
 *   - INSERT: cmd(1B) + length(uint64 8B) + 실제 데이터
 *
 * CDC 파라미터 근거:
 *   - 슬라이딩 윈도우 48바이트: 경계 안정성과 연산 비용의 균형
 *   - 평균 블록 목표 ~16KB: MASK=16383=(2^14-1), P(hit)≈1/16384
 *   - 최소 블록 4KB: 지나친 분할 방지
 *   - 최대 블록 64KB: 과대 청크 방지
 *
 * @param path_a    원본 파일 경로 (이전 버전)
 * @param path_b    새 파일 경로 (현재 버전)
 * @param out_delta 생성할 delta 파일 경로
 * @return          DELTA_OK(0) 성공, 음수 DeltaError 실패
 *
 * 담당: 김민수
 * 사용: 김채연 (commit 내부)
 */
int delta_create(const char* path_a,
                 const char* path_b,
                 const char* out_delta);

/**
 * delta_apply
 * delta 파일을 베이스 파일에 적용해서 파일을 복원한다.
 *
 * 동작 방식:
 *   1. delta 파일 헤더 검증 (magic "DGDELTA\0" + version 1)
 *   2. COPY  → base 파일의 offset 위치에서 length 바이트 복사
 *   3. INSERT → delta에 포함된 데이터 그대로 출력
 *   4. 알 수 없는 cmd → ERR_BAD_CMD (손상 파일 조기 감지)
 *   5. 모든 read/write 실패 즉시 감지 및 반환
 *   6. length 상한 검증으로 악성/손상 delta 방어
 *
 * @param path_base  베이스 파일 경로
 * @param path_delta delta 파일 경로
 * @param out_file   복원된 파일을 저장할 경로
 * @return           DELTA_OK(0) 성공, 음수 DeltaError 실패
 *
 * 담당: 김민수
 * 사용: 김채연 (checkout 내부)
 */
int delta_apply(const char* path_base,
                const char* path_delta,
                const char* out_file);
