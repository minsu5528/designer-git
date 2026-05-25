#include "tiff_utils.h"
#include <fstream>
#include <vector>
#include <filesystem>
#include <tiffio.h>

namespace fs = std::filesystem;

// TIFF 매직 바이트 확인 (II = little-endian, MM = big-endian)
bool is_tiff_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    uint8_t h[4] = {};
    f.read(reinterpret_cast<char*>(h), 4);
    if (f.gcount() < 4) return false;

    // Little-endian: II 0x2A 0x00
    if (h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[3] == 0) return true;
    // Big-endian: MM 0x00 0x2A
    if (h[0] == 'M' && h[1] == 'M' && h[2] == 0  && h[3] == 42) return true;
    return false;
}

// TIFF compression tag 읽기 (0x0103)
// COMPRESSION_NONE = 1 → 비압축
bool is_tiff_compressed(const std::string& path)
{
    if (!is_tiff_file(path)) return false;

    // libtiff 경고 억제
    TIFFSetWarningHandler(nullptr);
    TIFFSetErrorHandler(nullptr);

    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (!tif) return false;

    uint16_t compression = 1;
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
    TIFFClose(tif);

    return compression != 1; // 1 = COMPRESSION_NONE
}

// 압축 TIFF → 비압축 TIFF (scanline 방식)
// Substance Painter 출력 등 일반 TIFF에 적합
bool decompress_tiff_to(const std::string& src_path, const std::string& dst_path)
{
    TIFFSetWarningHandler(nullptr);
    TIFFSetErrorHandler(nullptr);

    TIFF* in_tif = TIFFOpen(src_path.c_str(), "r");
    if (!in_tif) return false;

    // ── 필수 태그 읽기 ──
    uint32_t width = 0, height = 0;
    uint16_t spp = 1, bps = 8, planar = PLANARCONFIG_CONTIG;
    uint16_t photometric = PHOTOMETRIC_RGB;

    TIFFGetField(in_tif, TIFFTAG_IMAGEWIDTH,      &width);
    TIFFGetField(in_tif, TIFFTAG_IMAGELENGTH,     &height);
    TIFFGetField(in_tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetField(in_tif, TIFFTAG_BITSPERSAMPLE,   &bps);
    TIFFGetField(in_tif, TIFFTAG_PLANARCONFIG,    &planar);
    TIFFGetField(in_tif, TIFFTAG_PHOTOMETRIC,     &photometric);

    // Extra samples (알파 채널 등) 복사
    uint16_t extra_count = 0;
    uint16_t* extra_types = nullptr;
    TIFFGetField(in_tif, TIFFTAG_EXTRASAMPLES, &extra_count, &extra_types);

    TIFF* out_tif = TIFFOpen(dst_path.c_str(), "w");
    if (!out_tif) {
        TIFFClose(in_tif);
        return false;
    }

    // ── 출력 태그 설정 (압축만 NONE으로 변경) ──
    TIFFSetField(out_tif, TIFFTAG_IMAGEWIDTH,      width);
    TIFFSetField(out_tif, TIFFTAG_IMAGELENGTH,     height);
    TIFFSetField(out_tif, TIFFTAG_SAMPLESPERPIXEL, spp);
    TIFFSetField(out_tif, TIFFTAG_BITSPERSAMPLE,   bps);
    TIFFSetField(out_tif, TIFFTAG_COMPRESSION,     COMPRESSION_NONE); // ← 핵심
    TIFFSetField(out_tif, TIFFTAG_PHOTOMETRIC,     photometric);
    TIFFSetField(out_tif, TIFFTAG_PLANARCONFIG,    planar);
    TIFFSetField(out_tif, TIFFTAG_ROWSPERSTRIP,    1);

    if (extra_count > 0 && extra_types != nullptr)
        TIFFSetField(out_tif, TIFFTAG_EXTRASAMPLES, extra_count, extra_types);

    // ── 스캔라인 단위 복사 ──
    tsize_t scan_size = TIFFScanlineSize(in_tif);
    std::vector<uint8_t> buf(static_cast<size_t>(scan_size));

    for (uint32_t row = 0; row < height; ++row)
    {
        if (TIFFReadScanline(in_tif, buf.data(), row) < 0)
        {
            TIFFClose(in_tif);
            TIFFClose(out_tif);
            fs::remove(dst_path);
            return false;
        }
        if (TIFFWriteScanline(out_tif, buf.data(), row) < 0)
        {
            TIFFClose(in_tif);
            TIFFClose(out_tif);
            fs::remove(dst_path);
            return false;
        }
    }

    TIFFClose(in_tif);
    TIFFClose(out_tif);
    return true;
}