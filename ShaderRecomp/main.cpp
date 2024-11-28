#include "shader.h"
#include "shader_recompiler.h"
#include "dxc_compiler.h"

static std::unique_ptr<uint8_t[]> readAllBytes(const char* filePath, size_t& fileSize)
{
    FILE* file = fopen(filePath, "rb");
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    auto data = std::make_unique<uint8_t[]>(fileSize);
    fread(data.get(), 1, fileSize, file);
    fclose(file);
    return data;
}

static void writeAllBytes(const char* filePath, const void* data, size_t dataSize)
{
    FILE* file = fopen(filePath, "wb");
    fwrite(data, 1, dataSize, file);
    fclose(file);
}

struct RecompiledShader
{
    uint8_t* data = nullptr;
    IDxcBlob* dxil = nullptr;
    std::vector<uint8_t> spirv;
    uint32_t specConstantsMask = 0;
};

int main(int argc, char** argv)
{
    const char* input =
#ifdef SHADER_RECOMP_INPUT 
        SHADER_RECOMP_INPUT
#else
        argv[1]
#endif
    ;

    const char* output =
#ifdef SHADER_RECOMP_OUTPUT 
        SHADER_RECOMP_OUTPUT
#else
        argv[2]
#endif
        ;
    
    const char* includeInput =
#ifdef SHADER_RECOMP_INCLUDE_INPUT
        SHADER_RECOMP_INCLUDE_INPUT
#else
        argv[3]
#endif
        ;

    size_t includeSize = 0;
    auto includeData = readAllBytes(includeInput, includeSize);
    std::string_view include(reinterpret_cast<const char*>(includeData.get()), includeSize);

    if (std::filesystem::is_directory(input))
    {
        std::vector<std::unique_ptr<uint8_t[]>> files;
        std::map<XXH64_hash_t, RecompiledShader> shaders;

        for (auto& file : std::filesystem::directory_iterator(input))
        {
            size_t fileSize = 0;
            auto fileData = readAllBytes(file.path().string().c_str(), fileSize);
            bool foundAny = false;

            for (size_t i = 0; fileSize > sizeof(ShaderContainer) && i < fileSize - sizeof(ShaderContainer) - 1;)
            {
                auto shaderContainer = reinterpret_cast<const ShaderContainer*>(fileData.get() + i);
                size_t dataSize = shaderContainer->virtualSize + shaderContainer->physicalSize;

                if ((shaderContainer->flags & 0xFFFFFF00) == 0x102A1100 &&
                    dataSize <= (fileSize - i) &&
                    shaderContainer->field1C == 0 &&
                    shaderContainer->field20 == 0)
                {
                    XXH64_hash_t hash = XXH3_64bits(shaderContainer, dataSize);
                    auto shader = shaders.try_emplace(hash);
                    if (shader.second)
                    {
                        shader.first->second.data = fileData.get() + i;
                        foundAny = true;
                    }

                    i += dataSize;
                }
                else
                {
                    i += sizeof(uint32_t);
                }
            }

            if (foundAny)
                files.emplace_back(std::move(fileData));
        }

        std::atomic<uint32_t> progress = 0;

        std::for_each(std::execution::par_unseq, shaders.begin(), shaders.end(), [&](auto& hashShaderPair)
            {
                auto& shader = hashShaderPair.second;

                thread_local ShaderRecompiler recompiler;
                recompiler = {};
                recompiler.recompile(shader.data, include);

                shader.specConstantsMask = recompiler.specConstantsMask;

                thread_local DxcCompiler dxcCompiler;
                shader.dxil = dxcCompiler.compile(recompiler.out, recompiler.isPixelShader, recompiler.specConstantsMask != 0, false);
                IDxcBlob* spirv = dxcCompiler.compile(recompiler.out, recompiler.isPixelShader, false, true);

                assert(shader.dxil != nullptr && spirv != nullptr);
                assert(*(reinterpret_cast<uint32_t*>(shader.dxil->GetBufferPointer()) + 1) != 0 && "DXIL was not signed properly!");

                bool result = smolv::Encode(spirv->GetBufferPointer(), spirv->GetBufferSize(), shader.spirv, smolv::kEncodeFlagStripDebugInfo);
                assert(result);

                spirv->Release();

                size_t currentProgress = ++progress;
                if ((currentProgress % 10) == 0 || (currentProgress == shaders.size() - 1))
                    std::println("Recompiling shaders... {}%", currentProgress / float(shaders.size()) * 100.0f);
            });

        std::println("Creating shader cache...");

        StringBuffer f;
        f.println("#include \"shader_cache.h\"");
        f.println("ShaderCacheEntry g_shaderCacheEntries[] = {{");

        std::vector<uint8_t> dxil;
        std::vector<uint8_t> spirv;

        for (auto& [hash, shader] : shaders)
        {
            f.println("\t{{ 0x{:X}, {}, {}, {}, {}, {} }},",
                hash, dxil.size(), shader.dxil->GetBufferSize(), spirv.size(), shader.spirv.size(), shader.specConstantsMask);

            dxil.insert(dxil.end(), reinterpret_cast<uint8_t*>(shader.dxil->GetBufferPointer()),
                reinterpret_cast<uint8_t*>(shader.dxil->GetBufferPointer()) + shader.dxil->GetBufferSize());    
            
            spirv.insert(spirv.end(), shader.spirv.begin(), shader.spirv.end());
        }

        f.println("}};");

        std::println("Compressing DXIL cache...");

        int level = ZSTD_maxCLevel();
        //level = ZSTD_defaultCLevel();
        std::vector<uint8_t> dxilCompressed(ZSTD_compressBound(dxil.size()));
        dxilCompressed.resize(ZSTD_compress(dxilCompressed.data(), dxilCompressed.size(), dxil.data(), dxil.size(), level));

        f.print("const uint8_t g_compressedDxilCache[] = {{");

        for (auto data : dxilCompressed)
            f.print("{},", data);

        f.println("}};");

        std::println("Compressing SPIRV cache...");

        std::vector<uint8_t> spirvCompressed(ZSTD_compressBound(spirv.size()));
        spirvCompressed.resize(ZSTD_compress(spirvCompressed.data(), spirvCompressed.size(), spirv.data(), spirv.size(), level));

        f.print("const uint8_t g_compressedSpirvCache[] = {{");

        for (auto data : spirvCompressed)
            f.print("{},", data);

        f.println("}};");

        f.println("const size_t g_shaderCacheEntryCount = {};", shaders.size());
        f.println("const size_t g_dxilCacheCompressedSize = {};", dxilCompressed.size());
        f.println("const size_t g_dxilCacheDecompressedSize = {};", dxil.size());
        f.println("const size_t g_spirvCacheCompressedSize = {};", spirvCompressed.size());
        f.println("const size_t g_spirvCacheDecompressedSize = {};", spirv.size());

        writeAllBytes(output, f.out.data(), f.out.size());
    }
    else
    {
        ShaderRecompiler recompiler;
        size_t fileSize;
        recompiler.recompile(readAllBytes(input, fileSize).get(), include);
        writeAllBytes(output, recompiler.out.data(), recompiler.out.size());
    }

    return 0;
}
