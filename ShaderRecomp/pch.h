#pragma once

#include <Windows.h>
#include <dxcapi.h>

#include <bit>
#include <cassert>
#include <cstdint>
#include <execution>
#include <filesystem>
#include <print>
#include <format>
#include <map>
#include <string>
#include <unordered_map>
#include <xxhash.h>
#include <zstd.h>
#include <smolv.h>

template<typename T>
struct be
{
    T value;

    T get() const
    {
        if constexpr (std::is_enum_v<T>)
            return T(std::byteswap(std::underlying_type_t<T>(value)));
        else
            return std::byteswap(value);
    }

    operator T() const
    {
        return get();
    }
};  
