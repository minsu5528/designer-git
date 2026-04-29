#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include <openssl/sha.h>
#include "json.hpp"
#include "vcs.h"
#include "../engine/delta.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

//  내부 헬퍼 함수
// 현재 시각을 ISO 8601 문자열로 변환
std::string get_current_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// 파일 전체를 읽어 SHA256 hex 문자열로 반환. 실패 시 빈 문자열.
static std::string sha256_file(const fs::path &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return "";

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
        SHA256_Update(&ctx, buf, static_cast<size_t>(f.gcount()));

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    return oss.str();
}

// HEAD 파일에서 현재 커밋 ID 읽기 (없으면 빈 문자열)
static std::string read_head(const fs::path &vcs_path)
{
    std::string id;
    std::ifstream f(vcs_path / "HEAD");
    if (f.is_open())
        std::getline(f, id);
    return id;
}

// HEAD 파일에 커밋 ID 쓰기
static void write_head(const fs::path &vcs_path, const std::string &commit_id)
{
    std::ofstream f(vcs_path / "HEAD");
    f << commit_id;
}

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
        fs::create_directories(vcs / "objects" / "snapshots");
        fs::create_directories(vcs / "commits");

        // 2. index 파일 생성 (현재 추적 중인 파일 목록)
        std::ofstream(vcs / "index").close();

        // 3. HEAD 파일 생성 (현재 체크아웃된 커밋 ID 저장)
        write_head(vcs, "");

        std::cout << "dgit 저장소가 초기화되었습니다: " << vcs << "\n";
        return 0;
    }
    catch (const fs::filesystem_error &e)
    {
        std::cerr << "저장소 초기화 중 오류 발생: " << e.what() << "\n";
        return -1;
    }
}

// index에 파일 경로 등록-------
int add_file(const std::string &repo_path, const std::string &filepath)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        std::cerr << "dgit 저장소가 없습니다. 먼저 dgit init을 실행하세요.\n";
        return -1;
    }

    if (!fs::exists(filepath))
    {
        std::cerr << "파일을 찾을 수 없습니다: " << filepath << "\n";
        return -1;
    }

    fs::path index_path = vcs_path / "index";

    // 이미 등록된 경로인지 확인 (중복 방지)
    {
        std::ifstream idx(index_path);
        std::string line;
        while (std::getline(idx, line))
        {
            if (line == filepath)
            {
                std::cout << "이미 추적 중인 파일입니다: " << filepath << "\n";
                return 0;
            }
        }
    }

    // 새 경로 추가 (append)
    std::ofstream idx(index_path, std::ios::app);
    if (!idx.is_open())
    {
        std::cerr << "index 파일을 열 수 없습니다.\n";
        return -1;
    }
    idx << filepath << "\n";

    std::cout << "추적 등록: " << filepath << "\n";
    return 0;
}

// 커밋 메타데이터 저장/읽기 --------
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
    j["sha256"] = meta.sha256;

    j["files"] = json::array();
    for (const auto &f : meta.files)
    {
        j["files"].push_back({{"path", f.path},
                              {"delta", f.delta},
                              {"is_base", f.is_base},
                              {"sha256", f.sha256},
                              {"length",f.length}});
    }

    try
    {
        std::ofstream file(file_path);
        if (!file.is_open())
            return -1;
        file << j.dump(4); // 들여쓰기 4칸 적용
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
    {
        std::cerr << "커밋을 찾을 수 없습니다: " << commit_id << "\n";
        return {};
    }

    try
    {
        json j;
        file >> j;

        CommitMetadata meta;
        meta.id = j.at("id").get<std::string>();
        meta.message = j.at("message").get<std::string>();
        meta.timestamp = j.at("timestamp").get<std::string>();
        meta.parent_id = j.at("parent_id").get<std::string>();
        meta.sha256 = j.value("sha256", "");

        for (const auto &item : j.at("files"))
        {
            FileEntry fe;
            fe.path = item.at("path").get<std::string>();
            fe.delta = item.at("delta").get<std::string>();
            fe.is_base = item.at("is_base").get<bool>();
            fe.sha256 = item.value("sha256", "");
            fe.length  = item.value("length", uint64_t(0));
            meta.files.push_back(fe);
        }

        return meta;
    }
    catch (const json::exception &e)
    {
        // 손상된 메타데이터 예외 처리
        std::cerr << "메타데이터 파싱 오류 (" << commit_id << "): "
                  << e.what() << "\n";
        return {};
    }
}

