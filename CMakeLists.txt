cmake_minimum_required(VERSION 3.15)
project(dgit)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenSSL REQUIRED)

add_executable(dgit
    src/cli/main.cpp
    src/vcs/vcs.cpp
    src/engine/delta.cpp
    src/engine/format.cpp
)

target_include_directories(dgit PRIVATE
    src/vcs
    src/engine
    src/cli
)

target_link_libraries(dgit PRIVATE
    OpenSSL::Crypto
)