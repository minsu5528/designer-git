#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// VCS ���� ������ �Լ����� ����ϱ� ���� ��� ���� ����
#include "../vcs/vcs.h"

#include "../engine/format.h"

namespace fs = std::filesystem;

namespace {

    // ���� �۾� ���� ���丮�� ���� ��θ� ��ȯ�ϴ� �Լ�
    std::string current_repo_path() {
        return fs::current_path().string();
    }

    // ������ ��ο� ".vcs" ������ �����ϴ��� Ȯ���Ͽ� dgit ����� ���θ� �Ǻ��ϴ� �Լ�
    bool is_dgit_repository(const std::string& repo_path) {
        return fs::exists(fs::path(repo_path) / ".vcs");
    }

    // ����ڰ� 'dgit'�� �Է��ϰų� '--help'�� �Է����� �� ��µǴ� ��ü ����
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

    // Ư�� ���ɾ�(��: 'dgit commit --help')�� ���� �� ������ ����ϴ� �Լ�
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

    // �ش� ���ɾ ����Ǳ� ���� �����(.vcs ����)�� �ݵ�� �����ؾ� �ϴ��� ���θ� ��ȯ
    bool needs_repository(const std::string& command) {
        return command == "add" || command == "commit" || command == "log" ||
            command == "checkout" || command == "diff";
    }

    // �߸��� ���ɾ� ���ڰ� ������ �� ���� �޽����� �ùٸ� ������ ����ϰ� ���� �ڵ�(1)�� ��ȯ
    int fail_usage(const std::string& message, const std::string& usage) {
        std::cerr << "dgit: " << message << "\n";
        std::cerr << "usage: " << usage << "\n";
        return 1;
    }

    // "commit", "-m", "hello", "world" ó�� �и��� ���� �迭�� 
    // "hello world" �ϳ��� ���Ⱑ ���Ե� ���ڿ��� �����ִ� ��ƿ��Ƽ �Լ�
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

    // .vcs/index ������ �о� ù ��°�� ���� ���� ������ �̸��� ��ȯ�ϴ� �Լ�
    std::string read_tracked_file(const std::string& repo_path) {
        const fs::path index_path = fs::path(repo_path) / ".vcs" / "index";
        std::ifstream index_file(index_path);

        if (!index_file.is_open()) {
            return "";
        }

        std::string line;
        while (std::getline(index_file, line)) {
            // Windows ȯ���� ���� ����(\r) ó��
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                return line;
            }
        }

        return "";
    }

    // 'dgit init' ���ɾ� ó��
    int handle_init(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("init");
            return 0;
        }
        if (args.size() != 2) {
            return fail_usage("init takes no arguments", "dgit init");
        }

        // VCS ���̾��� �ʱ�ȭ �Լ� ȣ��
        const int result = init_repository(repo_path);
        if (result != 0) {
            std::cerr << "fatal: dgit repository already exists or could not be initialized\n";
            return 1;
        }

        std::cout << "Initialized empty dgit repository in .vcs/\n";
        return 0;
    }

    // 'dgit add <file>' ���ɾ� ó��
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

        // ���� ���� ���� �˻�
        if (!fs::exists(file_path)) {
            std::cerr << "fatal: pathspec '" << filepath << "' did not match any files\n";
            return 1;
        }
        // ������ �߰��Ϸ��� �õ��ϴ� ��� ���� ó�� (���� MVP ���� �ƴ�)
        if (fs::is_directory(file_path)) {
            std::cerr << "fatal: pathspec '" << filepath
                << "' is a directory; this MVP accepts exactly one file\n";
            return 1;
        }

        // VCS ���̾��� add_file �Լ� ȣ��
        const int result = add_file(repo_path, filepath);
        if (result != 0) {
            std::cerr << "fatal: failed to add '" << filepath << "'\n";
            return 1;
        }

        std::cout << "Added: " << filepath << "\n";
        return 0;
    }

    // 'dgit commit -m "message"' ���ɾ� ó��
    int handle_commit(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("commit");
            return 0;
        }
        // �ʼ� �ɼ� �˻�
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

        // ���� ���� ������ �ִ��� Ȯ��
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

        // VCS ���̾��� commit �Լ� ȣ�� �� ���(commit_id) ��ȯ
        const std::string commit_id = commit(repo_path, message, target_file);
        if (commit_id.empty()) {
            std::cerr << "fatal: failed to create commit\n";
            return 1;
        }

        // ���� �� ª�� �ؽð�(�� 8�ڸ�)�� �޽��� ���
        std::cout << "[dgit " << commit_id.substr(0, 8) << "] " << message << "\n";
        return 0;
    }

    // 'dgit log' ���ɾ� ó��
    int handle_log(const std::vector<std::string>& args, const std::string& repo_path) {
        if (args.size() == 3 && args[2] == "--help") {
            print_command_help("log");
            return 0;
        }
        if (args.size() != 2) {
            return fail_usage("log takes no arguments", "dgit log");
        }

        // VCS ���̾��� log �Լ� ȣ�� (���ο��� ��ü������ ��� ���)
        log(repo_path);
        return 0;
    }

    // 'dgit checkout <commit_id>' ���ɾ� ó��
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

        // VCS ���̾��� checkout �Լ� ȣ��
        const int result = checkout(repo_path, commit_id);
        if (result != 0) {
            std::cerr << "fatal: reference is not a commit: " << commit_id << "\n";
            return 1;
        }

        std::cout << "HEAD is now at " << commit_id.substr(0, 8) << "\n";
        return 0;
    }

    // 'dgit diff' ���ɾ� ó�� (MVP �ܰ迡���� �������̽��� ����)
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

// ���α׷��� ���� ������
int main(int argc, char* argv[]) {
    // �ü���� ������ ���� �迭(argv)�� C++ ����(vector)�� ��ȯ�Ͽ� ����ϱ� ���ϰ� ����
    const std::vector<std::string> args(argv, argv + argc);

    // ���ڰ� �ƹ��͵� ���� 'dgit'�� �ԷµǾ��� ��
    if (argc == 1) {
        print_general_help();
        return 1;
    }

    // ù ��° ����(��: init, commit) ����
    const std::string command = args[1];

    // ��ü ���� ��û ó��
    if (command == "--help" || command == "help") {
        print_general_help();
        return 0;
    }

    // Ư�� ���ɾ ���� ���� ��û ó�� (��: dgit add --help)
    if (args.size() == 3 && args[2] == "--help") {
        print_command_help(command);
        return 0;
    }

    const std::string repo_path = current_repo_path();

    // init ���ɾ �����ϰ��� �׻� .vcs ������ �����ϴ��� ���� Ȯ��
    if (needs_repository(command) && !is_dgit_repository(repo_path)) {
        std::cerr << "fatal: not a dgit repository (or any of the parent directories): .vcs\n";
        return 1;
    }

    // �Էµ� ���ɾ ���� �ش�Ǵ� �ڵ鷯 �Լ��� ���� (�����)
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

    // ������ ���ǵ��� ���� ���ɾ �ԷµǾ��� ���� ���� ó��
    std::cerr << "dgit: '" << command << "' is not a dgit command. See 'dgit --help'.\n";
    return 1;
}