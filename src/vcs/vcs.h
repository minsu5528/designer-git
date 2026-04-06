#pragma once
#include <string>

/**
 * init_repository
 * .vcs 폴더 전체 구조를 생성한다. (dgit init)
 *
 * 생성 목록:
 *   .vcs/objects/base/
 *   .vcs/objects/deltas/
 *   .vcs/commits/
 *   .vcs/index  (빈 파일)
 *   .vcs/HEAD   (빈 파일)
 *
 * @param path 저장소를 초기화할 디렉터리 경로
 * @return     0 성공, -1 이미 존재하거나 실패
 *
 * 담당: 김채연
 */
int init_repository(const std::string& path);

/**
 * add_file
 * 파일을 추적 대상으로 등록한다. (dgit add)
 * .vcs/index 파일에 경로를 한 줄 추가한다.
 *
 * @param filepath 추적할 파일 경로
 * @return         0 성공, -1 실패
 *
 * 담당: 김채연
 */
int add_file(const std::string& filepath);

/**
 * commit
 * 현재 상태를 커밋한다. (dgit commit)
 * 내부적으로 delta_create()를 호출해서 delta를 생성하고
 * .vcs/commits/[ID].json 으로 메타데이터를 저장한다.
 *
 * @param message 커밋 메시지
 * @return        커밋 ID 문자열 (SHA256), 실패 시 빈 문자열
 *
 * 담당: 김채연
 * 사용: 강민경 (CLI), 윤현우 (테스트)
 */
std::string commit(const std::string& message);

/**
 * checkout
 * 특정 커밋 시점으로 파일을 복원한다. (dgit checkout)
 * 내부적으로 delta_apply()를 순서대로 호출한다.
 *
 * @param commit_id 복원할 커밋 ID
 * @return          0 성공, -1 실패
 *
 * 담당: 김채연
 * 사용: 강민경 (CLI), 윤현우 (테스트)
 */
int checkout(const std::string& commit_id);

/**
 * log
 * 커밋 히스토리를 출력한다. (dgit log)
 * HEAD에서 시작해서 parent_id를 따라가며 커밋 정보를 출력한다.
 *
 * 담당: 김채연
 * 사용: 강민경 (CLI)
 */
void log();