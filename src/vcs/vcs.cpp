#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include <openssl/evp.h>
#include "json.hpp"
#include "vcs.h"
#include "../engine/delta.h"
#include "../engine/format.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

// VCS 레이어의 출력은 CLI에서 담당함.
// 사용자에게 보여줄 메시지는 CLI에서만 출력하도록 표준 출력/표준 에러 사용을 제거함.

//  내부 헬퍼 함수 ㅡㅡㅡㅡㅡㅡㅡㅡ
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
static std::string sha256_file(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return "";

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
    {
        if (EVP_DigestUpdate(ctx, buf, static_cast<size_t>(f.gcount())) != 1)
        {
            EVP_MD_CTX_free(ctx);
            return "";
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < digest_len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(digest[i]);
    return oss.str();
}

// HEAD 파일에서 현재 커밋 ID 읽기 (없으면 빈 문자열)
static std::string read_head(const fs::path& vcs_path)
{
    std::string id;
    std::ifstream f(vcs_path / "HEAD");
    if (f.is_open())
        std::getline(f, id);
    return id;
}

// HEAD 파일에 커밋 ID 쓰기
static void write_head(const fs::path& vcs_path, const std::string& commit_id)
{
    std::ofstream f(vcs_path / "HEAD");
    f << commit_id;
}

// 현재 커밋이 몇 번째인지 체인 길이로 계산
static int count_commit_depth(const std::string& repo_path,
    const std::string& commit_id)
{
    int depth = 0;
    std::string cur = commit_id;
    while (!cur.empty())
    {
        CommitMetadata m = load_commit_metadata(repo_path, cur);
        if (m.id.empty())
            break;
        ++depth;
        if (!m.files.empty() && m.files[0].is_base)
            break;
        cur = m.parent_id;
    }
    return depth;
}

// 스냅샷 저장
static void save_snapshot(const fs::path& vcs_path,
    const std::string& commit_id,
    const std::string& target_file)
{
    fs::path snap_path = vcs_path / "objects" / "snapshots" /
        (commit_id + ".snap");
    fs::copy_file(target_file, snap_path,
        fs::copy_options::overwrite_existing);
}

// 체인 역추적하며 가장 가까운 스냅샷 찾기
// 반환값: 스냅샷 커밋 ID (없으면 빈 문자열)
static std::string find_nearest_snapshot(const std::string& repo_path,
    const fs::path& vcs_path,
    const std::string& commit_id)
{
    std::string cur = commit_id;
    while (!cur.empty())
    {
        fs::path snap = vcs_path / "objects" / "snapshots" / (cur + ".snap");
        if (fs::exists(snap))
            return cur;

        CommitMetadata m = load_commit_metadata(repo_path, cur);
        if (m.id.empty())
            break;
        if (!m.files.empty() && m.files[0].is_base)
            break;
        cur = m.parent_id;
    }
    return "";
}

// 특정 커밋 시점의 파일 out_path에 복원
static bool restore_file_at_commit(const std::string& repo_path,
    const std::string& prev_commit_id,
    const std::string& filename,
    const fs::path& out_path)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    // prev_commit_id까지의 체인 구성
    std::vector<std::string> chain;
    std::string cur = prev_commit_id;
    while (!cur.empty())
    {
        CommitMetadata m = load_commit_metadata(repo_path, cur);
        if (m.id.empty())
            return false;
        chain.push_back(cur);
        if (!m.files.empty() && m.files[0].is_base)
            break;
        cur = m.parent_id;
    }
    std::reverse(chain.begin(), chain.end());

    fs::path base_file = vcs_path / "objects" / "base" / filename;
    if (!fs::exists(base_file))
        return false;

    // base 커밋이 목표인 경우
    if (chain.size() == 1)
    {
        fs::copy_file(base_file, out_path, fs::copy_options::overwrite_existing);
        return true;
    }

    fs::path tmp_a = vcs_path / ("restore_a_" + prev_commit_id);
    fs::path tmp_b = vcs_path / ("restore_b_" + prev_commit_id);
    fs::copy_file(base_file, tmp_a, fs::copy_options::overwrite_existing);

    for (size_t i = 1; i < chain.size(); ++i)
    {
        CommitMetadata cm = load_commit_metadata(repo_path, chain[i]);
        if (cm.id.empty())
        {
            fs::remove(tmp_a);
            return false;
        }

        std::string delta_rel;
        for (const auto& entry : cm.files)
        {
            if (fs::path(entry.path).filename() == filename)
            {
                delta_rel = entry.delta;
                break;
            }
        }
        if (delta_rel.empty())
            continue;

        fs::path delta_path = vcs_path / delta_rel;
        if (!fs::exists(delta_path))
        {
            fs::remove(tmp_a);
            return false;
        }

        if (delta_apply(tmp_a.string().c_str(),
            delta_path.string().c_str(),
            tmp_b.string().c_str()) != 0)
        {
            fs::remove(tmp_a);
            fs::remove(tmp_b);
            return false;
        }
        fs::rename(tmp_b, tmp_a);
    }

    fs::rename(tmp_a, out_path);
    return true;
}

// .vcs 폴더 구조 및 초기 설정 파일을 생성하는 함수
int init_repository(const std::string& path)
{
    fs::path vcs = fs::path(path) / ".vcs";

    if (fs::exists(vcs))
    {
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

        return 0;
    }
    catch (const fs::filesystem_error& e)
    {
        return -1;
    }
}

// index에 파일 경로 등록-------
int add_file(const std::string& repo_path, const std::string& filepath)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        return -1;
    }

    if (!fs::exists(filepath))
    {
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
                return 0;
            }
        }
    }

    // 새 경로 추가 (append)
    std::ofstream idx(index_path, std::ios::app);
    if (!idx.is_open())
    {
        return -1;
    }
    idx << filepath << "\n";

    return 0;
}