// commit: 파일의 델타를 생성 및 메타데이터를 JSON으로 저장----------
std::string commit(const std::string &repo_path, const std::string &message, const std::string &target_file)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        std::cerr << "dgit 저장소가 없습니다. 먼저 dgit init을 실행하세요.\n";
        return "";
    }
    if (!fs::exists(target_file))
    {
        std::cerr << "대상 파일을 찾을 수 없습니다: " << target_file << "\n";
        return "";
    }

    // 1. 타임스탬프 & 부모 ID
    std::string timestamp = get_current_timestamp();
    std::string parent_id = read_head(vcs_path);

    // 2. 커밋 ID(메시지+시각+부모ID 해시)
    std::string seed = message + timestamp + parent_id;
    std::hash<std::string> hasher;
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hasher(seed);
    std::string commit_id = ss.str();

    // 3. 파일 SHA256
    std::string file_sha256 = sha256_file(target_file);

    CommitMetadata meta;
    meta.id = commit_id;
    meta.message = message;
    meta.timestamp = timestamp;
    meta.parent_id = parent_id;
    meta.sha256 = file_sha256;

    // 4. base 없으면 최초 커밋: 파일을 base에 복사
    fs::path base_file = vcs_path / "objects" / "base" /
                         fs::path(target_file).filename();

    if (!fs::exists(base_file))
    {
        fs::copy_file(target_file, base_file,
                      fs::copy_options::overwrite_existing);
        meta.files.push_back({target_file, "", true, file_sha256, 0});
    }
    else
    {
        // 5. delta 생성
        std::string delta_filename = commit_id + ".delta";
        fs::path delta_path = vcs_path / "objects" / "deltas" / delta_filename;

        if (delta_create(base_file.string().c_str(),
                         target_file.c_str(),
                         delta_path.string().c_str()) != 0)
        {
            std::cerr << "delta 생성 실패\n";
            return "";
        }
     
        // delta 커밋
        uint64_t file_size = static_cast<uint64_t>(fs::file_size(target_file));
        meta.files.push_back(
            {target_file, "objects/deltas/" + delta_filename, false, file_sha256, file_size});
    }

    // 6. JSON 저장 & HEAD 업데이트
    if (save_commit_metadata(repo_path, meta) != 0)
    {
        std::cerr << "커밋 메타데이터 저장 실패\n";
        return "";
    }

    write_head(vcs_path, commit_id);
    std::cout << "[" << commit_id.substr(0, 8) << "] " << message << "\n";
    return commit_id;
}


