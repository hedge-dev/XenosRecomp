cmake_minimum_required(VERSION 3.20)

include($ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
set(CMAKE_CXX_STANDARD 17)

project("ShaderRecomp-ALL")

set(SHADER_RECOMP_THIRDPARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

add_subdirectory(${SHADER_RECOMP_THIRDPARTY_ROOT})
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ShaderRecomp")
