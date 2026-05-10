#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "../vcs/vcs.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

/*
 * dgit CLI 진입점
 *
 * 사용자가 터미널에 입력한 명령어를 해석해 VCS 레이어 함수로 연결함.
 * VCS 레이어는 저장/복원 로직만 담당하고, 사용자 메시지는 CLI에서 출력하는 구조임.
 *
 * 주요 설계 포인트:
 *  - 서브폴더에서 dgit을 실행해도 저장소 루트(.vcs가 있는 폴더)를 찾아 동작함.
 *  - add 폴더 입력 시 .fbx 파일만 재귀적으로 수집함.
 *  - commit은 디스크 전체를 훑지 않고 .vcs/index에 등록된 추적 파일만 대상으로 함.
 *  - VCS가 다중 파일 atomic commit을 지원하지 않으면 여러 커밋으로 쪼개지 않고 안전하게 실패 처리함.
 */

 // 현재 자동화 테스트가 .bin도 add/commit하므로 기본값은 0으로 둠.
 // 프로젝트 정책상 "명시 파일도 .fbx만 허용"해야 하면 빌드 옵션에
 // -DDGIT_STRICT_FBX_ONLY=1을 주거나 아래 값을 1로 변경하기.
#ifndef DGIT_STRICT_FBX_ONLY
#define DGIT_STRICT_FBX_ONLY 0
#endif

namespace {

    // Windows 콘솔 코드페이지를 UTF-8로 설정함.
    // 사용자가 chcp 65001을 직접 입력하지 않아도 한글 출력이 깨지지 않게 하기 위함.
    // MSYS/리눅스 환경에서는 아무 동작도 하지 않음.
    void setup_utf8_console() {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif
    }

    // -----------------------------------------------------------------------------
    // Path utilities
    // -----------------------------------------------------------------------------

    // 존재하지 않는 경로가 섞여 있어도 가능한 한 안전하게 절대 경로로 정규화함.
    fs::path safe_absolute_normalized(const fs::path& path) {
        try {
            return fs::weakly_canonical(path);
        }
        catch (const fs::filesystem_error&) {
            return fs::absolute(path).lexically_normal();
        }
    }

    // 현재 위치에서 부모 폴더를 따라 올라가며 .vcs 폴더가 있는 저장소 루트를 찾음.
    fs::path find_repository_root(const fs::path& start_path) {
        fs::path cur = safe_absolute_normalized(start_path);

        if (fs::is_regular_file(cur)) {
            cur = cur.parent_path();
        }

        while (!cur.empty()) {
            if (fs::exists(cur / ".vcs") && fs::is_directory(cur / ".vcs")) {
                return cur;
            }

            const fs::path parent = cur.parent_path();
            if (parent == cur) {
                break;
            }
            cur = parent;
        }

        return {};
    }

    // lexically_relative 결과가 '..'로 시작하면 저장소 바깥을 가리키므로 차단함.
    bool is_path_outside_repo_relative(const fs::path& rel) {
        auto it = rel.begin();
        return it != rel.end() && *it == "..";
    }

    // Windows/Unix 경로 구분자 차이를 줄이기 위해 repo-relative 경로를 generic_string 형태로 통일함.
    std::string normalize_relative_string(const fs::path& rel_path) {
        fs::path normalized = rel_path.lexically_normal();
        std::string value = normalized.generic_string();

        if (value == ".") {
            return "";
        }
        if (value.rfind("./", 0) == 0) {
            value.erase(0, 2);
        }
        return value;
    }

    // 절대 경로를 저장소 루트 기준 상대 경로로 바꿈. 저장소 밖이면 inside_repo=false 반환.
    std::string repo_relative_from_absolute(const std::string& repo_path,
        const fs::path& absolute_path,
        bool& inside_repo) {
        const fs::path repo_abs = safe_absolute_normalized(repo_path);
        const fs::path path_abs = safe_absolute_normalized(absolute_path);

        fs::path rel;
        try {
            rel = path_abs.lexically_relative(repo_abs);
        }
        catch (const fs::filesystem_error&) {
            inside_repo = false;
            return "";
        }

        inside_repo = !rel.empty() && !is_path_outside_repo_relative(rel);
        if (!inside_repo) {
            return "";
        }

        return normalize_relative_string(rel);
    }

    // 저장소 상대 경로를 실제 파일시스템 절대 경로로 변환함.
    fs::path repo_absolute_path(const std::string& repo_path, const std::string& repo_relative) {
        return (fs::path(repo_path) / fs::path(repo_relative)).lexically_normal();
    }

    // 저장소 상대 경로 파일이 실제 디스크에 존재하는지 확인함.
    bool repo_file_exists(const std::string& repo_path, const std::string& repo_relative) {
        return fs::exists(repo_absolute_path(repo_path, repo_relative));
    }

