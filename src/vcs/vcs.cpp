#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>
#include "json.hpp"
#include "vcs.h"
#include "../engine/delta.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

// .vcs 폴더 구조 및 초기 설정 파일을 생성하는 함수
int init_repository(const std::string &path)
{
    fs::path vcs = fs::path(path) / ".vcs";

    if (fs::exists(vcs))
    {
        std::cerr << "이미 dgit 저장소가 존재합니다.\n";
        return -1;
    }

    try
    {
        // 1. 필수 폴더 구조 생성
        fs::create_directories(vcs / "objects" / "base");
        fs::create_directories(vcs / "objects" / "deltas");
        fs::create_directories(vcs / "commits");

        // 2. index 파일 생성 (현재 추적 중인 파일 목록)
        std::ofstream(vcs / "index").close();

        // 3. HEAD 파일 생성 (현재 체크아웃된 커밋 ID 저장)
        // 변수 방식을 사용하여 설계 문서의 초기값("")을 명시적으로 기록
        std::ofstream head_file(vcs / "HEAD");
        if (head_file.is_open())
        {
            head_file << ""; // 초기값은 빈 문자열
            head_file.close();
        }

        std::cout << "dgit 저장소가 초기화되었습니다: " << vcs << "\n";
        return 0;
    }
    catch (const fs::filesystem_error &e)
    {
        std::cerr << "저장소 초기화 중 오류 발생: " << e.what() << "\n";
        return -1;
    }
}

// 커밋 메타데이터를 .vcs/commits/[commit_id].json 형식으로 JSON 파일로 저장하는 함수
int save_commit_metadata(const std::string &repo_path, const CommitMetadata &meta)
{
    fs::path commit_dir = fs::path(repo_path) / ".vcs" / "commits";
    fs::path file_path = commit_dir / (meta.id + ".json");

    json j;
    j["id"] = meta.id;
    j["message"] = meta.message;
    j["timestamp"] = meta.timestamp;
    j["parent_id"] = meta.parent_id;

    j["files"] = json::array();
    for (const auto &f : meta.files)
    {
        j["files"].push_back({{"path", f.path},
                              {"delta", f.delta},
                              {"is_base", f.is_base}});
    }

    try
    {
        std::ofstream file(file_path);
        if (!file.is_open())
            return -1;
        file << j.dump(4); // 들여쓰기 4칸 적용
        file.close();
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

//  저장된 커밋 JSON을 읽어오는 함수
CommitMetadata load_commit_metadata(const std::string &repo_path, const std::string &commit_id)
{
    fs::path file_path = fs::path(repo_path) / ".vcs" / "commits" / (commit_id + ".json");

    std::ifstream file(file_path);
    if (!file.is_open())
        return {};

    json j;
    file >> j;

    CommitMetadata meta;
    meta.id = j["id"];
    meta.message = j["message"];
    meta.timestamp = j["timestamp"];
    meta.parent_id = j["parent_id"];

    for (const auto &item : j["files"])
    {
        meta.files.push_back({item["path"].get<std::string>(),
                              item["delta"].get<std::string>(),
                              item.at("is_base").get<bool>()});
    }

    return meta;
}

// 현재 시각을 ISO 8601 문자열로 변환하는 헬퍼 함수
std::string get_current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// 파일의 델타를 생성하고 메타데이터를 JSON으로 저장하는 함수
std::string commit(const std::string &repo_path, const std::string &message, const std::string &target_file)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    // 1. 커밋 ID 생성을 위한 소스 데이터 준비
    std::string timestamp = get_current_timestamp();
    std::string parent_id = "";

    std::ifstream head_in(vcs_path / "HEAD");
    std::getline(head_in, parent_id);
    head_in.close();

    // 메시지, 시각, 부모ID를 섞어 고유한 문자열(해시) 생성
    std::string seed = message + timestamp + parent_id;
    std::hash<std::string> hasher;
    size_t hash_value = hasher(seed);

    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash_value;
    std::string commit_id = ss.str(); // 예: 7af32b8e12d45678

    // 2. 델타 파일 경로 설정
    std::string delta_filename = commit_id + ".delta";
    fs::path delta_path = vcs_path / "objects" / "deltas" / delta_filename;

    // 3. delta_create 호출 (이전 버전이 있다고 가정 - 실제론 HEAD에서 추적해야 함)
    // 여기서는 base 폴더의 원본과 현재 파일을 비교한다고 가정
    fs::path base_file = vcs_path / "objects" / "base" / "original_file";

    if (fs::exists(base_file))
    {
        if (delta_create(base_file.string().c_str(), target_file.c_str(), delta_path.string().c_str()) != 0)
        {
            return ""; // 델타 생성 실패 시 빈 문자열 반환
        }
    }

    // 4. JSON 데이터 구성
    CommitMetadata meta;
    meta.id = commit_id;
    meta.message = message;
    meta.timestamp = get_current_timestamp();

    // HEAD 파일에서 부모 ID 읽어오기
    std::ifstream head_in(vcs_path / "HEAD");
    std::getline(head_in, meta.parent_id);
    head_in.close();

    meta.files.push_back({target_file, "objects/deltas/" + delta_filename, false});

    // 5. JSON 저장
    if (save_commit_metadata(repo_path, meta) == 0)
    {
        // 성공 시 HEAD 업데이트
        std::ofstream head_out(vcs_path / "HEAD");
        head_out << commit_id;
        head_out.close();
        return commit_id;
    }

    return "";
}