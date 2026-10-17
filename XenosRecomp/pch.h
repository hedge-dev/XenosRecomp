#pragma once

#ifdef _WIN32
#include <Windows.h>
#endif

#include <dxcapi.h>

#include <bit>
#include <cassert>
#include <cstdint>
#include <execution>
#include <filesystem>
#include <map>
#include <smolv.h>
#include <fmt/core.h>
#include <string>
#include <unordered_map>
#include <xxhash.h>
#include <zstd.h>

#if defined(_MSC_VER)
	#define BYTE_SWAP_64 _byteswap_uint64
	#define BYTE_SWAP_32 _byteswap_ulong
	#define BYTE_SWAP_16 _byteswap_ushort
#else
	#define BYTE_SWAP_64 __builtin_bswap64
	#define BYTE_SWAP_32 __builtin_bswap32
	#define BYTE_SWAP_16 __builtin_bswap16
#endif

template<typename T>
static T byteSwap(T value)
{
    if constexpr (sizeof(T) == 1)
        return value;
    else if constexpr (sizeof(T) == 2)
        return static_cast<T>(BYTE_SWAP_16(static_cast<uint16_t>(value)));
    else if constexpr (sizeof(T) == 4)
        return static_cast<T>(BYTE_SWAP_32(static_cast<uint32_t>(value)));
    else if constexpr (sizeof(T) == 8) 
        return static_cast<T>(BYTE_SWAP_64(static_cast<uint64_t>(value)));

    assert(false && "Unexpected byte size.");
    return value;
}

template<typename T>
struct be
{
    T value;

    T get() const
    {
        if constexpr (std::is_enum_v<T>)
            return T(byteSwap(std::underlying_type_t<T>(value)));
        else
            return byteSwap(value);
    }

    operator T() const
    {
        return get();
    }
};  