// checkout: 특정 시점으로 파일 복원--------
int checkout(const std::string &repo_path, const std::string &commit_id)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        std::cerr << "dgit 저장소가 없습니다.\n";
        return -1;
    }

    // 1. 목표 커밋 메타데이터 로드
    CommitMetadata target = load_commit_metadata(repo_path, commit_id);
    if (target.id.empty())
    {
        std::cerr << "존재하지 않는 커밋 ID입니다: " << commit_id << "\n";
        return -1;
    }

    // 2. base → 목표 커밋까지 delta 체인 구성 (parent_id 역추적)
    //    chain[0] = base 커밋, chain[back] = 목표 커밋
    std::vector<std::string> chain;
    {
        std::string cur = commit_id;
        while (!cur.empty())
        {
            CommitMetadata m = load_commit_metadata(repo_path, cur);
            if (m.id.empty())
            {
                std::cerr << "체인 순회 중 손상된 메타데이터: " << cur << "\n";
                return -1;
            }
            chain.push_back(cur);
            // base 커밋(is_base == true)에 도달하면 중단
            if (!m.files.empty() && m.files[0].is_base)
                break;
            cur = m.parent_id;
        }
        std::reverse(chain.begin(), chain.end()); // 오래된 순서로 정렬
    }

    // 3. 파일별 복원
    for (const auto &fe : target.files)
    {
        fs::path base_file = vcs_path / "objects" / "base" /
                             fs::path(fe.path).filename();

        if (!fs::exists(base_file))
        {
            std::cerr << "base 파일 없음: " << base_file << "\n";
            return -1;
        }

        // 최초 커밋(is_base) checkout: base 파일 그대로 복사
        if (fe.is_base)
        {
            fs::copy_file(base_file, fe.path,
                          fs::copy_options::overwrite_existing);
        }
        else
        {
            // delta 체인 순차 적용
            // tmp_a: 현재 복원 중간 결과, tmp_b: delta 적용 후 출력
            fs::path tmp_a = vcs_path / ("tmp_a_" + commit_id);
            fs::path tmp_b = vcs_path / ("tmp_b_" + commit_id);

            fs::copy_file(base_file, tmp_a,
                          fs::copy_options::overwrite_existing);

            // chain[0]은 base 커밋(delta 없음), chain[1]부터 delta 적용
            for (size_t i = 1; i < chain.size(); ++i)
            {
                CommitMetadata cm = load_commit_metadata(repo_path, chain[i]);
                if (cm.id.empty())
                {
                    std::cerr << "체인 복원 중 손상된 메타데이터: "
                              << chain[i] << "\n";
                    fs::remove(tmp_a);
                    return -1;
                }

                // 같은 파일명의 delta 경로 탐색
                std::string delta_rel;
                for (const auto &entry : cm.files)
                {
                    if (fs::path(entry.path).filename() ==
                        fs::path(fe.path).filename())
                    {
                        delta_rel = entry.delta;
                        break;
                    }
                }

                if (delta_rel.empty())
                    continue; // 이 커밋에서 해당 파일 변경 없음

                fs::path delta_path = vcs_path / delta_rel;
                if (!fs::exists(delta_path))
                {
                    std::cerr << "delta 파일 없음: " << delta_path << "\n";
                    fs::remove(tmp_a);
                    return -1;
                }

                // delta 적용: tmp_a(이전) + delta → tmp_b(다음)
                if (delta_apply(tmp_a.string().c_str(),
                                delta_path.string().c_str(),
                                tmp_b.string().c_str()) != 0)
                {
                    std::cerr << "delta 적용 실패 (커밋: " << chain[i] << ")\n";
                    fs::remove(tmp_a);
                    fs::remove(tmp_b);
                    return -1;
                }

                fs::rename(tmp_b, tmp_a); // 다음 단계 입력으로 교체
            }

            // 최종 결과를 원래 경로에 저장
            fs::copy_file(tmp_a, fe.path,
                          fs::copy_options::overwrite_existing);
            fs::remove(tmp_a);
        }

        // 4. SHA256 검증
        if (!fe.sha256.empty())
        {
            std::string actual = sha256_file(fe.path);
            if (actual != fe.sha256)
            {
                std::cerr << "SHA256 불일치 - 복원 실패!\n"
                          << "  기대값: " << fe.sha256 << "\n"
                          << "  실제값: " << actual << "\n";
                return -1;
            }
            std::cout << "SHA256 검증 통과: " << fe.path << "\n";
        }
    }

    // 5. HEAD 업데이트
    write_head(vcs_path, commit_id);
    std::cout << "체크아웃 완료: " << commit_id.substr(0, 8) << "\n";
    return 0;
}

// log: HEAD-> parent_id 체인 순회 출력---------
void log(const std::string &repo_path)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        std::cerr << "dgit 저장소가 없습니다.\n";
        return;
    }

    std::string cur = read_head(vcs_path);
    if (cur.empty())
    {
        std::cout << "커밋 히스토리가 없습니다.\n";
        return;
    }

    int count = 0;
    while (!cur.empty())
    {
        CommitMetadata meta = load_commit_metadata(repo_path, cur);
        if (meta.id.empty())
        {
            std::cerr << "손상된 커밋 발견, 히스토리 출력을 중단합니다: "
                      << cur << "\n";
            break;
        }

        std::cout << "commit  " << meta.id << "\n"
                  << "Date:   " << meta.timestamp << "\n"
                  << "        " << meta.message << "\n\n";

        ++count;
        cur = meta.parent_id; // 부모 커밋으로 이동
    }

    if (count == 0)
        std::cout << "커밋 히스토리가 없습니다.\n";
}
