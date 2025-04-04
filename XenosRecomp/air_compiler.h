#pragma once

struct AirCompiler
{
    static std::vector<uint8_t> compile(const std::string& shaderSource);

private:
    static int makeTemporaryFile(std::string& extension);
};