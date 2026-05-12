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

    const size_t TEST_MB = 10000;
    std::cout << TEST_MB << "MB test start...\n";

    std::cout << "1. Creating base file...\n";
    if (!create_random_file("test_base.bin", TEST_MB)) {
        std::cerr << "FAIL: file creation failed\n"; return 1;
    }

    std::cout << "2. Creating modified file...\n";
    if (!modify_file("test_base.bin", "test_modified.bin")) {
        std::cerr << "FAIL: file modify failed\n"; return 1;
    }

    std::cout << "3. Creating delta...\n";
    auto t1 = clock();
    int ret = delta_create("test_base.bin", "test_modified.bin", "test.delta");
    auto t2 = clock();
    if (ret != DELTA_OK) {
        std::cerr << "FAIL: delta_create error: " << ret << "\n"; return 1;
    }
    std::cout << "   Done: "
              << (double)(t2 - t1) / CLOCKS_PER_SEC << "sec\n";

    std::ifstream df("test.delta", std::ios::binary | std::ios::ate);
    std::cout << "   delta size: " << df.tellg() / 1024 << " KB"
              << " (base " << TEST_MB * 1024 << " KB)\n";

    std::cout << "4. Applying delta...\n";
    auto t3 = clock();
    ret = delta_apply("test_base.bin", "test.delta", "test_restored.bin");
    auto t4 = clock();
    if (ret != DELTA_OK) {
        std::cerr << "FAIL: delta_apply error: " << ret << "\n"; return 1;
    }
    std::cout << "   Done: "
              << (double)(t4 - t3) / CLOCKS_PER_SEC << "sec\n";

    std::cout << "5. Verifying restore...\n";
    std::ifstream f1("test_modified.bin", std::ios::binary);
    std::ifstream f2("test_restored.bin", std::ios::binary);

    const size_t CMP_BUF = 4 * 1024 * 1024;
    std::vector<char> b1(CMP_BUF), b2(CMP_BUF);
    bool match = true;

    while (true) {
        f1.read(b1.data(), CMP_BUF);
        f2.read(b2.data(), CMP_BUF);
        size_t n1 = f1.gcount(), n2 = f2.gcount();
        if (n1 != n2 || memcmp(b1.data(), b2.data(), n1) != 0) {
            match = false; break;
        }
        if (n1 == 0) break;
    }

    if (match)
        std::cout << "OK Restore success: byte-level match\n";
    else {
        std::cerr << "FAIL: byte mismatch\n"; return 1;
    }

    std::cout << "\nTest complete!\n";
    return 0;
}