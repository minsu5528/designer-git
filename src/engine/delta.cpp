#include "delta.h"
#include <fstream>
#include <cstdint>
#include <unordered_map>
#include <vector>

// ── 상수 ────────────────────────────────────────────────────
static const size_t CHUNK = 16 * 1024;  // 16KB 블록 크기

// ── Rolling Hash 계산 ────────────────────────────────────────
// 블록 하나의 해시값을 계산한다.
// Rabin-Karp 방식: 각 바이트를 BASE의 거듭제곱으로 가중합산.
static uint64_t compute_hash(const uint8_t* buf, size_t len) {
    const uint64_t BASE = 257;
    const uint64_t MOD  = 1000000007ULL;
    uint64_t hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash = (hash * BASE + buf[i]) % MOD;
    }
    return hash;
}

// ── delta_create ─────────────────────────────────────────────
int delta_create(const char* path_a,
                 const char* path_b,
                 const char* out_delta) {

    // 1. 파일 A 열기
    std::ifstream file_a(path_a, std::ios::binary);
    if (!file_a) return -1;

    // 2. 파일 A를 16KB씩 읽으면서 해시맵 구성
    // { 해시값 → (오프셋, 길이) }
    std::unordered_map<uint64_t, std::pair<size_t, size_t>> hash_map;
    uint8_t buf[CHUNK];
    size_t offset = 0;

    while (file_a.read((char*)buf, CHUNK) || file_a.gcount() > 0) {
        size_t n = file_a.gcount();
        uint64_t h = compute_hash(buf, n);
        hash_map[h] = { offset, n };
        offset += n;
    }

    // 3. 파일 B 열기
    std::ifstream file_b(path_b, std::ios::binary);
    if (!file_b) return -1;

    // 4. delta 출력 파일 열기
    std::ofstream out(out_delta, std::ios::binary);
    if (!out) return -1;

    // 5. 파일 B를 16KB씩 읽으면서 파일 A와 비교
    // 같은 블록 → COPY 명령 (1)
    // 다른 블록 → INSERT 명령 (0) + 실제 데이터
    offset = 0;
    while (file_b.read((char*)buf, CHUNK) || file_b.gcount() > 0) {
        size_t n = file_b.gcount();
        uint64_t h = compute_hash(buf, n);

        if (hash_map.count(h)) {
            // COPY 명령: 파일 A의 해당 블록을 그대로 쓴다
            uint8_t  cmd    = 1;                          // COPY
            size_t   src_off = hash_map[h].first;
            size_t   src_len = hash_map[h].second;
            out.write((char*)&cmd,     sizeof(cmd));
            out.write((char*)&src_off, sizeof(src_off));
            out.write((char*)&src_len, sizeof(src_len));
        } else {
            // INSERT 명령: 실제 데이터를 그대로 저장한다
            uint8_t cmd = 0;                              // INSERT
            out.write((char*)&cmd, sizeof(cmd));
            out.write((char*)&n,   sizeof(n));
            out.write((char*)buf,  n);
        }
        offset += n;
    }

    return 0;
}

// ── delta_apply ──────────────────────────────────────────────
int delta_apply(const char* path_base,
                const char* path_delta,
                const char* out_file) {

    // 1. 베이스 파일 열기
    std::ifstream base(path_base, std::ios::binary);
    if (!base) return -1;

    // 2. delta 파일 열기
    std::ifstream delta(path_delta, std::ios::binary);
    if (!delta) return -1;

    // 3. 출력 파일 열기
    std::ofstream out(out_file, std::ios::binary);
    if (!out) return -1;

    // 4. delta 명령어 순서대로 실행
    uint8_t cmd;
    uint8_t buf[CHUNK];

    while (delta.read((char*)&cmd, sizeof(cmd))) {
        if (cmd == 1) {
            // COPY 명령: 베이스 파일에서 해당 위치 데이터 읽어서 쓰기
            size_t src_off, src_len;
            delta.read((char*)&src_off, sizeof(src_off));
            delta.read((char*)&src_len, sizeof(src_len));

            base.seekg(src_off);
            base.read((char*)buf, src_len);
            out.write((char*)buf, base.gcount());

        } else {
            // INSERT 명령: delta에 포함된 데이터 그대로 쓰기
            size_t n;
            delta.read((char*)&n, sizeof(n));
            delta.read((char*)buf, n);
            out.write((char*)buf, n);
        }
    }

    return 0;
}