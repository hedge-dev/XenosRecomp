project(XenosRecomp)

if (WIN32)
    option(XENOS_RECOMP_DXIL "Generate DXIL shader cache" ON)
endif()

set(SMOLV_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/smol-v/source")

set(DXC_BIN_PATH "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin")
set(DXCOMPILER_INC "${DXC_BIN_PATH}/inc")
if (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
    if (WIN32)
        set(DXCOMPILER_LIB "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/dxcompiler.lib")
    elseif (APPLE)
        set(DXCOMPILER_LIB "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/libdxcompiler.dylib")
    else()
        set(DXCOMPILER_LIB "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/libdxcompiler.so")
    endif()
else()
    if (WIN32)
        set(DXCOMPILER_LIB "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/arm64/dxcompiler.lib")
    elseif (APPLE)
        set(DXCOMPILER_LIB "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/arm64/libdxcompiler.dylib")
    else()
        set(DXCOMPILER_LIB "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/arm64/libdxcompiler.so")
    endif()
endif()

add_executable(XenosRecomp 
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

target_link_libraries(XenosRecomp PRIVATE
    ${DXCOMPILER_LIB}
    xxHash::xxhash
    libzstd_static
    fmt::fmt)

target_include_directories(XenosRecomp PRIVATE ${SMOLV_SOURCE_DIR} ${DXCOMPILER_INC})

target_precompile_headers(XenosRecomp PRIVATE pch.h)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(XenosRecomp PRIVATE -Wno-switch -Wno-unused-variable -Wno-null-arithmetic -fms-extensions)

    include(CheckCXXSymbolExists)
    check_cxx_symbol_exists(_LIBCPP_VERSION version LIBCPP)
    if(LIBCPP)
        # Allows using std::execution
        target_compile_options(XenosRecomp PRIVATE -fexperimental-library)
    endif()
endif()

if (WIN32)
    target_compile_definitions(XenosRecomp PRIVATE _CRT_SECURE_NO_WARNINGS)
    find_file(DIRECTX_DXIL_LIBRARY "dxil.dll")
    file(COPY ${DIRECTX_DXIL_LIBRARY} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()

if (XENOS_RECOMP_DXIL)
    target_compile_definitions(XenosRecomp PRIVATE XENOS_RECOMP_DXIL)
endif()
