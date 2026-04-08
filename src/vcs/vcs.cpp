#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "vcs.h"

namespace fs = std::filesystem;

int init_repository(const std::string &root_path)
{
    fs::path vcs = root_path + "/.vcs";

    // 이미 초기화된 경우 방어
    if (fs::exists(vcs))
    {
        std::cerr << "이미 dgit 저장소가 존재합니다.\n";
        return -1;
    }

    // 폴더 생성
    fs::create_directories(vcs / "objects" / "base");
    fs::create_directories(vcs / "objects" / "deltas");
    fs::create_directories(vcs / "commits");

    // 빈 파일 생성
    std::ofstream(vcs / "index").close(); // 빈 index
    std::ofstream(vcs / "HEAD").close();  // 빈 HEAD (초기값 "")

    std::cout << "dgit 저장소가 초기화되었습니다: " << vcs << "\n";
    return 0;
}
