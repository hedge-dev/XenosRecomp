project(ShaderRecomp)

if (WIN32)
    option(SHADER_RECOMP_DXIL "Generate DXIL shader cache" ON)
endif()

set(SMOLV_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/smol-v/source")

add_executable(ShaderRecomp 
    constant_table.h
    dxc_compiler.cpp
    dxc_compiler.h
    main.cpp
    pch.h
    shader.h
    shader_code.h
    shader_recompiler.cpp
    shader_recompiler.h
    "${SMOLV_SOURCE_DIR}/smolv.cpp")

find_package(directx-dxc CONFIG REQUIRED)
find_package(xxhash CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

target_link_libraries(ShaderRecomp PRIVATE
    Microsoft::DirectXShaderCompiler
    xxHash::xxhash
    $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,zstd::libzstd_shared,zstd::libzstd_static>
    fmt::fmt)

target_include_directories(ShaderRecomp PRIVATE ${SMOLV_SOURCE_DIR})

target_precompile_headers(ShaderRecomp PRIVATE pch.h)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ShaderRecomp PRIVATE -Wno-switch -Wno-unused-variable)
endif()

target_compile_definitions(ShaderRecomp PRIVATE _CRT_SECURE_NO_WARNINGS)

if (SHADER_RECOMP_DXIL)
    target_compile_definitions(ShaderRecomp PRIVATE SHADER_RECOMP_DXIL)
endif()

file(COPY ${PACKAGE_PREFIX_DIR}/bin/dxil.dll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
