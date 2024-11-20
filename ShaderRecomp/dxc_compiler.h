#pragma once

struct DxcCompiler
{
    IDxcCompiler3* dxcCompiler = nullptr;

    DxcCompiler();
    ~DxcCompiler();

    IDxcBlob* compile(const std::string& shaderSource, bool compilePixelShader, bool compileLibrary, bool compileSpirv);
};
