#pragma once
#include <string>
 
// TIFF 파일 여부 확인 (매직 바이트)
bool is_tiff_file(const std::string& path);
 
// TIFF 압축 여부 확인 (compression tag != 1)
bool is_tiff_compressed(const std::string& path);
 
// 압축 TIFF → 비압축 TIFF로 변환
// 성공 시 true, 실패 시 false
bool decompress_tiff_to(const std::string& src_path, const std::string& dst_path);