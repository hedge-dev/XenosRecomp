#include "dxc_compiler.h"

DxcCompiler::DxcCompiler()
{
    HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));
}

DxcCompiler::~DxcCompiler()
{
    dxcCompiler->Release();
}

IDxcBlob* DxcCompiler::compile(const std::string& shaderSource, bool compilePixelShader, bool compileLibrary, bool compileSpirv)
{
    DxcBuffer source{};
    source.Ptr = shaderSource.c_str();
    source.Size = shaderSource.size();

    const wchar_t* args[32]{};
    uint32_t argCount = 0;

    const wchar_t* target = nullptr;
    if (compileLibrary)
    {
        assert(!compileSpirv);
        target = L"-T lib_6_3";
    }
    else
    {
        if (compilePixelShader)
            target = L"-T ps_6_0";
        else
            target = L"-T vs_6_0";
    }

    args[argCount++] = target;
    args[argCount++] = L"-HV 2021";
    args[argCount++] = L"-all-resources-bound";

    if (compileSpirv)
    {
        args[argCount++] = L"-spirv";
        args[argCount++] = L"-fvk-use-dx-layout";

        if (!compilePixelShader)
            args[argCount++] = L"-fvk-invert-y";
    }
    else
    {
        args[argCount++] = L"-Wno-ignored-attributes";
        args[argCount++] = L"-Qstrip_reflect";
    }

    args[argCount++] = L"-Qstrip_debug";

#ifdef UNLEASHED_RECOMP
    args[argCount++] = L"-DUNLEASHED_RECOMP";
#endif

    IDxcResult* result = nullptr;
    HRESULT hr = dxcCompiler->Compile(&source, args, argCount, nullptr, IID_PPV_ARGS(&result));

    IDxcBlob* object = nullptr;
    if (SUCCEEDED(hr))
    {
        assert(result != nullptr);

        HRESULT status;
        hr = result->GetStatus(&status);
        assert(SUCCEEDED(hr));

        if (FAILED(status))
        {
            if (result->HasOutput(DXC_OUT_ERRORS))
            {
                IDxcBlobUtf8* errors = nullptr;
                hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
                assert(SUCCEEDED(hr) && errors != nullptr);

                fputs(errors->GetStringPointer(), stderr);

                errors->Release();
            }
        }
        else
        {
            hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
            assert(SUCCEEDED(hr) && object != nullptr);
        }

        result->Release();
    }
    else
    {
        assert(result == nullptr);
    }

    return object;
}
