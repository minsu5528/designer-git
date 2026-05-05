#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// VCS 엔진 계층의 함수들을 사용하기 위한 헤더 파일 포함
#include "../vcs/vcs.h"

#include "../engine/format.h"

namespace fs = std::filesystem;

namespace {

    // 현재 작업 중인 디렉토리의 절대 경로를 반환하는 함수
    std::string current_repo_path() {
        return fs::current_path().string();
    }

    // 지정된 경로에 ".vcs" 폴더가 존재하는지 확인하여 dgit 저장소 여부를 판별하는 함수
    bool is_dgit_repository(const std::string& repo_path) {
        return fs::exists(fs::path(repo_path) / ".vcs");
    }

    // 사용자가 'dgit'만 입력하거나 '--help'를 입력했을 때 출력되는 전체 도움말
    void print_general_help() {
        std::cout
            << "usage: dgit <command> [<args>]\n\n"
            << "These are common dgit commands:\n\n"
            << "  init                         Create an empty dgit repository\n"
            << "  add <file>                   Add one file to the index\n"
            << "  commit -m <message>          Record changes to the repository\n"
            << "  log                          Show commit logs\n"
            << "  checkout <commit_id>         Restore files from a commit\n"
            << "  diff <commit1> <commit2>     Show changes between two commits\n\n"
            << "Use 'dgit <command> --help' for more information on a command.\n";
    }

    // 특정 명령어(예: 'dgit commit --help')에 대한 상세 도움말을 출력하는 함수
    void print_command_help(const std::string& command) {
        if (command == "init") {
            std::cout << "usage: dgit init\n\n"
                << "Create an empty dgit repository in the current directory.\n";
        }
        else if (command == "add") {
            std::cout << "usage: dgit add <file>\n\n"
                << "Add one file to the index.\n";
        }
        else if (command == "commit") {
            std::cout << "usage: dgit commit -m <message>\n\n"
                << "Record changes to the repository.\n";
        }
        else if (command == "log") {
            std::cout << "usage: dgit log\n\n"
                << "Show commit logs.\n";
        }
        else if (command == "checkout") {
            std::cout << "usage: dgit checkout <commit_id>\n\n"
                << "Restore files from a commit.\n";
        }
        else if (command == "diff") {
            std::cout << "usage: dgit diff <commit1> <commit2>\n\n"
                << "Show changes between two commits.\n";
        }
        else {
            std::cerr << "dgit: unknown help topic '" << command << "'\n";
        }
    }

    // 해당 명령어가 실행되기 전에 저장소(.vcs 폴더)가 반드시 존재해야 하는지 여부를 반환
    bool needs_repository(const std::string& command) {
        return command == "add" || command == "commit" || command == "log" ||
            command == "checkout" || command == "diff";
    }

    // 잘못된 명령어 인자가 들어왔을 때 에러 메시지와 올바른 사용법을 출력하고 에러 코드(1)를 반환
    int fail_usage(const std::string& message, const std::string& usage) {
        std::cerr << "dgit: " << message << "\n";
        std::cerr << "usage: " << usage << "\n";
        return 1;
    }

    // "commit", "-m", "hello", "world" 처럼 분리된 인자 배열을 
    // "hello world" 하나의 띄어쓰기가 포함된 문자열로 합쳐주는 유틸리티 함수
    std::string join_args(const std::vector<std::string>& args, std::size_t start) {
        std::string result;
        for (std::size_t i = start; i < args.size(); ++i) {
            if (!result.empty()) {
                result += " ";
            }
            result += args[i];
        }
        return result;
    }

    // .vcs/index 파일을 읽어 첫 번째로 추적 중인 파일의 이름을 반환하는 함수
    std::string read_tracked_file(const std::string& repo_path) {
        const fs::path index_path = fs::path(repo_path) / ".vcs" / "index";
        std::ifstream index_file(index_path);

        if (!index_file.is_open()) {
            return "";
        }

        std::string line;
        while (std::getline(index_file, line)) {
            // Windows 환경의 개행 문자(\r) 처리
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                return line;
            }
        }

