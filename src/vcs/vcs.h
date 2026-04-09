#pragma once
#include <string>
#include <vector>

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
 */
int init_repository(const std::string &path);

/**
 * add_file
 * 파일을 추적 대상으로 등록한다. (dgit add)
 * .vcs/index 파일에 경로를 한 줄 추가한다.
 *
 * @param filepath 추적할 파일 경로
 * @return         0 성공, -1 실패
 *
 */
int add_file(const std::string &filepath);

/**
 * commit
 * 현재 상태를 커밋한다. (dgit commit)
 * 내부적으로 delta_create()를 호출해서 delta를 생성하고
 * .vcs/commits/[ID].json 으로 메타데이터를 저장한다.
 *
 * @param repo_path   저장소 루트 경로
 * @param message     커밋 메시지
 * @param target_file 커밋 대상 파일 경로
 * @return            생성된 커밋 ID 문자열, 실패 시 빈 문자열
 */
std::string commit(const std::string &repo_path, const std::string &message, const std::string &target_file);

/**
 * checkout
 * 특정 커밋 시점으로 파일을 복원한다. (dgit checkout)
 * 내부적으로 delta_apply()를 순서대로 호출한다.
 *
 * @param commit_id 복원할 커밋 ID
 * @return          0 성공, -1 실패
 *
 * 사용: CLI, 테스트
 */
int checkout(const std::string &commit_id);

/**
 * log
 * 커밋 히스토리를 출력한다. (dgit log)
 * HEAD에서 시작해서 parent_id를 따라가며 커밋 정보를 출력한다.
 *
 * 사용: CLI
 */
void log();

// 커밋 내 개별 파일 정보를 관리하기 위한 구조체
struct FileEntry
{
    std::string path;  // 원본 파일 경로
    std::string delta; // delta 파일이나 base 파일의 저장 경로
    bool is_base;      // true면 base 파일, false면 delta 파일
};

// 커밋 메타데이터 구조체
struct CommitMetadata
{
    std::string id;               // 커밋 고유 ID
    std::string message;          // 커밋 메시지
    std::string timestamp;        // 생성 시각(ISO 8601)
    std::string parent_id;        // 이전 커밋 ID (없으면 빈 문자열)
    std::vector<FileEntry> files; // 커밋에 포함된 파일 목록
};

/**
 * save_commit_metadata
 * 생성된 커밋 객체를 JSON 파일로 변환하여 저장한다.
 * 저장 경로: .vcs/commits/[commit_id].json
 *
 * @param repo_path 저장소 루트 경로
 * @param meta      저장할 커밋 메타데이터 객체
 * @return          0 성공, -1 파일 쓰기 실패
 */
int save_commit_metadata(const std::string &repo_path, const CommitMetadata &meta);

/**
 * load_commit_metadata
 * 지정된 커밋 ID의 JSON 파일을 읽어 구조체로 복원한다.
 *
 * @param repo_path 저장소 루트 경로
 * @param commit_id 읽어올 커밋의 ID
 * @return          복원된 CommitMetadata 객체 (실패 시 빈 객체 반환)
 */
CommitMetadata load_commit_metadata(const std::string &repo_path, const std::string &commit_id);
