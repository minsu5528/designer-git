#pragma once

/**
 * delta_create
 * 파일 A와 파일 B를 비교해서 변경분(delta)만 추출한다.
 *
 * 동작 방식:
 *   1. 파일 A의 각 블록 해시를 해시맵에 저장
 *   2. 파일 B를 슬라이딩 윈도우로 읽으면서 해시 비교
 *   3. 같은 블록 → COPY 명령, 다른 블록 → INSERT 명령
 *   4. 명령들을 이진 파일로 직렬화 + zstd 압축
 *
 * @param path_a    원본 파일 경로 (이전 버전)
 * @param path_b    새 파일 경로 (현재 버전)
 * @param out_delta 생성할 delta 파일 경로
 * @return          0 성공, -1 실패
 *
 * 담당: 김민수 (feat/engine-rolling-hash)
 * 사용: 김채연 (commit 내부), 윤현우 (테스트)
 */
int delta_create(const char* path_a,
                 const char* path_b,
                 const char* out_delta);

/**
 * delta_apply
 * delta 파일을 베이스 파일에 적용해서 원하는 버전의 파일을 복원한다.
 *
 * 동작 방식:
 *   1. delta 파일 읽기 (zstd 압축 해제)
 *   2. COPY 명령 → 베이스 파일에서 해당 오프셋 데이터 읽어서 쓰기
 *   3. INSERT 명령 → delta에 포함된 데이터 그대로 쓰기
 *
 * @param path_base  베이스 파일 경로
 * @param path_delta delta 파일 경로
 * @param out_file   복원된 파일을 저장할 경로
 * @return           0 성공, -1 실패
 *
 * 담당: 김민수 (feat/engine-rolling-hash)
 * 사용: 김채연 (checkout 내부), 윤현우 (테스트)
 */
int delta_apply(const char* path_base,
                const char* path_delta,
                const char* out_file);