        return "";
    }

    // 'dgit init' 명령어 처리
    int handle_init(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("init");
            return 0;
        }
        if (args.size() != 2) {
            return fail_usage("init takes no arguments", "dgit init");
        }

        // VCS 레이어의 초기화 함수 호출
        const int result = init_repository(repo_path);
        if (result != 0) {
            std::cerr << "fatal: dgit repository already exists or could not be initialized\n";
            return 1;
        }

        std::cout << "Initialized empty dgit repository in .vcs/\n";
        return 0;
    }

    // 'dgit add <file>' 명령어 처리
    int handle_add(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("add");
            return 0;
        }
        if (args.size() < 3) {
            return fail_usage("missing pathspec", "dgit add <file>");
        }
        if (args.size() > 3) {
            return fail_usage("add currently accepts exactly one file", "dgit add <file>");
        }

        const std::string filepath = args[2];
        const fs::path file_path(filepath);

        // 파일 존재 여부 검사
        if (!fs::exists(file_path)) {
            std::cerr << "fatal: pathspec '" << filepath << "' did not match any files\n";
            return 1;
        }
        // 폴더를 추가하려고 시도하는 경우 에러 처리 (현재 MVP 범위 아님)
        if (fs::is_directory(file_path)) {
            std::cerr << "fatal: pathspec '" << filepath
                << "' is a directory; this MVP accepts exactly one file\n";
            return 1;
        }

        // VCS 레이어의 add_file 함수 호출
        const int result = add_file(repo_path, filepath);
        if (result != 0) {
            std::cerr << "fatal: failed to add '" << filepath << "'\n";
            return 1;
        }

        std::cout << "Added: " << filepath << "\n";
        return 0;
    }

    // 'dgit commit -m "message"' 명령어 처리
    int handle_commit(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("commit");
            return 0;
        }
        // 필수 옵션 검사
        if (args.size() < 4) {
            return fail_usage("switch 'm' requires a value", "dgit commit -m <message>");
        }
        if (args[2] != "-m") {
            return fail_usage("commit message is required", "dgit commit -m <message>");
        }

        const std::string message = join_args(args, 3);
        if (message.empty()) {
            return fail_usage("empty commit message", "dgit commit -m <message>");
        }

        // 추적 중인 파일이 있는지 확인
        const std::string target_file = read_tracked_file(repo_path);
        if (target_file.empty()) {
            std::cerr << "fatal: no tracked files. Use 'dgit add <file>' first.\n";
            return 1;
        }

        // 압축 포맷 여부 출력
        if (is_compressed_format(target_file))
        {
            std::cout << "note: compressed format detected, storing full copy\n";
        }
        else
        {
            std::cout << "note: binary delta will be created\n";
        }   

        // VCS 레이어의 commit 함수 호출 및 결과(commit_id) 반환
        const std::string commit_id = commit(repo_path, message, target_file);
        if (commit_id.empty()) {
            std::cerr << "fatal: failed to create commit\n";
            return 1;
        }

        // 성공 시 짧은 해시값(앞 8자리)과 메시지 출력
        std::cout << "[dgit " << commit_id.substr(0, 8) << "] " << message << "\n";
        return 0;
    }

    // 'dgit log' 명령어 처리
    int handle_log(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("log");
            return 0;
        }
        if (args.size() != 2) {
            return fail_usage("log takes no arguments", "dgit log");
        }

        // VCS 레이어의 log 함수 호출 (내부에서 자체적으로 출력 담당)
        log(repo_path);
        return 0;
    }

    // 'dgit checkout <commit_id>' 명령어 처리
    int handle_checkout(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("checkout");
            return 0;
        }
        if (args.size() < 3) {
            return fail_usage("missing commit ID", "dgit checkout <commit_id>");
        }
        if (args.size() > 3) {
            return fail_usage("checkout currently accepts exactly one commit ID", "dgit checkout <commit_id>");
        }

        const std::string commit_id = args[2];

        // VCS 레이어의 checkout 함수 호출
        const int result = checkout(repo_path, commit_id);
        if (result != 0) {
            std::cerr << "fatal: reference is not a commit: " << commit_id << "\n";
            return 1;
        }

        std::cout << "HEAD is now at " << commit_id.substr(0, 8) << "\n";
        return 0;
    }

    // 'dgit diff' 명령어 처리 (MVP 단계에서는 인터페이스만 제공)
    int handle_diff(const std::vector<std::string>& args) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("diff");
            return 0;
        }
        if (args.size() != 4) {
            return fail_usage("diff requires two commit IDs", "dgit diff <commit1> <commit2>");
        }

        std::cerr << "fatal: diff is not implemented yet\n";
        return 1;
    }

}  // namespace

// 프로그램의 메인 진입점
int main(int argc, char* argv[]) {
    // 운영체제가 전달한 인자 배열(argv)을 C++ 벡터(vector)로 변환하여 사용하기 편하게 만듦
    const std::vector<std::string> args(argv, argv + argc);

    // 인자가 아무것도 없이 'dgit'만 입력되었을 때
    if (argc == 1) {
        print_general_help();
        return 1;
    }

    // 첫 번째 인자(예: init, commit) 추출
    const std::string command = args[1];

    // 전체 도움말 요청 처리
    if (command == "--help" || command == "help") {
        print_general_help();
        return 0;
    }

    // 특정 명령어에 대한 도움말 요청 처리 (예: dgit add --help)
    if (args.size() == 3 && args[2] == "--help") {
        print_command_help(command);
        return 0;
    }

    const std::string repo_path = current_repo_path();

    // init 명령어를 제외하고는 항상 .vcs 폴더가 존재하는지 먼저 확인
    if (needs_repository(command) && !is_dgit_repository(repo_path)) {
        std::cerr << "fatal: not a dgit repository (or any of the parent directories): .vcs\n";
        return 1;
    }

    // 입력된 명령어에 따라 해당되는 핸들러 함수로 연결 (라우팅)
    if (command == "init") {
        return handle_init(args, repo_path);
    }
    if (command == "add") {
        return handle_add(args, repo_path);
    }
    if (command == "commit") {
        return handle_commit(args, repo_path);
    }
    if (command == "log") {
        return handle_log(args, repo_path);
    }
    if (command == "checkout") {
        return handle_checkout(args, repo_path);
    }
    if (command == "diff") {
        return handle_diff(args);
    }

    // 사전에 정의되지 않은 명령어가 입력되었을 때의 예외 처리
    std::cerr << "dgit: '" << command << "' is not a dgit command. See 'dgit --help'.\n";
    return 1;
}
