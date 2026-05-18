#include "format.h"
#include <fstream>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static const size_t SAMPLE_BLOCK = 4 + 1024 * 1024;        // 4MB 블록 단위
static const size_t SAMPLE_REGION = 100 * 1024 * 1024; // 100MB 구간

// 앞 16바이트만 읽으면 모든 헤더 판별 가능
static const size_t HEADER_SIZE = 16;

// 압축 포맷 헤더 테이블
// { 시작 오프셋, 바이트 배열, 길이 }
struct MagicEntry {
    size_t      offset;
    uint8_t     magic[8];
    size_t      len;
};

static const MagicEntry COMPRESSED_FORMATS[] = {
    { 0, { 0xFF, 0xD8, 0xFF },             3 },  // JPG
    { 0, { 0x89, 0x50, 0x4E, 0x47 },       4 },  // PNG
    { 0, { 0x47, 0x49, 0x46 },             3 },  // GIF
    { 0, { 0x50, 0x4B, 0x03, 0x04 },       4 },  // ZIP
    { 0, { 0x1F, 0x8B },                   2 },  // GZIP
    { 0, { 0x28, 0xB5, 0x2F, 0xFD },       4 },  // ZSTD
    { 4, { 0x66, 0x74, 0x79, 0x70 },       4 },  // MP4 ("ftyp" box)
};

bool is_compressed_format(const std::string& filepath)
{
    // 헤더 16바이트 읽기
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open())
    {
        std::cerr << "format: failed to open file: " << filepath << "\n";
        return false;
    }

    uint8_t header[HEADER_SIZE] = {};
    f.read(reinterpret_cast<char*>(header), HEADER_SIZE);
    const size_t bytes_read = static_cast<size_t>(f.gcount());

    // 헤더 테이블과 비교
    for (const auto& entry : COMPRESSED_FORMATS) {
        if (entry.offset + entry.len > bytes_read)
            continue;
        if (memcmp(header + entry.offset, entry.magic, entry.len) == 0)
            return true;
    }

    return false;  // 해당 없으면 비압축 → delta 대상
}


// 특정 오프셋부터 블록 단위로 읽어서 변경된 블록 수 반환
static size_t count_changed_blocks(std::ifstream& prev_f,
                                   std::ifstream& curr_f,
                                   uint64_t offset,
                                   uint64_t region_size)
{
    prev_f.seekg(offset);
    curr_f.seekg(offset);

    std::vector<uint8_t> prev_buf(SAMPLE_BLOCK);
    std::vector<uint8_t> curr_buf(SAMPLE_BLOCK);

    size_t changed = 0;
    size_t scanned = 0;

    while (scanned < region_size)
    {
        prev_f.read(reinterpret_cast<char*>(prev_buf.data()), SAMPLE_BLOCK);
        curr_f.read(reinterpret_cast<char*>(curr_buf.data()), SAMPLE_BLOCK);

        size_t prev_n = static_cast<size_t>(prev_f.gcount());
        size_t curr_n = static_cast<size_t>(curr_f.gcount());

        if (prev_n == 0 || curr_n == 0)
            break;

        size_t n = std::min(prev_n, curr_n);
        if (memcmp(prev_buf.data(), curr_buf.data(), n) != 0)
            ++changed;

        scanned += n;
    }

    return changed;
}

bool should_use_full_copy(const std::string& prev_path,
                          const std::string& curr_path,
                          double threshold)
{
    std::ifstream prev_f(prev_path, std::ios::binary);
    std::ifstream curr_f(curr_path, std::ios::binary);

    if (!prev_f.is_open() || !curr_f.is_open())
        return false; // 열기 실패 시 delta 시도

    // 파일 크기 확인
    prev_f.seekg(0, std::ios::end);
    curr_f.seekg(0, std::ios::end);
    uint64_t prev_size = static_cast<uint64_t>(prev_f.tellg());
    uint64_t curr_size = static_cast<uint64_t>(curr_f.tellg());

    // 파일 크기 차이가 50% 이상이면 바로 전체 저장
    double size_ratio = static_cast<double>(curr_size) /
                        static_cast<double>(prev_size > 0 ? prev_size : 1);
    if (size_ratio > 1.5 || size_ratio < 0.5)
        return true;

    uint64_t file_size = std::min(prev_size, curr_size);

    size_t changed = 0;
    size_t total   = 0;

    // 파일이 300MB보다 작으면 전체를 한 번만 샘플링
    if (file_size < SAMPLE_REGION * 3)
    {
        size_t blocks = (file_size + SAMPLE_BLOCK - 1) / SAMPLE_BLOCK;
        changed += count_changed_blocks(prev_f, curr_f, 0, file_size);
        total   += blocks;
    }
    else
    {
        uint64_t mid_offset  = file_size / 2 - SAMPLE_REGION / 2;
        uint64_t tail_offset = file_size - SAMPLE_REGION;

        auto count_region = [&](uint64_t offset) {
            size_t blocks = (SAMPLE_REGION + SAMPLE_BLOCK - 1) / SAMPLE_BLOCK;
            changed += count_changed_blocks(prev_f, curr_f, offset, SAMPLE_REGION);
            total   += blocks;
        };

        count_region(0);
        if (mid_offset >= SAMPLE_REGION)
            count_region(mid_offset);
        if (tail_offset >= mid_offset + SAMPLE_REGION)
            count_region(tail_offset);
    }

    if (total == 0)
        return false;

    double change_ratio = static_cast<double>(changed) /
                        static_cast<double>(total);

    return change_ratio >= threshold;
}