// 커밋 메타데이터 저장/읽기 --------
// 커밋 메타데이터를 .vcs/commits/[commit_id].json 형식으로 JSON 파일로 저장하는 함수
int save_commit_metadata(const std::string& repo_path, const CommitMetadata& meta)
{
    fs::path file_path =
        fs::path(repo_path) / ".vcs" / "commits" / (meta.id + ".json");

    json j;
    j["id"] = meta.id;
    j["message"] = meta.message;
    j["timestamp"] = meta.timestamp;
    j["parent_id"] = meta.parent_id;
    j["sha256"] = meta.sha256;

    j["files"] = json::array();
    for (const auto& f : meta.files)
    {
        j["files"].push_back({ {"path", f.path},
                              {"delta", f.delta},
                              {"is_base", f.is_base},
                              {"sha256", f.sha256},
                              {"length", f.length} });
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
CommitMetadata load_commit_metadata(const std::string& repo_path, const std::string& commit_id)
{
    fs::path file_path = fs::path(repo_path) / ".vcs" / "commits" / (commit_id + ".json");

    std::ifstream file(file_path);
    if (!file.is_open())
    {
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

        for (const auto& item : j.at("files"))
        {
            FileEntry fe;
            fe.path = item.at("path").get<std::string>();
            fe.delta = item.at("delta").get<std::string>();
            fe.is_base = item.at("is_base").get<bool>();
            fe.sha256 = item.value("sha256", "");
            fe.length = item.value("length", uint64_t(0));
            meta.files.push_back(fe);
        }

        return meta;
    }
    catch (const json::exception& e)
    {
        // 손상된 메타데이터 예외 처리
        return {};
    }
}

// commit: 파일의 델타를 생성 및 메타데이터를 JSON으로 저장----------
std::string commit(const std::string& repo_path, const std::string& message, const std::string& target_file)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        return "";
    }
    if (!fs::exists(target_file))
    {
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

    fs::path base_file = vcs_path / "objects" / "base" /
        fs::path(target_file).filename();

    // 4. 최초 커밋: base에 파일 통째로 복사
    if (!fs::exists(base_file))
    {
        fs::copy_file(target_file, base_file,
            fs::copy_options::overwrite_existing);
        uint64_t file_size = static_cast<uint64_t>(fs::file_size(target_file));
        meta.files.push_back({ target_file, "", true, file_sha256, file_size });
    }
    else
    {
        // 압축 포맷이면 delta 대신 전체 저장 (Git LFS 방식과 동일)
        if (is_compressed_format(target_file))
        {
            fs::copy_file(target_file, base_file,
                fs::copy_options::overwrite_existing);
            uint64_t file_size = static_cast<uint64_t>(fs::file_size(target_file));
            meta.files.push_back({ target_file, "", true, file_sha256, file_size });
        }
        else
        {
            // 직전 커밋 파일 기준으로 delta 생성
            std::string delta_filename = commit_id + ".delta";
            fs::path delta_path = vcs_path / "objects" / "deltas" / delta_filename;

            fs::path prev_file;

            /* parent_id가 비어 있는 경우:
                base 파일은 이미 존재하지만(.vcs/objects/base/), 아직 커밋 JSON이 하나도 없는 상태
                이 커밋이 "base 파일과 현재 작업본" 사이의 첫 번째 delta를 만드는 케이스
                parent_id가 있는 분기로 들어가면 restore_file_at_commit()이 parent_id를 따라가다
                JSON을 못 찾아 실패하므로, base 파일 자체를 비교 기준으로 사용한다.
            */
            if (parent_id.empty())
            {
                prev_file = base_file;
            }
            else
            {
                // 직전 커밋 시점의 파일을 임시 경로에 복원
                prev_file = vcs_path / ("prev_" + commit_id);
                std::string filename = fs::path(target_file).filename().string();

                if (!restore_file_at_commit(repo_path, parent_id, filename, prev_file))
                {
                    return "";
                }
            }

            // 사전 샘플링 — 변경률 높으면 전체 저장으로 전환
            if (should_use_full_copy(prev_file.string(), target_file))
            {
                if (!parent_id.empty() && fs::exists(prev_file))
                    fs::remove(prev_file);

                std::string fc_filename = commit_id + ".fullcopy";
                fs::path fc_path = vcs_path / "objects" / "snapshots" / fc_filename;
                fs::copy_file(target_file, fc_path,
                    fs::copy_options::overwrite_existing);
                uint64_t file_size = static_cast<uint64_t>(fs::file_size(target_file));
                meta.files.push_back({ target_file, "objects/snapshots/" + fc_filename, true, file_sha256, file_size });
            }
            else
            {
                int ret = delta_create(prev_file.string().c_str(),
                    target_file.c_str(),
                    delta_path.string().c_str());

                /* parent_id가 비어 있으면 위의 분기에서 prev_file = base_file 을 그대로
                가리키고 있으므로, 임시 복원 파일이 생성된 적이 없다.즉, 삭제 대상이 없다.
                parent_id가 있을 때만 restore_file_at_commit()이 임시 파일을 만들었으므로 해당 경우에만 정리한다.
                */
                if (!parent_id.empty() && fs::exists(prev_file))
                    fs::remove(prev_file);

                if (ret != 0)
                {
                    return "";
                }

                // delta 커밋
                uint64_t file_size = static_cast<uint64_t>(fs::file_size(target_file));
                meta.files.push_back(
                    { target_file, "objects/deltas/" + delta_filename, false, file_sha256, file_size });
            }
        }
    }

    // 5. JSON 저장 & HEAD 업데이트
    if (save_commit_metadata(repo_path, meta) != 0)
    {
        return "";
    }

    // N커밋마다 스냅샷 저장
    static const int SNAPSHOT_INTERVAL = 10;
    int depth = count_commit_depth(repo_path, commit_id);
    if (depth % SNAPSHOT_INTERVAL == 0)
    {
        save_snapshot(vcs_path, commit_id, target_file);
    }

    write_head(vcs_path, commit_id);
    return commit_id;
}

// checkout: 특정 시점으로 파일 복원--------
int checkout(const std::string& repo_path, const std::string& commit_id)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        return -1;
    }

    // 1. 목표 커밋 메타데parent_id가 있는 분기로 들어가면이터 로드
    CommitMetadata target = load_commit_metadata(repo_path, commit_id);
    if (target.id.empty())
    {
        return -1;
    }

    // 2. base → 목표 커밋까지 delta 체인 구성 (parent_id 역추적)
    //    chain[0] = base 커밋, chain[back] = 목표 커밋
    std::vector<std::string> chain;
    std::string snap_commit_id = find_nearest_snapshot(repo_path, vcs_path, commit_id);
    {
        std::string cur = commit_id;
        while (!cur.empty())
        {
            CommitMetadata m = load_commit_metadata(repo_path, cur);
            if (m.id.empty())
            {
                return -1;
            }
            chain.push_back(cur);

            // 스냅샷 있으면 거기서 중단, 없으면 base까지
            if (cur == snap_commit_id)
                break;
            // base 커밋(is_base == true)에 도달하면 중단
            if (!m.files.empty() && m.files[0].is_base)
                break;
            cur = m.parent_id;
        }
        std::reverse(chain.begin(), chain.end()); // 오래된 순서로 정렬
    }

    // 3. 파일별 복원
    for (const auto& fe : target.files)
    {
        fs::path base_file = vcs_path / "objects" / "base" /
            fs::path(fe.path).filename();

        if (!fs::exists(base_file))
        {
            return -1;
        }

        // 최초 커밋(is_base) checkout: base 파일 그대로 복사
        if (fe.is_base)
        {
            fs::path src_file;
            if (!fe.delta.empty())
                src_file = vcs_path / fe.delta; // fullcopy 경로
            else
                src_file = base_file; // 최초 base

            if (!fs::exists(src_file))
            {
                return -1;
            }
            fs::copy_file(src_file, fe.path,
                fs::copy_options::overwrite_existing);
        }
        else
        {
            // delta 체인 순차 적용
            // tmp_a: 현재 복원 중간 결과, tmp_b: delta 적용 후 출력
            fs::path tmp_a = vcs_path / ("tmp_a_" + commit_id);
            fs::path tmp_b = vcs_path / ("tmp_b_" + commit_id);

            // 스냅샷 있으면 스냅샷에서 시작, 없으면 base에서 시작
            fs::path start_file;
            if (!snap_commit_id.empty())
                start_file = vcs_path / "objects" / "snapshots" / (snap_commit_id + ".snap");
            else
                start_file = base_file;

            fs::copy_file(base_file, tmp_a,
                fs::copy_options::overwrite_existing);

            // chain[0]은 base 커밋(delta 없음), chain[1]부터 delta 적용
            for (size_t i = 1; i < chain.size(); ++i)
            {
                CommitMetadata cm = load_commit_metadata(repo_path, chain[i]);
                if (cm.id.empty())
                {
                    fs::remove(tmp_a);
                    return -1;
                }

                // 같은 파일명의 delta 경로 탐색
                std::string delta_rel;
                for (const auto& entry : cm.files)
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
                    fs::remove(tmp_a);
                    return -1;
                }

                // delta 적용: tmp_a(이전) + delta → tmp_b(다음)
                if (delta_apply(tmp_a.string().c_str(),
                    delta_path.string().c_str(),
                    tmp_b.string().c_str()) != 0)
                {
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
                return -1;
            }
        }
    }

    // 5. HEAD 업데이트
    write_head(vcs_path, commit_id);
    return 0;
}

// log: HEAD-> parent_id 체인 순회 출력---------
void log(const std::string& repo_path)
{
    fs::path vcs_path = fs::path(repo_path) / ".vcs";

    if (!fs::exists(vcs_path))
    {
        return;
    }

    std::string cur = read_head(vcs_path);
    if (cur.empty())
    {
        return;
    }

    int count = 0;
    while (!cur.empty())
    {
        CommitMetadata meta = load_commit_metadata(repo_path, cur);
        if (meta.id.empty())
        {
            break;
        }


        ++count;
        cur = meta.parent_id; // 부모 커밋으로 이동
    }
}
