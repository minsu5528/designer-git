#pragma once
#include <string>

// 저장소 초기화 (dgit init)
// root_path: .vcs 폴더를 만들 위치
int init_repository(const std::string &root_path);

// 파일 추적 등록 (dgit add)
// file_path: 추적할 파일 경로
int add_file(const std::string &file_path);

// 커밋 생성 (dgit commit)
// message: 커밋 메시지
std::string commit(const std::string &message);

// 특정 커밋으로 복원 (dgit checkout)
// commit_id: 복원할 커밋 ID
int checkout(const std::string &commit_id);

// 커밋 히스토리 출력 (dgit log)
void log();