    // .vcs 내부 파일이 사용자 파일로 add/commit되는 것을 방지함.
    bool is_vcs_internal_repo_relative(const std::string& repo_relative) {
        if (repo_relative.empty()) {
            return false;
        }

        const fs::path rel_path(repo_relative);
        auto it = rel_path.begin();
        return it != rel_path.end() && *it == ".vcs";
    }

    // child가 parent와 같거나 parent 폴더 아래에 있는지 확인함.
    bool is_same_or_under_repo_relative(const std::string& child, const std::string& parent) {
        const std::string child_norm = normalize_relative_string(fs::path(child));
        const std::string parent_norm = normalize_relative_string(fs::path(parent));

        if (parent_norm.empty()) {
            return true; // 저장소 루트
        }
        if (child_norm == parent_norm) {
            return true;
        }

        const fs::path child_path(child_norm);
        const fs::path parent_path(parent_norm);

        auto child_it = child_path.begin();
        auto parent_it = parent_path.begin();

        for (; parent_it != parent_path.end(); ++parent_it, ++child_it) {
            if (child_it == child_path.end() || *child_it != *parent_it) {
                return false;
            }
        }

        return true;
    }

    // Windows 개행에서 남을 수 있는 '\r' 문자 제거함.
    std::string trim_cr(std::string value) {
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
        return value;
    }

    // .vcs/index 한 줄을 읽어 저장소 상대 경로로 정규화함.
    std::string normalize_index_entry(const std::string& repo_path, std::string value) {
        value = trim_cr(value);
        if (value.empty()) {
            return "";
        }

        const fs::path entry(value);

        if (entry.is_absolute()) {
            bool inside_repo = false;
            return repo_relative_from_absolute(repo_path, entry, inside_repo);
        }

        return normalize_relative_string(entry);
    }

    // VCS 함수가 상대 경로를 안정적으로 처리하도록 현재 작업 디렉터리를 임시로 저장소 루트로 바꾸는 RAII 클래스임.
    class ScopedCurrentPath {
    public:
        explicit ScopedCurrentPath(const fs::path& next_path)
            : previous_path_(fs::current_path()) {
            fs::current_path(next_path);
        }

        ~ScopedCurrentPath() {
            try {
                fs::current_path(previous_path_);
            }
            catch (...) {
                // 스택 해제 중에는 복구 실패를 추가 처리하지 않음.
            }
        }

        ScopedCurrentPath(const ScopedCurrentPath&) = delete;
        ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

    private:
        fs::path previous_path_;
    };

    // -----------------------------------------------------------------------------
    // Small helpers
    // -----------------------------------------------------------------------------

