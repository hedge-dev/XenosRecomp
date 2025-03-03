if (NOT TARGET fmt::fmt)
    add_subdirectory("${XENOS_RECOMP_THIRDPARTY_ROOT}/fmt")
endif()

if (NOT TARGET xxHash::xxhash)
    add_subdirectory("${XENOS_RECOMP_THIRDPARTY_ROOT}/xxHash/cmake_unofficial")
endif()

if (NOT TARGET libzstd)
    add_subdirectory("${XENOS_RECOMP_THIRDPARTY_ROOT}/zstd/build/cmake")
endif()

if (NOT TARGET Microsoft::DirectXShaderCompiler)
    add_subdirectory("${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin")
endif()
