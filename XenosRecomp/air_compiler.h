#pragma once

#include <string>
#include <vector>

class AirCompiler
{
public:
    [[nodiscard]] static std::vector<uint8_t> compile(const std::string& shaderSource);
};