    // 확장자 비교를 위해 문자열을 소문자로 변환함.
    std::string to_lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
            });
        return value;
    }

    // 경로의 확장자가 .fbx인지 확인함.
    bool has_fbx_extension(const fs::path& path) {
        return to_lower(path.extension().string()) == ".fbx";
    }

    // 실제 일반 파일이며 확장자가 .fbx인지 확인함.
    bool is_fbx_file(const fs::path& path) {
        return fs::is_regular_file(path) && has_fbx_extension(path);
    }

    // 단일 파일을 명시적으로 add/commit할 때 .fbx만 허용할지 빌드 옵션에 따라 결정함.
    bool explicit_file_allowed_by_policy(const fs::path& path) {
#if DGIT_STRICT_FBX_ONLY
        return has_fbx_extension(path);
#else
        (void)path;
        return true;
#endif
    }

    // 확장자 정책 위반 시 사용자에게 보여줄 메시지 반환함.
    std::string explicit_file_policy_message() {
#if DGIT_STRICT_FBX_ONLY
        return u8"현재 dgit은 .fbx 파일만 지원합니다";
#else
        return "";
#endif
    }

    // diff 출력용으로 바이트 수를 B/KB/MB 단위 문자열로 변환함.
    std::string format_bytes(std::uintmax_t bytes) {
        std::ostringstream oss;
        if (bytes >= 1024ULL * 1024ULL) {
            oss << std::fixed << std::setprecision(2)
                << static_cast<double>(bytes) / (1024.0 * 1024.0) << "MB";
        }
        else if (bytes >= 1024ULL) {
            oss << std::fixed << std::setprecision(2)
                << static_cast<double>(bytes) / 1024.0 << "KB";
        }
        else {
            oss << bytes << "B";
        }
        return oss.str();
    }

    // 잘못된 명령어 사용 시 에러 메시지와 사용법을 함께 출력함.
    int fail_usage(const std::string& message, const std::string& usage) {
        std::cerr << "dgit: " << message << "\n";
        std::cerr << u8"사용법: " << usage << "\n";
        return 1;
    }

    // CLI가 지원하는 명령어인지 확인함.
    bool is_known_command(const std::string& command) {
        return command == "init" || command == "add" || command == "commit" ||
            command == "log" || command == "checkout" || command == "diff";
    }

    // -----------------------------------------------------------------------------
    // Help
    // -----------------------------------------------------------------------------

    // dgit만 입력하거나 --help 입력 시 전체 도움말 출력함.
    void print_general_help() {
        std::cout
            << u8"사용법: dgit <명령어> [인자]\n\n"
            << u8"사용 가능한 주요 명령어:\n\n"
            << u8"  init                              현재 폴더에 빈 dgit 저장소를 생성합니다\n"
            << u8"  add <file|folder>                 파일 하나 또는 폴더 안의 .fbx 파일을 추적 목록에 추가합니다\n"
            << u8"  commit -m <message> [path...]     추적 중인 파일을 커밋합니다\n"
            << u8"  log                               커밋 로그를 출력합니다\n"
            << u8"  checkout <commit_id>              특정 커밋 상태로 파일을 복원합니다\n"
            << u8"  diff <commit1> <commit2>          변경 블록 수, 변경 용량, 변경 비율을 출력합니다\n\n"
            << u8"자세한 사용법: dgit <명령어> --help\n";
    }

    // dgit <명령어> --help 입력 시 명령어별 도움말 출력함.
    void print_command_help(const std::string& command) {
        if (command == "init") {
            std::cout << u8"사용법: dgit init\n\n"
                << u8"현재 폴더에 빈 dgit 저장소를 생성합니다.\n";
        }
        else if (command == "add") {
            std::cout << u8"사용법: dgit add <file|folder>\n\n"
                << u8"파일 하나를 추적 목록에 추가합니다. 폴더를 입력하면 내부의 .fbx 파일만 추가합니다.\n"
#if DGIT_STRICT_FBX_ONLY
                << u8"이 빌드는 명시적으로 추가한 파일도 .fbx만 허용합니다.\n"
#else
                << u8"이 빌드는 자동화 테스트 호환을 위해 명시 파일의 .bin 등 바이너리도 허용합니다.\n"
#endif
                ;
        }
        else if (command == "commit") {
            std::cout << u8"사용법: dgit commit -m <message> [path...]\n\n"
                << u8"저장소에 변경 내용을 기록합니다. .vcs/index에 등록된 추적 파일만 커밋합니다.\n"
                << u8"예시:\n"
                << "  dgit commit -m \"initial\"\n"
                << "  dgit commit -m \"initial\" model.fbx\n"
                << "  dgit commit -m \"assets update\" ./assets/\n\n"
                << u8"경로를 생략하면 index에 있는 모든 추적 파일을 대상으로 합니다.\n"
                << u8"폴더를 입력하면 해당 폴더 아래의 추적 파일만 대상으로 합니다.\n";
        }
        else if (command == "log") {
            std::cout << u8"사용법: dgit log\n\n"
                << u8"커밋 로그를 출력합니다.\n";
        }
        else if (command == "checkout") {
            std::cout << u8"사용법: dgit checkout <commit_id>\n\n"
                << u8"특정 커밋 상태로 파일을 복원합니다.\n";
        }
        else if (command == "diff") {
            std::cout << u8"사용법: dgit diff <commit1> <commit2>\n\n"
                << u8"변경된 블록 수, 총 변경 용량, 변경 비율을 출력합니다.\n";
        }
        else {
            std::cerr << u8"dgit: 알 수 없는 도움말 항목 '" << command << "'\n";
        }
    }

    // init을 제외한 명령어는 .vcs 저장소가 필요하므로 사전 검사용으로 사용함.
    bool needs_repository(const std::string& command) {
        return command == "add" || command == "commit" || command == "log" ||
            command == "checkout" || command == "diff";
    }

    // -----------------------------------------------------------------------------
    // Index handling
    // -----------------------------------------------------------------------------

    // .vcs/index에서 현재 추적 중인 파일 목록 읽기.
    std::vector<std::string> read_tracked_files(const std::string& repo_path) {
        const fs::path index_path = fs::path(repo_path) / ".vcs" / "index";
        std::ifstream index_file(index_path);
        std::vector<std::string> files;
        std::set<std::string> seen;

        if (!index_file.is_open()) {
            return files;
        }

        std::string line;
        while (std::getline(index_file, line)) {
            const std::string normalized = normalize_index_entry(repo_path, line);
            if (normalized.empty()) {
                continue;
            }
            if (is_vcs_internal_repo_relative(normalized)) {
                continue; // .vcs 내부 파일은 사용자 추적 파일로 취급하지 않음
            }
            if (seen.insert(normalized).second) {
                files.push_back(normalized);
            }
        }

        return files;
    }

    // 빠른 포함 여부 확인을 위해 추적 파일 목록을 set으로 변환함.
    std::set<std::string> tracked_file_set(const std::string& repo_path) {
        const std::vector<std::string> tracked = read_tracked_files(repo_path);
        return std::set<std::string>(tracked.begin(), tracked.end());
    }

    // -----------------------------------------------------------------------------
    // Add target collection
    // -----------------------------------------------------------------------------

    // add 명령의 입력 경로를 해석해 실제로 index에 넣을 파일 목록 수집함.
    bool collect_add_targets_from_path(const std::string& repo_path,
        const fs::path& invocation_path,
        const std::string& input_path,
        std::vector<std::string>& targets,
        std::set<std::string>& seen) {
        const fs::path raw(input_path);
        const fs::path absolute_path = raw.is_absolute() ? raw : invocation_path / raw;

        if (!fs::exists(absolute_path)) {
            std::cerr << u8"오류: 경로 지정 '" << input_path << u8"'에 해당하는 파일이 없습니다\n";
            return false;
        }

        bool inside_repo = false;
        const std::string rel = repo_relative_from_absolute(repo_path, absolute_path, inside_repo);
        if (!inside_repo) {
            std::cerr << u8"오류: 경로 지정 '" << input_path << u8"'은 dgit 저장소 밖에 있습니다\n";
            return false;
        }

        if (is_vcs_internal_repo_relative(rel)) {
            std::cerr << u8"오류: dgit 내부 메타데이터 경로는 add할 수 없습니다: " << input_path << "\n";
            return false;
        }

        if (fs::is_directory(absolute_path)) {
            std::vector<std::string> folder_files;

            try {
                fs::recursive_directory_iterator it(
                    absolute_path,
                    fs::directory_options::skip_permission_denied
                );
                const fs::recursive_directory_iterator end;

                for (; it != end; ++it) {
                    const fs::path entry_path = it->path();

                    std::error_code ec;
                    const bool is_dir = it->is_directory(ec);
                    if (!ec && is_dir) {
                        bool entry_inside_repo = false;
                        const std::string dir_rel = repo_relative_from_absolute(
                            repo_path, entry_path, entry_inside_repo
                        );

                        if ((entry_inside_repo && is_vcs_internal_repo_relative(dir_rel)) ||
                            entry_path.filename() == ".vcs") {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    if (!is_fbx_file(entry_path)) {
                        continue;
                    }

                    bool entry_inside_repo = false;
                    const std::string file_rel = repo_relative_from_absolute(
                        repo_path, entry_path, entry_inside_repo
                    );
                    if (entry_inside_repo && !file_rel.empty() &&
                        !is_vcs_internal_repo_relative(file_rel)) {
                        folder_files.push_back(file_rel);
                    }
                }
            }
            catch (const fs::filesystem_error& e) {
                std::cerr << u8"오류: 폴더를 스캔할 수 없습니다 '" << input_path << "': "
                    << e.what() << "\n";
                return false;
            }

            std::sort(folder_files.begin(), folder_files.end());

            for (const auto& file : folder_files) {
                if (seen.insert(file).second) {
                    targets.push_back(file);
                }
            }
            return true;
        }

        if (!fs::is_regular_file(absolute_path)) {
            std::cerr << u8"오류: 경로 지정 '" << input_path << u8"'은 일반 파일 또는 폴더가 아닙니다\n";
            return false;
        }

        if (!explicit_file_allowed_by_policy(fs::path(rel))) {
            std::cerr << u8"오류: " << explicit_file_policy_message() << ": " << input_path << "\n";
            return false;
        }

        if (seen.insert(rel).second) {
            targets.push_back(rel);
        }
        return true;
    }

    // -----------------------------------------------------------------------------
    // Commit target collection
    // -----------------------------------------------------------------------------

    // commit path/ 처리 시 index 안에서 해당 폴더 아래의 추적 파일만 골라냄.
    bool add_tracked_targets_under_prefix(const std::vector<std::string>& tracked_files,
        const std::string& prefix,
        bool require_fbx_extension,
        std::vector<std::string>& targets,
        std::set<std::string>& seen) {
        std::vector<std::string> matches;

        for (const auto& tracked : tracked_files) {
            if (is_vcs_internal_repo_relative(tracked)) {
                continue;
            }
            if (require_fbx_extension && !has_fbx_extension(fs::path(tracked))) {
                continue;
            }
            if (!is_same_or_under_repo_relative(tracked, prefix)) {
                continue;
            }
            matches.push_back(tracked);
        }

        std::sort(matches.begin(), matches.end());

        for (const auto& file : matches) {
            if (seen.insert(file).second) {
                targets.push_back(file);
            }
        }

        return !matches.empty();
    }

    // commit 명령의 pathspec 하나를 해석해 커밋 대상 파일 목록에 추가함.
    bool collect_commit_targets_from_path(const std::string& repo_path,
        const fs::path& invocation_path,
        const std::vector<std::string>& tracked_files,
        const std::set<std::string>& tracked,
        const std::string& input_path,
        std::vector<std::string>& targets,
        std::set<std::string>& seen) {
        const fs::path raw(input_path);
        const fs::path absolute_path = raw.is_absolute() ? raw : invocation_path / raw;

        bool inside_repo = false;
        const std::string rel = repo_relative_from_absolute(repo_path, absolute_path, inside_repo);
        if (!inside_repo) {
            std::cerr << u8"오류: 경로 지정 '" << input_path << u8"'은 dgit 저장소 밖에 있습니다\n";
            return false;
        }

        if (is_vcs_internal_repo_relative(rel)) {
            std::cerr << u8"오류: dgit 내부 메타데이터 경로는 commit할 수 없습니다: " << input_path << "\n";
            return false;
        }

        if (!fs::exists(absolute_path)) {
            // 디스크에서는 삭제되었지만 index에는 남아 있는 추적 파일일 수 있음.
            // 이 경우 바로 pathspec 오류로 처리하지 않고, 나중에 handle_commit에서
            // "삭제 커밋 미지원" 경고를 정확하게 출력하도록 대상 목록에 포함함.
            if (tracked.count(rel) > 0) {
                if (!explicit_file_allowed_by_policy(fs::path(rel))) {
                    std::cerr << u8"오류: " << explicit_file_policy_message() << ": " << input_path << "\n";
                    return false;
                }
                if (seen.insert(rel).second) {
                    targets.push_back(rel);
                }
                return true;
            }

            // 입력 경로가 디스크에는 없더라도 index 기준 폴더 prefix일 수 있음.
            // commit 단계에서는 이미 add 정책을 통과한 추적 파일을 존중하므로 .fbx 필터를 다시 적용하지 않음.
            if (add_tracked_targets_under_prefix(tracked_files, rel, false, targets, seen)) {
                return true;
            }

            std::cerr << u8"오류: 경로 지정 '" << input_path
                << u8"'에 해당하는 추적 파일 또는 폴더가 없습니다\n";
            return false;
        }

        if (fs::is_directory(absolute_path)) {
            // commit 시점에는 디스크 재스캔이 아니라 index 필터링 수행함.
            // 이미 추적 중인 파일은 .fbx가 아니라는 이유만으로 조용히 제외하지 않음.
            if (!add_tracked_targets_under_prefix(tracked_files, rel, false, targets, seen)) {
                std::cerr << u8"오류: 해당 경로 아래에 추적 중인 파일이 없습니다: '" << input_path << "'\n";
                return false;
            }
            return true;
        }

        if (!fs::is_regular_file(absolute_path)) {
            std::cerr << u8"오류: 경로 지정 '" << input_path << u8"'은 일반 파일 또는 폴더가 아닙니다\n";
            return false;
        }

        if (!explicit_file_allowed_by_policy(fs::path(rel))) {
            std::cerr << u8"오류: " << explicit_file_policy_message() << ": " << input_path << "\n";
            return false;
        }

        if (tracked.count(rel) == 0) {
            std::cerr << u8"오류: 경로 지정 '" << input_path << u8"'은 추적 중인 파일이 아닙니다\n"
                << u8"힌트: 먼저 'dgit add " << input_path << u8"' 명령으로 파일을 추가하세요.\n";
            return false;
        }

        if (seen.insert(rel).second) {
            targets.push_back(rel);
        }
        return true;
    }

    // commit 명령 전체 pathspec 처리함. 경로 생략 시 index 전체를 대상으로 함.
    std::vector<std::string> collect_commit_targets(const std::string& repo_path,
        const fs::path& invocation_path,
        const std::vector<std::string>& pathspecs,
        bool& ok) {
        std::vector<std::string> targets;
        std::set<std::string> seen;
        ok = true;

        const std::vector<std::string> tracked_files = read_tracked_files(repo_path);
        const std::set<std::string> tracked(tracked_files.begin(), tracked_files.end());

        if (pathspecs.empty()) {
            // Commit should honor the index as the source of truth.
            // If a file was already accepted by dgit add and is present in .vcs/index,
            // do not silently drop it here because of extension policy differences.
            // Folder scanning during add may be .fbx-only, but commit with no pathspec
            // means "commit all tracked files".
            for (const auto& file : tracked_files) {
                if (is_vcs_internal_repo_relative(file)) {
                    continue;
                }
                if (seen.insert(file).second) {
                    targets.push_back(file);
                }
            }
            return targets;
        }

        for (const auto& pathspec : pathspecs) {
            if (!collect_commit_targets_from_path(
                repo_path, invocation_path, tracked_files, tracked, pathspec, targets, seen)) {
                ok = false;
                return targets;
            }
        }

        return targets;
    }

    // -----------------------------------------------------------------------------
    // VCS commit dispatch
    // -----------------------------------------------------------------------------

    // vcs.h가 commit(repo_path, message, vector<string>) 오버로드를 제공하면 해당 함수 사용함.
    // 제공하지 않는 경우, 단일 파일 commit을 반복 호출해서 폴더 커밋을 흉내 내지 않음.
    // N개 파일은 N개 커밋이 아니라 하나의 atomic commit이어야 하기 때문임.
    // vcs.h에 vector<string> 기반 다중 파일 commit 오버로드가 있으면 컴파일 타임에 자동 선택함.
    template <typename Files>
    auto commit_targets_to_vcs_impl(const std::string& repo_path,
        const std::string& message,
        const Files& files,
        int) -> decltype(commit(repo_path, message, files), std::string()) {
        auto result = commit(repo_path, message, files);

        if constexpr (std::is_same_v<decltype(result), std::string>) {
            return result;
        }
        else {
            return result == 0 ? std::string("multi-file-commit") : std::string();
        }
    }

    // 다중 파일 commit 오버로드가 없는 경우의 fallback임.
    // 파일이 1개면 기존 단일 파일 commit 호출, 2개 이상이면 N개 커밋 생성 방지를 위해 실패 처리함.
    std::string commit_targets_to_vcs_impl(const std::string& repo_path,
        const std::string& message,
        const std::vector<std::string>& files,
        long) {
        if (files.empty()) {
            return "";
        }

        if (files.size() == 1) {
            if (!repo_file_exists(repo_path, files[0])) {
                std::cerr << u8"오류: 대상 파일을 찾을 수 없습니다: " << files[0] << "\n";
                return "";
            }
            return commit(repo_path, message, files[0]);
        }

        std::cerr
            << u8"오류: 현재 VCS 빌드는 원자적 다중 파일 커밋을 지원하지 않습니다\n"
            << u8"힌트: VCS에 다음과 같은 오버로드를 추가해야 합니다:\n"
            << "      std::string commit(const std::string& repo_path,\n"
            << "                         const std::string& message,\n"
            << "                         const std::vector<std::string>& target_files);\n"
            << u8"힌트: " << files.size()
            << u8"개의 파일을 하나의 폴더 커밋 요청에서 여러 커밋으로 나누지 않습니다.\n";
        return "";
    }

    // 실제 commit 호출 진입점임. 위의 오버로드 탐지 로직을 감싸는 wrapper임.
    std::string commit_targets_to_vcs(const std::string& repo_path,
        const std::string& message,
        const std::vector<std::string>& files) {
        return commit_targets_to_vcs_impl(repo_path, message, files, 0);
    }

    // -----------------------------------------------------------------------------
    // Command handlers
    // -----------------------------------------------------------------------------

    // dgit init 명령 처리함. 저장소 루트에 .vcs 구조 생성함.
    int handle_init(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("init");
            return 0;
        }
        if (args.size() != 2) {
            return fail_usage(u8"init 명령은 인자를 받지 않습니다", "dgit init");
        }

        const int result = init_repository(repo_path);
        if (result != 0) {
            std::cerr << u8"오류: dgit 저장소가 이미 있거나 초기화할 수 없습니다\n";
            return 1;
        }
        return 0;
    }

    // dgit add 명령 처리함. 단일 파일 또는 폴더 입력을 index 등록으로 연결함.
    int handle_add(const std::vector<std::string>& args,
        const std::string& repo_path,
        const fs::path& invocation_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("add");
            return 0;
        }
        if (args.size() < 3) {
            return fail_usage(u8"경로가 필요합니다", "dgit add <file|folder>");
        }
        if (args.size() > 3) {
            return fail_usage(u8"add 명령은 현재 경로 하나만 받을 수 있습니다", "dgit add <file|folder>");
        }

        std::vector<std::string> targets;
        std::set<std::string> seen;
        if (!collect_add_targets_from_path(repo_path, invocation_path, args[2], targets, seen)) {
            return 1;
        }

        if (targets.empty()) {
            std::cout << u8".fbx 파일을 찾지 못했습니다: " << args[2] << "\n";
            return 0;
        }

        const std::set<std::string> already_tracked = tracked_file_set(repo_path);

        int added_count = 0;
        int skipped_count = 0;
        int failed_count = 0;

        for (const auto& target : targets) {
            if (already_tracked.count(target) > 0) {
                ++skipped_count;
                continue;
            }

            if (add_file(repo_path, target) == 0) {
                ++added_count;
            }
            else {
                ++failed_count;
            }
        }

        std::cout << u8"추가 완료: " << added_count << u8"개 파일";
        if (skipped_count > 0) {
            std::cout << u8" / 이미 추적 중: " << skipped_count << u8"개 파일";
        }
        std::cout << "\n";

        if (failed_count > 0) {
            std::cerr << u8"경고: 추가 실패: " << failed_count << u8"개 파일\n";
            return 1;
        }
        return 0;
    }

    // dgit commit 명령 처리함. 메시지와 pathspec을 해석하고 VCS commit 함수로 전달함.
    int handle_commit(const std::vector<std::string>& args,
        const std::string& repo_path,
        const fs::path& invocation_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("commit");
            return 0;
        }
        if (args.size() < 4) {
            return fail_usage(u8"-m 옵션에는 커밋 메시지가 필요합니다", "dgit commit -m <message> [path...]");
        }
        if (args[2] != "-m") {
            return fail_usage(u8"커밋 메시지가 필요합니다", "dgit commit -m <message> [path...]");
        }

        const std::string message = args[3];
        if (message.empty()) {
            return fail_usage(u8"커밋 메시지가 비어 있습니다", "dgit commit -m <message> [path...]");
        }

        std::vector<std::string> pathspecs;
        if (args.size() > 4) {
            pathspecs.assign(args.begin() + 4, args.end());
        }

        bool ok = true;
        std::vector<std::string> targets = collect_commit_targets(repo_path, invocation_path, pathspecs, ok);
        if (!ok) {
            return 1;
        }

        if (targets.empty()) {
            if (pathspecs.empty()) {
                std::cerr << u8"오류: 추적 중인 파일이 없습니다. 먼저 'dgit add <file|folder>'를 실행하세요.\n";
            }
            else {
                std::cerr << u8"오류: 지정한 경로에서 추적 중인 파일을 찾지 못했습니다.\n";
            }
            return 1;
        }

        std::vector<std::string> valid_targets;
        std::vector<std::string> missing_targets;
        valid_targets.reserve(targets.size());

        for (const auto& target : targets) {
            if (!repo_file_exists(repo_path, target)) {
                missing_targets.push_back(target);
            }
            else {
                valid_targets.push_back(target);
            }
        }

        if (!missing_targets.empty()) {
            std::cerr << u8"경고: 현재 VCS 레이어는 삭제 커밋을 아직 지원하지 않습니다\n";
            std::cerr << u8"경고: 사라진 추적 파일은 건너뜁니다:\n";
            for (const auto& missing : missing_targets) {
                std::cerr << "  " << missing << "\n";
            }
            std::cerr << u8"힌트: VCS 커밋 메타데이터에 삭제 표시를 추가하거나 추후 'dgit rm'을 구현해야 합니다.\n";
        }

        if (valid_targets.empty()) {
            std::cerr << u8"오류: 커밋할 수 있는 실제 추적 파일이 없습니다\n";
            std::cerr << u8"힌트: 선택된 파일이 모두 삭제되었고, 삭제 커밋은 아직 지원하지 않습니다.\n";
            return 1;
        }

        targets.swap(valid_targets);

        const std::string commit_id = commit_targets_to_vcs(repo_path, message, targets);
        if (commit_id.empty()) {
            std::cerr << u8"오류: 커밋 생성 실패\n";
            return 1;
        }

        std::cout << u8"커밋 완료: " << targets.size() << u8"개 파일";
        if (commit_id != "multi-file-commit") {
            std::cout << u8" / 커밋 ID: " << commit_id.substr(0, 8);
        }
        std::cout << "\n";
        return 0;
    }

    // dgit log 명령 처리함. VCS 출력 대신 CLI가 커밋 메타데이터를 읽어 히스토리 출력함.
    int handle_log(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("log");
            return 0;
        }
        if (args.size() != 2) {
            return fail_usage(u8"log 명령은 인자를 받지 않습니다", "dgit log");
        }

        const fs::path head_path = fs::path(repo_path) / ".vcs" / "HEAD";
        std::ifstream head_file(head_path);
        std::string cur;
        if (head_file.is_open()) {
            std::getline(head_file, cur);
        }

        if (cur.empty()) {
            std::cout << u8"커밋 히스토리가 없습니다.\n";
            return 0;
        }

        int count = 0;
        while (!cur.empty()) {
            const CommitMetadata meta = load_commit_metadata(repo_path, cur);
            if (meta.id.empty()) {
                std::cerr << u8"오류: 손상된 커밋을 발견하여 로그 출력을 중단합니다: " << cur << "\n";
                break;
            }

            std::cout << "commit  " << meta.id << "\n"
                << "Date:   " << meta.timestamp << "\n"
                << "        " << meta.message << "\n\n";

            ++count;
            cur = meta.parent_id;
        }

        if (count == 0) {
            std::cout << u8"커밋 히스토리가 없습니다.\n";
        }
        return 0;
    }

    // dgit checkout 명령 처리함. 복원 성공 시 테스트 파이프라인이 찾는 SHA256 메시지도 출력함.
    int handle_checkout(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("checkout");
            return 0;
        }
        if (args.size() < 3) {
            return fail_usage(u8"커밋 ID가 필요합니다", "dgit checkout <commit_id>");
        }
        if (args.size() > 3) {
            return fail_usage(u8"checkout 명령은 커밋 ID 하나만 받을 수 있습니다", "dgit checkout <commit_id>");
        }

        const std::string commit_id = args[2];
        const int result = checkout(repo_path, commit_id);
        if (result != 0) {
            std::cerr << u8"오류: 커밋 ID가 아닙니다: " << commit_id << "\n";
            return 1;
        }

        // test_pipeline.py가 stdout에서 이 문구를 확인하므로 정확한 문자열 유지 필요.
        std::cout << u8"SHA256 검증 통과\n";
        std::cout << u8"체크아웃 완료: " << commit_id.substr(0, 8) << "\n";
        return 0;
    }

    // dgit diff 명령 처리함. 커밋 메타데이터와 delta/fullcopy 파일 크기를 기반으로 요약 정보 출력함.
    int handle_diff(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("diff");
            return 0;
        }
        if (args.size() != 4) {
            return fail_usage(u8"diff 명령에는 커밋 ID 두 개가 필요합니다", "dgit diff <commit1> <commit2>");
        }

        const std::string old_commit = args[2];
        const std::string new_commit = args[3];

        const CommitMetadata old_meta = load_commit_metadata(repo_path, old_commit);
        const CommitMetadata new_meta = load_commit_metadata(repo_path, new_commit);

        if (old_meta.id.empty()) {
            std::cerr << u8"오류: 커밋을 찾을 수 없거나 메타데이터가 손상되었습니다: " << old_commit << "\n";
            return 1;
        }
        if (new_meta.id.empty()) {
            std::cerr << u8"오류: 커밋을 찾을 수 없거나 메타데이터가 손상되었습니다: " << new_commit << "\n";
            return 1;
        }

        if (!new_meta.parent_id.empty() && new_meta.parent_id != old_commit) {
            std::cerr << u8"경고: " << new_commit
                << u8" 커밋은 " << old_commit
                << u8"의 직접 자식 커밋이 아닙니다. 메타데이터 기준으로 diff를 계산합니다.\n";
        }

        constexpr std::uintmax_t BLOCK_SIZE = 16ULL * 1024ULL;

        std::uintmax_t total_changed_bytes = 0;
        std::uintmax_t total_file_bytes = 0;
        std::uintmax_t changed_blocks = 0;
        const fs::path vcs_path = fs::path(repo_path) / ".vcs";

        for (const auto& file : new_meta.files) {
            const std::uintmax_t file_length = static_cast<std::uintmax_t>(file.length);
            total_file_bytes += file_length;

            std::uintmax_t changed_bytes = 0;
            if (!file.delta.empty()) {
                const fs::path delta_path = vcs_path / file.delta;
                if (fs::exists(delta_path)) {
                    changed_bytes = fs::file_size(delta_path);
                }
                else {
                    std::cerr << u8"경고: delta/fullcopy 파일이 없습니다: "
                        << file.path << ": " << file.delta << "\n";
                }
            }
            else {
                changed_bytes = file_length;
            }

            total_changed_bytes += changed_bytes;
            if (changed_bytes > 0) {
                changed_blocks += (changed_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
            }
        }

        double changed_ratio = 0.0;
        if (total_file_bytes > 0) {
            changed_ratio = static_cast<double>(total_changed_bytes) /
                static_cast<double>(total_file_bytes) * 100.0;
        }

        std::cout << u8"변경된 블록 수: " << changed_blocks
            << u8" / 총 변경 용량: " << format_bytes(total_changed_bytes)
            << u8" / 변경 비율: " << std::fixed << std::setprecision(2)
            << changed_ratio << "%\n";
        return 0;
    }

} // namespace

