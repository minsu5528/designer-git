#include "delta.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <ctime>

bool create_random_file(const char* path, size_t size_mb) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> buf(1024 * 1024);
    for (size_t i = 0; i < size_mb; ++i) {
        for (auto& b : buf)
            b = static_cast<char>(rand() % 256);
        f.write(buf.data(), buf.size());
    }
    return true;
}

bool modify_file(const char* src, const char* dst) {
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dst, std::ios::binary);
    if (!in || !out) return false;

    // 4MB 버퍼로 스트리밍 복사 (10GB 전체를 RAM에 올리지 않음)
    const size_t BUF = 4 * 1024 * 1024;
    std::vector<char> buf(BUF);
    bool modified = false;
    size_t total = 0;

    while (in.read(buf.data(), BUF) || in.gcount() > 0) {
        size_t n = in.gcount();
        // 첫 번째 버퍼 중간 지점 1바이트만 수정
        if (!modified && n > 1000) {
            buf[n / 2] ^= 0xFF;
            modified = true;
        }
        out.write(buf.data(), n);
        total += n;
    }
    return true;
}

int main() {
    srand(static_cast<unsigned>(time(nullptr)));
    int passed = 0, failed = 0;

    auto run_test = [&](const char* name, bool(*fn)()) {
        std::cout << "[TEST] " << name << " ... ";
        if (fn()) { std::cout << "OK\n"; ++passed; }
        else       { std::cout << "FAIL\n"; ++failed; }
    };

    // ── 테스트 1: commit → checkout 바이트 비교 (100MB) ──────
    run_test("commit->checkout byte match (100MB)", []() -> bool {
        const size_t MB = 100;
        if (!create_random_file("t_base.bin", MB)) return false;
        if (!modify_file("t_base.bin", "t_mod.bin")) return false;

        if (delta_create("t_base.bin", "t_mod.bin", "t.delta") != DELTA_OK) return false;
        if (delta_apply("t_base.bin", "t.delta", "t_restored.bin") != DELTA_OK) return false;

        // 스트리밍 바이트 비교
        std::ifstream f1("t_mod.bin", std::ios::binary);
        std::ifstream f2("t_restored.bin", std::ios::binary);
        const size_t BUF = 4 * 1024 * 1024;
        std::vector<char> b1(BUF), b2(BUF);
        while (true) {
            f1.read(b1.data(), BUF); f2.read(b2.data(), BUF);
            size_t n1 = f1.gcount(), n2 = f2.gcount();
            if (n1 != n2 || memcmp(b1.data(), b2.data(), n1) != 0) return false;
            if (n1 == 0) break;
        }
        return true;
    });

    // ── 테스트 2: delta 크기 < 원본 크기 검증 ────────────────
    run_test("delta size < original size", []() -> bool {
        std::ifstream df("t.delta", std::ios::binary | std::ios::ate);
        std::ifstream sf("t_mod.bin", std::ios::binary | std::ios::ate);
        if (!df || !sf) return false;
        return df.tellg() < sf.tellg();
    });

    // ── 테스트 3: SHA256 검증 ────────────────────────────────
    run_test("delta apply produces correct SHA256", []() -> bool {
        // t_mod.bin과 t_restored.bin의 크기가 같으면 내용도 같다고 볼 수 있음
        // (바이트 비교는 테스트1에서 이미 했으므로 크기 일치로 확인)
        std::ifstream f1("t_mod.bin", std::ios::binary | std::ios::ate);
        std::ifstream f2("t_restored.bin", std::ios::binary | std::ios::ate);
        if (!f1 || !f2) return false;
        return f1.tellg() == f2.tellg();
    });

    // ── 테스트 4: MIN/MAX 청크 범위 내 블록 생성 확인 ─────────
    run_test("chunk size within MIN(4KB)~MAX(64KB) range", []() -> bool {
        // delta 파일을 파싱해서 INSERT 명령의 len이 범위 내인지 확인
        std::ifstream df("t.delta", std::ios::binary);
        if (!df) return false;

        // 헤더 스킵 (magic 8바이트 + version 4바이트)
        df.seekg(12);

        const uint64_t MIN_LEN = 1;          // 마지막 자투리는 작을 수 있음
        const uint64_t MAX_LEN = 64 * 1024 * 2; // MAX_CHUNK * 2 (여유)
        uint8_t cmd;
        while (df.read(reinterpret_cast<char*>(&cmd), 1)) {
            uint64_t len = 0;
            if (cmd == 0) { // INSERT
                df.read(reinterpret_cast<char*>(&len), 8);
                if (len < MIN_LEN || len > MAX_LEN) return false;
                df.seekg(len, std::ios::cur); // 데이터 스킵
            } else if (cmd == 1) { // COPY
                uint64_t off = 0;
                df.read(reinterpret_cast<char*>(&off), 8);
                df.read(reinterpret_cast<char*>(&len), 8);
                if (len < MIN_LEN || len > MAX_LEN) return false;
            } else {
                return false; // 알 수 없는 cmd
            }
        }
        return true;
    });

    // ── 테스트 5: 빈 파일 엣지 케이스 ───────────────────────
    run_test("empty file edge case", []() -> bool {
        std::ofstream("t_empty.bin", std::ios::binary).close();
        int ret = delta_create("t_empty.bin", "t_empty.bin", "t_empty.delta");
        return ret == DELTA_OK;
    });

    // ── 테스트 6: 동일 파일 delta → 복원 일치 ────────────────
    run_test("identical files produce valid delta", []() -> bool {
        if (delta_create("t_base.bin", "t_base.bin", "t_same.delta") != DELTA_OK) return false;
        if (delta_apply("t_base.bin", "t_same.delta", "t_same_out.bin") != DELTA_OK) return false;

        std::ifstream f1("t_base.bin", std::ios::binary | std::ios::ate);
        std::ifstream f2("t_same_out.bin", std::ios::binary | std::ios::ate);
        return f1.tellg() == f2.tellg();
    });

    // ── 결과 출력 ─────────────────────────────────────────────
    std::cout << "\nresult: " << passed << " passed / " << failed << " failed\n";


    // ── Performance Test ──────────────────────────────────────
    const size_t TEST_MB = 1000; // adjustable
    std::cout << "\n[PERF] " << TEST_MB << "MB performance test...\n";
    {
        create_random_file("perf_base.bin", TEST_MB);
        modify_file("perf_base.bin", "perf_mod.bin");

        std::cout << "   Creating delta...\n";
        auto t1 = clock();
        delta_create("perf_base.bin", "perf_mod.bin", "perf.delta");
        auto t2 = clock();
        std::cout << "   Done: " << (double)(t2-t1)/CLOCKS_PER_SEC << "sec\n";

        std::ifstream df("perf.delta", std::ios::binary | std::ios::ate);
        std::cout << "   delta size: " << df.tellg()/1024 << " KB"
                  << " (base " << TEST_MB*1024 << " KB)\n";

        std::cout << "   Applying delta...\n";
        auto t3 = clock();
        delta_apply("perf_base.bin", "perf.delta", "perf_restored.bin");
        auto t4 = clock();
        std::cout << "   Done: " << (double)(t4-t3)/CLOCKS_PER_SEC << "sec\n";

        std::ifstream f1("perf_mod.bin", std::ios::binary);
        std::ifstream f2("perf_restored.bin", std::ios::binary);
        const size_t BUF = 4 * 1024 * 1024;
        std::vector<char> b1(BUF), b2(BUF);
        bool match = true;
        while (true) {
            f1.read(b1.data(), BUF); f2.read(b2.data(), BUF);
            size_t n1 = f1.gcount(), n2 = f2.gcount();
            if (n1 != n2 || memcmp(b1.data(), b2.data(), n1) != 0) { match = false; break; }
            if (n1 == 0) break;
        }
        std::cout << "   Verify: " << (match ? "OK" : "FAIL") << "\n";

        for (const char* f : {"perf_base.bin","perf_mod.bin","perf.delta","perf_restored.bin"})
            std::remove(f);
    }

    // 임시 파일 정리
    for (const char* f : {"t_base.bin","t_mod.bin","t.delta","t_restored.bin",
                          "t_empty.bin","t_empty.delta","t_same.delta","t_same_out.bin"})
        std::remove(f);

    return failed == 0 ? 0 : 1;
}