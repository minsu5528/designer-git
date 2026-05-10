#pragma once
#include <string>

/**
 * is_compressed_format
 * 파일 헤더(magic bytes)를 읽어 압축 포맷 여부를 판별한다.
 *
 * 압축 포맷(true): jpg, png, gif, zip, gzip, zstd, mp4
 * 비압축 포맷(false): fbx, exr, tiff, obj 및 기타
 *
 * @param filepath 판별할 파일 경로
 * @return         true면 전체 저장, false면 delta 추출
 */
bool is_compressed_format(const std::string& filepath);

/**
 * should_use_full_copy
 * 두 파일을 샘플링해서 변경률이 높으면 전체 저장을 권장한다.
 * 파일 앞/중간/뒤 각 100MB 구간에서 16KB 블록 단위로 변경 비율 추정.
 *
 * @param prev_path 직전 커밋 파일 경로
 * @param curr_path 현재 파일 경로
 * @param threshold 변경률 임계치 (기본값 0.8 = 80%)
 * @return          true면 전체 저장, false면 delta 생성
 */
bool should_use_full_copy(const std::string& prev_path,
                          const std::string& curr_path,
                          double threshold = 0.8);