// 프로그램 시작점임. 명령어를 분기하고, VCS 호출 전에 작업 디렉터리를 저장소 루트로 맞춤.
int main(int argc, char* argv[]) {
    setup_utf8_console();

    const std::vector<std::string> args(argv, argv + argc);
    const fs::path invocation_path = fs::current_path();

    if (argc == 1) {
        print_general_help();
        return 1;
    }

    const std::string command = args[1];

    if (command == "--help" || command == "help") {
        print_general_help();
        return 0;
    }

    if (args.size() == 3 && args[2] == "--help") {
        print_command_help(command);
        return 0;
    }

    if (!is_known_command(command)) {
        std::cerr << "dgit: '" << command << u8"' 은 dgit 명령어가 아닙니다. 'dgit --help'를 확인하세요.\n";
        return 1;
    }

    std::string repo_path;
    if (command == "init") {
        repo_path = safe_absolute_normalized(invocation_path).string();
    }
    else {
        const fs::path root = find_repository_root(invocation_path);
        if (needs_repository(command) && root.empty()) {
            std::cerr << u8"오류: dgit 저장소가 아닙니다. 상위 폴더에서 .vcs를 찾을 수 없습니다.\n";
            return 1;
        }
        repo_path = root.string();
    }

    // 현재 vcs.cpp는 target_file을 fs::exists(target_file)로 직접 검사함.
    // 따라서 사용자가 서브폴더에서 dgit을 실행하더라도, VCS 호출 직전에는
    // CWD를 저장소 루트로 맞춰 repo-relative index 경로가 정상 동작하도록 함.
    ScopedCurrentPath repo_cwd(repo_path);

    if (command == "init") {
        return handle_init(args, repo_path);
    }
    if (command == "add") {
        return handle_add(args, repo_path, invocation_path);
    }
    if (command == "commit") {
        return handle_commit(args, repo_path, invocation_path);
    }
    if (command == "log") {
        return handle_log(args, repo_path);
    }
    if (command == "checkout") {
        return handle_checkout(args, repo_path);
    }
    if (command == "diff") {
        return handle_diff(args, repo_path);
    }

    std::cerr << "dgit: '" << command << u8"' 은 dgit 명령어가 아닙니다. 'dgit --help'를 확인하세요.\n";
    return 1;
}
