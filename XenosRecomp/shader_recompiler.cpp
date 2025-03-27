#include "shader_recompiler.h"
#include "shader_common.h"

static constexpr char SWIZZLES[] = 
{ 
    'x',
    'y', 
    'z', 
    'w', 
    '0', 
    '1',
    '_',
    '_'
};

static constexpr const char* USAGE_TYPES[] =
{
    "float4", // POSITION
    "float4", // BLENDWEIGHT
    "uint4", // BLENDINDICES
    "uint4", // NORMAL
    "float4", // PSIZE
    "float4", // TEXCOORD
    "uint4", // TANGENT
    "uint4", // BINORMAL
    "float4", // TESSFACTOR
    "float4", // POSITIONT
    "float4", // COLOR
    "float4", // FOG
    "float4", // DEPTH
    "float4", // SAMPLE
};

static constexpr const char* USAGE_VARIABLES[] =
{
    "Position",
    "BlendWeight",
    "BlendIndices",
    "Normal",
    "PointSize",
    "TexCoord",
    "Tangent",
    "Binormal",
    "TessFactor",
    "PositionT",
    "Color",
    "Fog",
    "Depth",
    "Sample"
};

static constexpr const char* USAGE_SEMANTICS[] =
{
    "POSITION",
    "BLENDWEIGHT",
    "BLENDINDICES",
    "NORMAL",
    "PSIZE",
    "TEXCOORD",
    "TANGENT",
    "BINORMAL",
    "TESSFACTOR",
    "POSITIONT",
    "COLOR",
    "FOG",
    "DEPTH",
    "SAMPLE"
};

struct DeclUsageLocation
{
    DeclUsage usage;
    uint32_t usageIndex;
    uint32_t location;
};

// NOTE: These are specialized Vulkan locations for Unleashed Recompiled. Change as necessary. Likely not going to work with other games.
static constexpr DeclUsageLocation USAGE_LOCATIONS[] =
{
    { DeclUsage::Position, 0, 0 },
    { DeclUsage::Normal, 0, 1 },
    { DeclUsage::Tangent, 0, 2 },
    { DeclUsage::Binormal, 0, 3 },
    { DeclUsage::TexCoord, 0, 4 },
    { DeclUsage::TexCoord, 1, 5 },
    { DeclUsage::TexCoord, 2, 6 },
    { DeclUsage::TexCoord, 3, 7 },
    { DeclUsage::Color, 0, 8 },
    { DeclUsage::BlendIndices, 0, 9 },
    { DeclUsage::BlendWeight, 0, 10 },
    { DeclUsage::Color, 1, 11 },
    { DeclUsage::TexCoord, 4, 12 },
    { DeclUsage::TexCoord, 5, 13 },
    { DeclUsage::TexCoord, 6, 14 },
    { DeclUsage::TexCoord, 7, 15 },
    { DeclUsage::Position, 1, 15 },
};

static constexpr std::pair<DeclUsage, size_t> INTERPOLATORS[] =
{
    { DeclUsage::TexCoord, 0 },
    { DeclUsage::TexCoord, 1 },
    { DeclUsage::TexCoord, 2 },
    { DeclUsage::TexCoord, 3 },
    { DeclUsage::TexCoord, 4 },
    { DeclUsage::TexCoord, 5 },
    { DeclUsage::TexCoord, 6 },
    { DeclUsage::TexCoord, 7 },
    { DeclUsage::TexCoord, 8 },
    { DeclUsage::TexCoord, 9 },
    { DeclUsage::TexCoord, 10 },
    { DeclUsage::TexCoord, 11 },
    { DeclUsage::TexCoord, 12 },
    { DeclUsage::TexCoord, 13 },
    { DeclUsage::TexCoord, 14 },
    { DeclUsage::TexCoord, 15 },
    { DeclUsage::Color, 0 },
    { DeclUsage::Color, 1 }
};

static constexpr std::string_view TEXTURE_DIMENSIONS[] = 
{
    "2D",
    "3D", 
    "Cube" 
};

static FetchDestinationSwizzle getDestSwizzle(uint32_t dstSwizzle, uint32_t index)
{
    return FetchDestinationSwizzle((dstSwizzle >> (index * 3)) & 0x7);
}

uint32_t ShaderRecompiler::printDstSwizzle(uint32_t dstSwizzle, bool operand)
{
    uint32_t size = 0;

    for (size_t i = 0; i < 4; i++)
    {
        const auto swizzle = getDestSwizzle(dstSwizzle, i);
        if (swizzle >= FetchDestinationSwizzle::X && swizzle <= FetchDestinationSwizzle::W)
        {
            out += SWIZZLES[operand ? uint32_t(swizzle) : i];
            size++;
        }
    }

    return size;
}

void ShaderRecompiler::printDstSwizzle01(uint32_t dstRegister, uint32_t dstSwizzle)
{
    for (size_t i = 0; i < 4; i++)
    {
        const auto swizzle = getDestSwizzle(dstSwizzle, i);
        if (swizzle == FetchDestinationSwizzle::Zero)
        {
            indent();
            println("r{}.{} = 0.0;", dstRegister, SWIZZLES[i]);
        }
        else if (swizzle == FetchDestinationSwizzle::One)
        {
            indent();
            println("r{}.{} = 1.0;", dstRegister, SWIZZLES[i]);
        }
    }
}

void ShaderRecompiler::recompile(const VertexFetchInstruction& instr, uint32_t address)
{
    if (instr.isPredicated)
    {
        indent();
        println("if ({}p0)", instr.predicateCondition ? "" : "!");

        indent();
        out += "{\n";
        ++indentation;
    }

    indent();
    print("r{}.", instr.dstRegister);
    uint32_t size = printDstSwizzle(instr.dstSwizzle, false);

    out += " = ";

    if (size <= 1)
        out += "(float)(";
    else
        print("(float{})(", size);

    auto findResult = vertexElements.find(address);
    assert(findResult != vertexElements.end());

    switch (findResult->second.usage)
    {
    case DeclUsage::Normal:
    case DeclUsage::Tangent:
    case DeclUsage::Binormal:
        specConstantsMask |= SPEC_CONSTANT_R11G11B10_NORMAL;
        print("tfetchR11G11B10((uint4)");
        break;

    case DeclUsage::TexCoord:
        print("tfetchTexcoord(g_SwappedTexcoords, (float4)");
        break;
    }

    print("(input.i{}{})", USAGE_VARIABLES[uint32_t(findResult->second.usage)], uint32_t(findResult->second.usageIndex));

    switch (findResult->second.usage)
    {
    case DeclUsage::Normal:
    case DeclUsage::Tangent:
    case DeclUsage::Binormal:
        out += ')';
        break;

    case DeclUsage::TexCoord:
        print(", {})", uint32_t(findResult->second.usageIndex));
        break;
    }

    out += ").";
    printDstSwizzle(instr.dstSwizzle, true);

    out += ";\n";

    printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);

    if (instr.isPredicated)
    {
        --indentation;
        indent();
        out += "}\n";
    }
}

void ShaderRecompiler::recompile(const TextureFetchInstruction& instr, bool bicubic)
{
    if (instr.opcode != FetchOpcode::TextureFetch && instr.opcode != FetchOpcode::GetTextureWeights)
        return;

    if (instr.isPredicated)
    {
        indent();
        println("if ({}p0)", instr.predCondition ? "" : "!");

        indent();
        out += "{\n";
        ++indentation;
    }

    auto printSrcRegister = [&](size_t componentCount)
        {
            print("r{}.", instr.srcRegister);

            for (size_t i = 0; i < componentCount; i++)
                out += SWIZZLES[((instr.srcSwizzle >> (i * 2))) & 0x3];
        };

    std::string constName;
    const char* constNamePtr = nullptr;
#ifdef UNLEASHED_RECOMP
    bool subtractFromOne = false;
#endif

    auto findResult = samplers.find(instr.constIndex);
    if (findResult != samplers.end())
    {
        constNamePtr = findResult->second;

    #ifdef UNLEASHED_RECOMP
        subtractFromOne = hasMtxPrevInvViewProjection && strcmp(constNamePtr, "sampZBuffer") == 0;
    #endif
    }
    else
    {
        constName = fmt::format("s{}", instr.constIndex);
        constNamePtr = constName.c_str();
    }

#ifdef UNLEASHED_RECOMP
    if (instr.constIndex == 0 && instr.dimension == TextureDimension::Texture2D)
    {
        indent();
        println("pixelCoord = getPixelCoord(");
        println("#ifdef __air__");
        indent();
        println("g_Texture2DDescriptorHeap,");
        println("#endif");
        indent();
        print("{}_Texture2DDescriptorIndex, ", constNamePtr);
        printSrcRegister(2);
        out += ");\n";
    }
#endif

    indent();
    print("r{}.", instr.dstRegister);
    printDstSwizzle(instr.dstSwizzle, false);

    out += " = ";
    switch (instr.opcode)
    {
    case FetchOpcode::TextureFetch:
    {
    #ifdef UNLEASHED_RECOMP
        if (subtractFromOne)
            out += "1.0 - ";
    #endif

        out += "tfetch";
        break;
    }
    case FetchOpcode::GetTextureWeights:
    {
        out += "getWeights";
        break;
    }
    }

    std::string_view dimension;
    uint32_t componentCount = 0;

    switch (instr.dimension)
    {
    case TextureDimension::Texture1D:
        dimension = "1D";
        componentCount = 1;
        break;
    case TextureDimension::Texture2D:
        dimension = "2D";
        componentCount = 2;
        break;
    case TextureDimension::Texture3D:
        dimension = "3D";
        componentCount = 3;
        break;
    case TextureDimension::TextureCube:
        dimension = "Cube";
        componentCount = 3;
        break;
    }

    out += dimension;

#ifdef UNLEASHED_RECOMP
    if (bicubic)
        out += "Bicubic";
#endif

    println("(");

    println("#ifdef __air__");
    indent();
    println("\tg_Texture{}DescriptorHeap,", dimension);
    indent();
    println("\tg_SamplerDescriptorHeap,");
    println("#endif");

    indent();
    print("\t{0}_Texture{1}DescriptorIndex, {0}_SamplerDescriptorIndex, ", constNamePtr, dimension);
    printSrcRegister(componentCount);

    switch (instr.dimension)
    {
    case TextureDimension::Texture2D:
        print(", float2({}, {})", instr.offsetX * 0.5f, instr.offsetY * 0.5f);
        break;
    case TextureDimension::TextureCube:
        println("\n#ifdef __air__");
        indent();
        println(", &cubeMapData");
        println("#else");
        indent();
        println(", cubeMapData");
        println("#endif");
        break;
    }

    out += ").";

    printDstSwizzle(instr.dstSwizzle, true);

    out += ";\n";

    printDstSwizzle01(instr.dstRegister, instr.dstSwizzle);

    if (instr.isPredicated)
    {
        --indentation;
        indent();
        out += "}\n";
    }
}

void ShaderRecompiler::recompile(const AluInstruction& instr)
{
    if (instr.isPredicated)
    {
        indent();
        println("if ({}p0)", instr.predicateCondition ? "" : "!");

        indent(); 
        out += "{\n";
        ++indentation;
    }

    enum
    {
        VECTOR_0,
        VECTOR_1,
        VECTOR_2,
        SCALAR_0,
        SCALAR_1,
        SCALAR_CONSTANT_0,
        SCALAR_CONSTANT_1
    };

    struct OperationResult
    {
        std::string expression;
        size_t componentCount;
    };

    auto op = [&](size_t operand)
        {
            size_t reg = 0;
            size_t swizzle = 0;
            bool select = true;
            bool negate = false;
            bool abs = false;

            switch (operand)
            {
            case SCALAR_CONSTANT_0:
                reg = instr.src3Register;
                swizzle = instr.src3Swizzle;
                select = false;
                negate = instr.src3Negate;
                abs = instr.absConstants;
                break;

            case SCALAR_CONSTANT_1:
                reg = (uint32_t(instr.scalarOpcode) & 1) | (instr.src3Select << 1) | (instr.src3Swizzle & 0x3C);
                swizzle = instr.src3Swizzle;
                select = true;
                negate = instr.src3Negate;
                abs = instr.absConstants;
                break;

            default:
                switch (operand)
                {
                case VECTOR_0:
                    reg = instr.src1Register;
                    swizzle = instr.src1Swizzle;
                    select = instr.src1Select;
                    negate = instr.src1Negate;
                    break;
                case VECTOR_1:
                    reg = instr.src2Register;
                    swizzle = instr.src2Swizzle;
                    select = instr.src2Select;
                    negate = instr.src2Negate;
                    break;
                case VECTOR_2:
                case SCALAR_0:
                case SCALAR_1:
                    reg = instr.src3Register;
                    swizzle = instr.src3Swizzle;
                    select = instr.src3Select;
                    negate = instr.src3Negate;
                    break;
                }

                if (select)
                {
                    abs = (reg & 0x80) != 0;
                    reg &= 0x3F;
                }
                else
                {
                    abs = instr.absConstants;
                }

                break;
            }

            std::string regFormatted;

            if (select)
            {
                regFormatted = fmt::format("r{}", reg);
            }
            else
            {
                auto findResult = float4Constants.find(reg);
                if (findResult != float4Constants.end())
                {
                    const char* constantName = reinterpret_cast<const char*>(constantTableData + findResult->second->name);
                    if (findResult->second->registerCount > 1)
                    {
                    #ifdef UNLEASHED_RECOMP
                        if (hasMtxProjection && strcmp(constantName, "g_MtxProjection") == 0)
                        {
                            regFormatted = fmt::format("(iterationIndex == 0 ? mtxProjectionReverseZ[{0}] : mtxProjection[{0}])",
                                reg - findResult->second->registerIndex);
                        }
                        else
                    #endif
                        {
                            regFormatted = fmt::format("{}({}{})", constantName,
                                reg - findResult->second->registerIndex, instr.const0Relative ? (instr.constAddressRegisterRelative ? " + a0" : " + aL") : "");
                        }
                    }
                    else
                    {
                        assert(!instr.const0Relative && !instr.const1Relative);
                        regFormatted = constantName;
                    }
                }
                else
                {
                    assert(!instr.const0Relative && !instr.const1Relative);
                    regFormatted = fmt::format("c{}", reg);
                }
            }

            OperationResult opResult {};

            if (negate)
                opResult.expression += '-';

            if (abs)
                opResult.expression += "abs(";

            opResult.expression += regFormatted;
            opResult.expression += '.';

            switch (operand)
            {
            case VECTOR_0:
            case VECTOR_1:
            case VECTOR_2:
            {
                uint32_t mask;

                switch (instr.vectorOpcode)
                {
                case AluVectorOpcode::Dp2Add:
                    mask = (operand == VECTOR_2) ? 0b1 : 0b11;
                    break;

                case AluVectorOpcode::Dp3:
                    mask = 0b111;
                    break;

                case AluVectorOpcode::Dp4:
                case AluVectorOpcode::Max4:
                    mask = 0b1111;
                    break;

                default:
                    mask = instr.vectorWriteMask != 0 ? instr.vectorWriteMask : 0b1;
                    break;
                }

                for (size_t i = 0; i < 4; i++)
                {
                    if ((mask >> i) & 0x1) {
                        opResult.componentCount++;
                        opResult.expression += SWIZZLES[((swizzle >> (i * 2)) + i) & 0x3];
                    }
                }

                break;
            }

            case SCALAR_0:
            case SCALAR_CONSTANT_0:
                opResult.componentCount = 1;
                opResult.expression += SWIZZLES[((swizzle >> 6) + 3) & 0x3];
                break;

            case SCALAR_1:
            case SCALAR_CONSTANT_1:
                opResult.componentCount = 1;
                opResult.expression += SWIZZLES[swizzle & 0x3];
                break;
            }

            if (abs)
                opResult.expression += ")";

            return opResult;
        };

    switch (instr.vectorOpcode)
    {
    case AluVectorOpcode::KillEq:
        indent();
        println("clip(any({} == {}) ? -1 : 1);", op(VECTOR_0).expression, op(VECTOR_1).expression);
        break;
    
    case AluVectorOpcode::KillGt:
        indent();
        println("clip(any({} > {}) ? -1 : 1);", op(VECTOR_0).expression, op(VECTOR_1).expression);
        break;
    
    case AluVectorOpcode::KillGe:
        indent();
        println("clip(any({} >= {}) ? -1 : 1);", op(VECTOR_0).expression, op(VECTOR_1).expression);
        break;
    
    case AluVectorOpcode::KillNe:
        indent();
        println("clip(any({} != {}) ? -1 : 1);", op(VECTOR_0).expression, op(VECTOR_1).expression);
        break;
    }

    bool closeIfBracket = false;

    std::string_view exportRegister;
    bool vectorRegister = true;

    if (instr.exportData)
    {
        if (isPixelShader)
        {
            switch (ExportRegister(instr.vectorDest))
            {
            case ExportRegister::PSColor0:
                exportRegister = "output.oC0";
                break;        
            case ExportRegister::PSColor1:
                exportRegister = "output.oC1";
                break;        
            case ExportRegister::PSColor2:
                exportRegister = "output.oC2";
                break;            
            case ExportRegister::PSColor3:
                exportRegister = "output.oC3";
                break;           
            case ExportRegister::PSDepth:
                exportRegister = "output.oDepth";
                vectorRegister = false;
                break;
            }
        }
        else
        {
            switch (ExportRegister(instr.vectorDest))
            {
            case ExportRegister::VSPosition:
                exportRegister = "output.oPos";

            #ifdef UNLEASHED_RECOMP
                if (hasMtxProjection)
                {
                    indent();
                    out += "if ((g_SpecConstants() & SPEC_CONSTANT_REVERSE_Z) == 0 || iterationIndex == 0)\n";
                    indent();
                    out += "{\n";
                    ++indentation;

                    closeIfBracket = true;
                }
            #endif

                break;

            default:
            {
                auto findResult = interpolators.find(instr.vectorDest);
                assert(findResult != interpolators.end());
                exportRegister = findResult->second;
                break;
            }
            }
        }
    }

    if (instr.vectorOpcode >= AluVectorOpcode::SetpEqPush && instr.vectorOpcode <= AluVectorOpcode::SetpGePush)
    {
        indent();
        print("p0 = {} == 0.0 && {} ", op(VECTOR_0).expression, op(VECTOR_1).expression);

        switch (instr.vectorOpcode)
        {
        case AluVectorOpcode::SetpEqPush:
            out += "==";
            break;
        case AluVectorOpcode::SetpNePush:
            out += "!=";
            break;
        case AluVectorOpcode::SetpGtPush:
            out += ">";
            break;
        case AluVectorOpcode::SetpGePush:
            out += ">=";
            break;
        }

        out += " 0.0;\n";
    }
    else if (instr.vectorOpcode >= AluVectorOpcode::MaxA)
    {
        indent();
        println("a0 = (int)clamp(floor(({}).w + 0.5), -256.0, 255.0);", op(VECTOR_0).expression);
    }

    uint32_t vectorWriteMask = instr.vectorWriteMask;
    if (instr.exportData)
        vectorWriteMask &= ~instr.scalarWriteMask;

    if (vectorWriteMask != 0)
    {
        indent();
        if (!exportRegister.empty())
        {
            out += exportRegister;
            if (vectorRegister)
                out += '.';
        }
        else
        {
            print("r{}.", instr.vectorDest);
        }

        uint32_t vectorWriteSize = 0;

        for (size_t i = 0; i < 4; i++)
        {
            if ((vectorWriteMask >> i) & 0x1)
            {
                if (vectorRegister)
                    out += SWIZZLES[i];
                vectorWriteSize++;
            }
        }

        out += " = ";

        bool scalarRegWrite = vectorWriteSize <= 1;

        if (vectorWriteSize > 1)
            print("(float{})(", vectorWriteSize);
        else
            out += "(float)((";

        if (instr.vectorSaturate)
            out += "saturate(";

        size_t operationResultComponentCount;

        switch (instr.vectorOpcode)
        {
        case AluVectorOpcode::Add:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                operationResultComponentCount = std::max(v0.componentCount, v1.componentCount);

                print("{} + {}", v0.expression, v1.expression);
                break;
            }

        case AluVectorOpcode::Mul:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                operationResultComponentCount = std::max(v0.componentCount, v1.componentCount);

                print("{} * {}", v0.expression, v1.expression);
                break;
            }

        case AluVectorOpcode::Max:
        case AluVectorOpcode::MaxA:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                operationResultComponentCount = v0.componentCount;

                print("max({}, {})", v0.expression, v1.expression);
                break;
            }

        case AluVectorOpcode::Min:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                operationResultComponentCount = v0.componentCount;

                print("min({}, {})", v0.expression, v1.expression);
                break;
            }

        case AluVectorOpcode::Seq:
            operationResultComponentCount = 1;
            print("{} == {}", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::Sgt:
            operationResultComponentCount = 1;
            print("{} > {}", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::Sge:
            operationResultComponentCount = 1;
            print("{} >= {}", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::Sne:
            operationResultComponentCount = 1;
            print("{} != {}", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::Frc:
            {
                auto v0 = op(VECTOR_0);
                operationResultComponentCount = v0.componentCount;

                print("frac({})", v0.expression);
                break;
            }

        case AluVectorOpcode::Trunc:
            {
                auto v0 = op(VECTOR_0);
                operationResultComponentCount = v0.componentCount;

                print("trunc({})", v0.expression);
                break;
            }

        case AluVectorOpcode::Floor:
            {
                auto v0 = op(VECTOR_0);
                operationResultComponentCount = v0.componentCount;

                print("floor({})", v0.expression);
                break;
            }

        case AluVectorOpcode::Mad:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                auto v2 = op(VECTOR_2);
                operationResultComponentCount = std::max(std::max(v0.componentCount, v1.componentCount), v2.componentCount);

                print("{} * {} + {}", v0.expression, v1.expression, v2.expression);
                break;
            }

        case AluVectorOpcode::CndEq:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                auto v2 = op(VECTOR_2);
                operationResultComponentCount = std::max(v1.componentCount, v2.componentCount);

                print("selectWrapper({} == 0.0, {}, {})", v0.expression, v1.expression, v2.expression);
                break;
            }

        case AluVectorOpcode::CndGe:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                auto v2 = op(VECTOR_2);
                operationResultComponentCount = std::max(v1.componentCount, v2.componentCount);

                print("selectWrapper({} >= 0.0, {}, {})", v0.expression, v1.expression, v2.expression);
                break;
            }

        case AluVectorOpcode::CndGt:
            {
                auto v0 = op(VECTOR_0);
                auto v1 = op(VECTOR_1);
                auto v2 = op(VECTOR_2);
                operationResultComponentCount = std::max(v1.componentCount, v2.componentCount);

                print("selectWrapper({} > 0.0, {}, {})", v0.expression, v1.expression, v2.expression);
                break;
            }

        case AluVectorOpcode::Dp4:
        case AluVectorOpcode::Dp3:
            operationResultComponentCount = 1;
            print("dot({}, {})", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::Dp2Add:
            {
                auto v2 = op(VECTOR_2);
                operationResultComponentCount = v2.componentCount;

                print("dot({}, {}) + {}", op(VECTOR_0).expression, op(VECTOR_1).expression, v2.expression);
                break;
            }

        case AluVectorOpcode::Cube:
            operationResultComponentCount = 4;
            println("\n#ifdef __air__");
            indent();
            print("cube(r{}, &cubeMapData)", instr.src1Register);
            println("\n#else");
            indent();
            print("cube(r{}, cubeMapData)", instr.src1Register);
            println("\n#endif");
            break;

        case AluVectorOpcode::Max4:
            operationResultComponentCount = 4;
            print("max4({})", op(VECTOR_0).expression);
            break;

        case AluVectorOpcode::SetpEqPush:
        case AluVectorOpcode::SetpNePush:
        case AluVectorOpcode::SetpGtPush:
        case AluVectorOpcode::SetpGePush:
            {
                auto v0 = op(VECTOR_0);
                operationResultComponentCount = v0.componentCount;

                print("p0 ? 0.0 : {} + 1.0", v0.expression);
                break;
            }

        case AluVectorOpcode::KillEq:
            operationResultComponentCount = 1;
            print("any({} == {})", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::KillGt:
            operationResultComponentCount = 1;
            print("any({} > {})", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::KillGe:
            operationResultComponentCount = 1;
            print("any({} >= {})", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::KillNe:
            operationResultComponentCount = 1;
            print("any({} != {})", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;

        case AluVectorOpcode::Dst:
            operationResultComponentCount = 4;
            print("dst({}, {})", op(VECTOR_0).expression, op(VECTOR_1).expression);
            break;
        }

		out += ")";

        if (scalarRegWrite) {
            if (operationResultComponentCount > 1)
                out += ".x";

            out += ")";
        }

        if (instr.vectorSaturate)
            out += ')';

        out += ";\n";
    }

    if (instr.scalarOpcode != AluScalarOpcode::RetainPrev)
    {
        if (instr.scalarOpcode >= AluScalarOpcode::SetpEq && instr.scalarOpcode <= AluScalarOpcode::SetpRstr)
        {
            indent();
            out += "p0 = ";

            switch (instr.scalarOpcode)
            {
            case AluScalarOpcode::SetpEq:
                print("{} == 0.0", op(SCALAR_0).expression);
                break;

            case AluScalarOpcode::SetpNe:
                print("{} != 0.0", op(SCALAR_0).expression);
                break;

            case AluScalarOpcode::SetpGt:
                print("{} > 0.0", op(SCALAR_0).expression);
                break;

            case AluScalarOpcode::SetpGe:
                print("{} >= 0.0", op(SCALAR_0).expression);
                break;

            case AluScalarOpcode::SetpInv:
                print("{} == 1.0", op(SCALAR_0).expression);
                break;

            case AluScalarOpcode::SetpPop:
                print("{} - 1.0 <= 0.0", op(SCALAR_0).expression);
                break;

            case AluScalarOpcode::SetpClr:
                out += "false";
                break;

            case AluScalarOpcode::SetpRstr:
                print("{} == 0.0", op(SCALAR_0).expression);
                break;
            }

            out += ";\n";
        }

        indent();
        out += "ps = ";
        if (instr.scalarSaturate)
            out += "saturate(";

        switch (instr.scalarOpcode)
        {
        case AluScalarOpcode::Adds:
            print("{} + {}", op(SCALAR_0).expression, op(SCALAR_1).expression);
            break;

        case AluScalarOpcode::AddsPrev:
            print("{} + ps", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Muls:
            print("{} * {}", op(SCALAR_0).expression, op(SCALAR_1).expression);
            break;

        case AluScalarOpcode::MulsPrev:
        case AluScalarOpcode::MulsPrev2:
            print("{} * ps", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Maxs:
        case AluScalarOpcode::MaxAs:
        case AluScalarOpcode::MaxAsf:
            print("max({}, {})", op(SCALAR_0).expression, op(SCALAR_1).expression);
            break;

        case AluScalarOpcode::Mins:
            print("min({}, {})", op(SCALAR_0).expression, op(SCALAR_1).expression);
            break;

        case AluScalarOpcode::Seqs:
            print("{} == 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Sgts:
            print("{} > 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Sges:
            print("{} >= 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Snes:
            print("{} != 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Frcs:
            print("frac({})", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Truncs:
            print("trunc({})", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Floors:
            print("floor({})", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Exp:
            print("exp2({})", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Logc:
        case AluScalarOpcode::Log:
            print("clamp(log2({}), -FLT_MAX, FLT_MAX)", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Rcpc:
        case AluScalarOpcode::Rcpf:
        case AluScalarOpcode::Rcp:
            print("clamp(rcp({}), -FLT_MAX, FLT_MAX)", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Rsqc:
        case AluScalarOpcode::Rsqf:
        case AluScalarOpcode::Rsq:
            print("clamp(rsqrt({}), -FLT_MAX, FLT_MAX)", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Subs:
            print("{} - {}", op(SCALAR_0).expression, op(SCALAR_1).expression);
            break;

        case AluScalarOpcode::SubsPrev:
            print("{} - ps", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::SetpEq:
        case AluScalarOpcode::SetpNe:
        case AluScalarOpcode::SetpGt:
        case AluScalarOpcode::SetpGe:
            out += "p0 ? 0.0 : 1.0";
            break;

        case AluScalarOpcode::SetpInv:
            print("{0} == 0.0 ? 1.0 : {0}", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::SetpPop:
            print("p0 ? 0.0 : ({} - 1.0)", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::SetpClr:
            out += "FLT_MAX";
            break;

        case AluScalarOpcode::SetpRstr:
            print("p0 ? 0.0 : {}", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::KillsEq:
            print("{} == 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::KillsGt:
            print("{} > 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::KillsGe:
            print("{} >= 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::KillsNe:
            print("{} != 0.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::KillsOne:
            print("{} == 1.0", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Sqrt:
            print("sqrt({})", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Mulsc0:
        case AluScalarOpcode::Mulsc1:
            print("{} * {}", op(SCALAR_CONSTANT_0).expression, op(SCALAR_CONSTANT_1).expression);
            break;

        case AluScalarOpcode::Addsc0:
        case AluScalarOpcode::Addsc1:
            print("{} + {}", op(SCALAR_CONSTANT_0).expression, op(SCALAR_CONSTANT_1).expression);
            break;

        case AluScalarOpcode::Subsc0:
        case AluScalarOpcode::Subsc1:
            print("{} - {}", op(SCALAR_CONSTANT_0).expression, op(SCALAR_CONSTANT_1).expression);
            break;

        case AluScalarOpcode::Sin:
            print("sin({})", op(SCALAR_0).expression);
            break;

        case AluScalarOpcode::Cos:
            print("cos({})", op(SCALAR_0).expression);
            break;
        }

        if (instr.scalarSaturate)
            out += ')';

        out += ";\n";

        switch (instr.scalarOpcode)
        {
        case AluScalarOpcode::MaxAs:
            indent();
            println("a0 = (int)clamp(floor({} + 0.5), -256.0, 255.0);", op(SCALAR_0).expression);
            break;     
        case AluScalarOpcode::MaxAsf:
            indent();
            println("a0 = (int)clamp(floor({}), -256.0, 255.0);", op(SCALAR_0).expression);
            break;
        }
    }

    uint32_t scalarWriteMask = instr.scalarWriteMask;
    if (instr.exportData)
        scalarWriteMask &= ~instr.vectorWriteMask;

    if (scalarWriteMask != 0)
    {
        indent();
        if (!exportRegister.empty())
        {
            out += exportRegister;
            if (vectorRegister)
                out += '.';
        }
        else
        {
            print("r{}.", instr.scalarDest);
        }

        for (size_t i = 0; i < 4; i++)
        {
            if (((scalarWriteMask >> i) & 0x1) && vectorRegister)
                out += SWIZZLES[i];
        }

        out += " = ps;\n";
    }

    if (instr.exportData)
    {
        uint32_t zeroMask = instr.scalarDestRelative ? (0b1111 & ~(instr.vectorWriteMask | instr.scalarWriteMask)) : 0;
        uint32_t oneMask = instr.vectorWriteMask & instr.scalarWriteMask;

        for (size_t i = 0; i < 4; i++)
        {
            uint32_t mask = 1 << i;
            if (zeroMask & mask)
            {
                indent();
                println("{}.{} = 0.0;", exportRegister, SWIZZLES[i]);
            }
            else if (oneMask & mask)
            {
                indent();
                println("{}.{} = 1.0;", exportRegister, SWIZZLES[i]);
            }
        }
    }

    if (instr.scalarOpcode >= AluScalarOpcode::KillsEq && instr.scalarOpcode <= AluScalarOpcode::KillsOne)
    {
        indent();
        out += "clip(ps != 0.0 ? -1 : 1);\n";
    }

    if (closeIfBracket)
    {
        --indentation;
        indent();
        out += "}\n";
    }

    if (instr.isPredicated)
    {
        --indentation;
        indent();
        out += "}\n";
    }
}

void ShaderRecompiler::recompile(const uint8_t* shaderData, const std::string_view& include)
{
    const auto shaderContainer = reinterpret_cast<const ShaderContainer*>(shaderData);

    assert((shaderContainer->flags & 0xFFFFFF00) == 0x102A1100);
    assert(shaderContainer->constantTableOffset != NULL);

    out += include;
    out += '\n';

    isPixelShader = (shaderContainer->flags & 0x1) == 0;

    const auto constantTableContainer = reinterpret_cast<const ConstantTableContainer*>(shaderData + shaderContainer->constantTableOffset);
    constantTableData = reinterpret_cast<const uint8_t*>(&constantTableContainer->constantTable);

    out += "#ifdef __spirv__\n\n";

#ifdef UNLEASHED_RECOMP
    bool isMetaInstancer = false;
    bool hasIndexCount = false;
#endif

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

    #ifdef UNLEASHED_RECOMP
        if (!isPixelShader)
        {
            if (strcmp(constantName, "g_MtxProjection") == 0)
                hasMtxProjection = true;
            else if (strcmp(constantName, "g_InstanceTypes") == 0)
                isMetaInstancer = true;
            else if (strcmp(constantName, "g_IndexCount") == 0)
                hasIndexCount = true;
        }
        else
        {
            if (strcmp(constantName, "g_MtxPrevInvViewProjection") == 0)
                hasMtxPrevInvViewProjection = true;
        }
    #endif

        switch (constantInfo->registerSet)
        {
        case RegisterSet::Float4:
        {
            const char* shaderName = isPixelShader ? "Pixel" : "Vertex";

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;

                println("#define {}(INDEX) selectWrapper((INDEX) < {}, vk::RawBufferLoad<float4>(g_PushConstants.{}ShaderConstants + ({} + min(INDEX, {})) * 16, 0x10), 0.0)",
                    constantName, tailCount, shaderName, constantInfo->registerIndex.get(), tailCount - 1);
            }
            else
            {
                println("#define {} vk::RawBufferLoad<float4>(g_PushConstants.{}ShaderConstants + {}, 0x10)",
                    constantName, shaderName, constantInfo->registerIndex * 16);
            }
            
            for (uint16_t j = 0; j < constantInfo->registerCount; j++)
                float4Constants.emplace(constantInfo->registerIndex + j, constantInfo);

            break;
        }

        case RegisterSet::Sampler:
        {
            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("#define {}_Texture{}DescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {})",
                    constantName, TEXTURE_DIMENSIONS[j], j * 64 + constantInfo->registerIndex * 4);
            }

            println("#define {}_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + {})",
                constantName, std::size(TEXTURE_DIMENSIONS) * 64 + constantInfo->registerIndex * 4);

            samplers.emplace(constantInfo->registerIndex, constantName);
            break;
        }

        }
    }

    out += "\n#elif __air__\n\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

    #ifdef UNLEASHED_RECOMP
        if (!isPixelShader)
        {
            if (strcmp(constantName, "g_MtxProjection") == 0)
                hasMtxProjection = true;
            else if (strcmp(constantName, "g_InstanceTypes") == 0)
                isMetaInstancer = true;
            else if (strcmp(constantName, "g_IndexCount") == 0)
                hasIndexCount = true;
        }
        else
        {
            if (strcmp(constantName, "g_MtxPrevInvViewProjection") == 0)
                hasMtxPrevInvViewProjection = true;
        }
    #endif

        switch (constantInfo->registerSet)
        {
        case RegisterSet::Float4:
        {
            const char* shaderName = isPixelShader ? "Pixel" : "Vertex";

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;

                println("#define {}(INDEX) selectWrapper((INDEX) < {}, (*(reinterpret_cast<device float4*>(g_PushConstants.{}ShaderConstants + ({} + min(INDEX, {})) * 16))), 0.0)",
                    constantName, tailCount, shaderName, constantInfo->registerIndex.get(), tailCount - 1);
            }
            else
            {
                println("#define {} (*(reinterpret_cast<device float4*>(g_PushConstants.{}ShaderConstants + {})))",
                    constantName, shaderName, constantInfo->registerIndex * 16);
            }

            for (uint16_t j = 0; j < constantInfo->registerCount; j++)
                float4Constants.emplace(constantInfo->registerIndex + j, constantInfo);

            break;
        }

        case RegisterSet::Sampler:
        {
            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("#define {}_Texture{}DescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + {})))",
                    constantName, TEXTURE_DIMENSIONS[j], j * 64 + constantInfo->registerIndex * 4);
            }

            println("#define {}_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + {})))",
                constantName, std::size(TEXTURE_DIMENSIONS) * 64 + constantInfo->registerIndex * 4);

            samplers.emplace(constantInfo->registerIndex, constantName);
            break;
        }

        }
    }

    out += "\n#else\n\n";

    println("cbuffer {}ShaderConstants : register(b{}, space4)", isPixelShader ? "Pixel" : "Vertex", isPixelShader ? 1 : 0);
    out += "{\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Float4)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

            print("\tfloat4 {}", constantName);

            if (constantInfo->registerCount > 1)
                print("[{}]", constantInfo->registerCount.get());

            println(" : packoffset(c{});", constantInfo->registerIndex.get());

            if (constantInfo->registerCount > 1)
            {
                uint32_t tailCount = (isPixelShader ? 224 : 256) - constantInfo->registerIndex;
                println("#define {0}(INDEX) selectWrapper((INDEX) < {1}, {0}[min(INDEX, {2})], 0.0)", constantName, tailCount, tailCount - 1);
            }
        }
    }

    out += "};\n\n";

    out += "cbuffer SharedConstants : register(b2, space4)\n";
    out += "{\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Sampler)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);

            for (size_t j = 0; j < std::size(TEXTURE_DIMENSIONS); j++)
            {
                println("\tuint {}_Texture{}DescriptorIndex : packoffset(c{}.{});",
                    constantName, TEXTURE_DIMENSIONS[j], j * 4 + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
            }

            println("\tuint {}_SamplerDescriptorIndex : packoffset(c{}.{});",
                constantName, 4 * std::size(TEXTURE_DIMENSIONS) + constantInfo->registerIndex / 4, SWIZZLES[constantInfo->registerIndex % 4]);
        }
    }

    out += "\tDEFINE_SHARED_CONSTANTS();\n";
    out += "};\n\n";

    out += "#endif\n";

    for (uint32_t i = 0; i < constantTableContainer->constantTable.constants; i++)
    {
        const auto constantInfo = reinterpret_cast<const ConstantInfo*>(
            constantTableData + constantTableContainer->constantTable.constantInfo + i * sizeof(ConstantInfo));

        if (constantInfo->registerSet == RegisterSet::Bool)
        {
            const char* constantName = reinterpret_cast<const char*>(constantTableData + constantInfo->name);
            println("#define {} (1 << {})", constantName, constantInfo->registerIndex + (isPixelShader ? 16 : 0));
            boolConstants.emplace(constantInfo->registerIndex, constantName);
        }
    }

    out += '\n';

    const auto shader = reinterpret_cast<const Shader*>(shaderData + shaderContainer->shaderOffset);

    println("struct {}", isPixelShader ? "Interpolators" : "VertexShaderInput");
    out += "{\n";

    if (isPixelShader)
    {
        out += "#if __air__\n";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            println("\tfloat4 i{0}{1} [[user({2}{1})]];", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);

        out += "#else\n";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            println("\tfloat4 i{0}{1} : {2}{1};", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);

        out += "#endif\n";
    }
    else
    {
        auto vertexShader = reinterpret_cast<const VertexShader*>(shader);

        out += "#if __air__\n";

        for (uint32_t i = 0; i < vertexShader->vertexElementCount; i++)
        {
            union
            {
                VertexElement vertexElement;
                uint32_t value;
            };

            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + i];

            const char* usageType = USAGE_TYPES[uint32_t(vertexElement.usage)];

#ifdef UNLEASHED_RECOMP
            if ((vertexElement.usage == DeclUsage::TexCoord && vertexElement.usageIndex == 2 && isMetaInstancer) ||
                (vertexElement.usage == DeclUsage::Position && vertexElement.usageIndex == 1))
            {
                usageType = "uint4";
            }
#endif

            out += '\t';

            print("{0} i{1}{2}", usageType, USAGE_VARIABLES[uint32_t(vertexElement.usage)],
                uint32_t(vertexElement.usageIndex));

            for (auto& usageLocation : USAGE_LOCATIONS)
            {
                if (usageLocation.usage == vertexElement.usage && usageLocation.usageIndex == vertexElement.usageIndex)
                {
                    println(" [[attribute({})]];", usageLocation.location);
                    break;
                }
            }

            vertexElements.emplace(uint32_t(vertexElement.address), vertexElement);
        }

        out += "#else\n";

        for (uint32_t i = 0; i < vertexShader->vertexElementCount; i++)
        {
            union
            {
                VertexElement vertexElement;
                uint32_t value;
            };

            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + i];

            const char* usageType = USAGE_TYPES[uint32_t(vertexElement.usage)];

#ifdef UNLEASHED_RECOMP
            if ((vertexElement.usage == DeclUsage::TexCoord && vertexElement.usageIndex == 2 && isMetaInstancer) ||
                (vertexElement.usage == DeclUsage::Position && vertexElement.usageIndex == 1))
            {
                usageType = "uint4";
            }
#endif

            out += '\t';

            for (auto& usageLocation : USAGE_LOCATIONS)
            {
                if (usageLocation.usage == vertexElement.usage && usageLocation.usageIndex == vertexElement.usageIndex)
                {
                    print("[[vk::location({})]] ", usageLocation.location);
                    break;
                }
            }

            println("{0} i{1}{2} : {3}{2};", usageType, USAGE_VARIABLES[uint32_t(vertexElement.usage)],
                uint32_t(vertexElement.usageIndex), USAGE_SEMANTICS[uint32_t(vertexElement.usage)]);
        }

        out += "#endif\n";
    }

    out += "};\n";

    println("struct {}", isPixelShader ? "PixelShaderOutput" : "Interpolators");
    out += "{\n";

    if (isPixelShader)
    {
        out += "#if __air__\n";

        auto pixelShader = reinterpret_cast<const PixelShader*>(shader);
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR0)
            out += "\tfloat4 oC0 [[color(0)]];\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR1)
            out += "\tfloat4 oC1 [[color(1)]];\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR2)
            out += "\tfloat4 oC2 [[color(2)]];\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR3)
            out += "\tfloat4 oC3 [[color(3)]];\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_DEPTH)
            out += "\tfloat oDepth [[depth(any)]];\n";

        out += "#else\n";

        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR0)
            out += "\tfloat4 oC0 : SV_Target0;\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR1)
            out += "\tfloat4 oC1 : SV_Target1;\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR2)
            out += "\tfloat4 oC2 : SV_Target2;\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_COLOR3)
            out += "\tfloat4 oC3 : SV_Target3;\n";
        if (pixelShader->outputs & PIXEL_SHADER_OUTPUT_DEPTH)
            out += "\tfloat oDepth : SV_Depth;\n";

        out += "#endif\n";
    }
    else
    {
        out += "#if __air__\n";

        out += "\tfloat4 oPos [[position]];\n";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            print("\tfloat4 o{0}{1} [[user({2}{1})]];\n", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);

        out += "#else\n";

        out += "\tfloat4 oPos : SV_Position;\n";

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            print("\tfloat4 o{0}{1} : {2}{1};\n", USAGE_VARIABLES[uint32_t(usage)], usageIndex, USAGE_SEMANTICS[uint32_t(usage)]);

        out += "#endif\n";
    }

    out += "};\n";

    out += "#ifdef __air__\n";

    if (isPixelShader)
        out += "[[fragment]]\n";
    else
        out += "[[vertex]]\n";

    out += "#elifndef __spirv__\n";

    if (isPixelShader)
        out += "[shader(\"pixel\")]\n";
    else
        out += "[shader(\"vertex\")]\n";

    out += "#endif\n";

    println("{} shaderMain(", isPixelShader ? "PixelShaderOutput" : "Interpolators");

    if (isPixelShader)
    {
        out += "#ifdef __air__\n";

        out += "\tInterpolators input [[stage_in]],\n";
        out += "\tfloat4 iPos [[position]],\n";
        out += "\tbool iFace [[front_facing]],\n";

        out += "\tconstant Texture2DDescriptorHeap& g_Texture2DDescriptorHeap [[buffer(0)]],\n";
        out += "\tconstant Texture3DDescriptorHeap& g_Texture3DDescriptorHeap [[buffer(1)]],\n";
        out += "\tconstant TextureCubeDescriptorHeap& g_TextureCubeDescriptorHeap [[buffer(2)]],\n";
        out += "\tconstant SamplerDescriptorHeap& g_SamplerDescriptorHeap [[buffer(3)]],\n";
        out += "\tconstant PushConstants& g_PushConstants [[buffer(8)]]\n";

        out += "#else\n";

        out += "\tInterpolators input,\n";
        out += "\tin float4 iPos : SV_Position,\n";

        out += "#ifdef __spirv__\n";
        out += "\tin bool iFace : SV_IsFrontFace\n";
        out += "#else\n";
        out += "\tin uint iFace : SV_IsFrontFace\n";
        out += "#endif\n";

        out += "\n#endif\n";
    }
    else
    {
        out += "#ifdef __air__\n";
        out += "\tconstant PushConstants& g_PushConstants [[buffer(8)]],\n";
        out += "\tVertexShaderInput input [[stage_in]]\n";
        out += "#else\n";
        out += "\tVertexShaderInput input\n";
        out += "#endif\n";

    #ifdef UNLEASHED_RECOMP
        if (hasIndexCount)
        {
            out += "\t,\n";
            out += "#ifdef __air__\n";
            out += "\tuint iVertexId [[vertex_id]],\n";
            out += "\tuint iInstanceId [[instance_id]]\n";
            out += "#else\n";
            out += "\tin uint iVertexId : SV_VertexID,\n";
            out += "\tin uint iInstanceId : SV_InstanceID\n";
            out += "#endif\n";
        }
    #endif
    }

    out += ")\n";
    out += "{\n";

    std::string outputName = isPixelShader ? "PixelShaderOutput" : "Interpolators";

    out += "#ifdef __air__\n";
    println("\t{0} output = {0}{{}};", outputName);
    out += "#else\n";
    println("\t{0} output = ({0})0;", outputName);
    out += "#endif\n";

#ifdef UNLEASHED_RECOMP
    if (hasMtxProjection)
    {
        specConstantsMask |= SPEC_CONSTANT_REVERSE_Z;

        out += "\toutput.oPos = 0.0;\n";

        out += "\tfloat4x4 mtxProjection = float4x4(g_MtxProjection(0), g_MtxProjection(1), g_MtxProjection(2), g_MtxProjection(3));\n";
        out += "\tfloat4x4 mtxProjectionReverseZ = mul(mtxProjection, float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 1, 1));\n";

        out += "\tUNROLL for (int iterationIndex = 0; iterationIndex < 2; iterationIndex++)\n";
        out += "\t{\n";
    }
#endif

    if (shaderContainer->definitionTableOffset != NULL)
    {
        auto definitionTable = reinterpret_cast<const DefinitionTable*>(shaderData + shaderContainer->definitionTableOffset);
        auto definitions = definitionTable->definitions;
        while (*definitions != 0)
        {
            auto definition = reinterpret_cast<const Float4Definition*>(definitions);
            auto value = reinterpret_cast<const be<uint32_t>*>(shaderData + shaderContainer->virtualSize + definition->physicalOffset);
            for (uint16_t i = 0; i < (definition->count + 3) / 4; i++)
            {
                println("#ifdef __air__");
                println("\tfloat4 c{} = as_type<float4>(uint4(0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}));",
                    definition->registerIndex + i - (isPixelShader ? 256 : 0), value[0].get(), value[1].get(), value[2].get(), value[3].get());
                println("#else");
                println("\tfloat4 c{} = asfloat(uint4(0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}));",
                    definition->registerIndex + i - (isPixelShader ? 256 : 0), value[0].get(), value[1].get(), value[2].get(), value[3].get());
                println("#endif");

                value += 4;
            }
            definitions += 2;
        }
        ++definitions;
        while (*definitions != 0)
        {
            auto definition = reinterpret_cast<const Int4Definition*>(definitions);
            for (uint16_t i = 0; i < definition->count; i++)
            {
                union
                {
                    uint32_t value;
                    struct
                    {
                        int8_t x;
                        int8_t y;
                        int8_t z;
                        int8_t w;
                    };
                };

                value = definition->values[i].get();

                println("\tint4 i{} = int4({}, {}, {}, {});",
                    (definition->registerIndex - 8992) / 4 + i, x, y, z, w);
            }
            definitions += 2;
            definitions += definition->count;
        }

        out += "\n";
    }

    bool printedRegisters[32]{};

    uint32_t interpolatorCount = (shader->interpolatorInfo >> 5) & 0x1F;

    for (uint32_t i = 0; i < interpolatorCount; i++)
    {
        union
        {
            Interpolator interpolator;
            uint32_t value;
        };
    
        if (isPixelShader)
        {
            value = reinterpret_cast<const PixelShader*>(shader)->interpolators[i];
            println("\tfloat4 r{} = input.i{}{};", uint32_t(interpolator.reg), USAGE_VARIABLES[uint32_t(interpolator.usage)], uint32_t(interpolator.usageIndex));
            printedRegisters[interpolator.reg] = true;
        }
        else
        {
            auto vertexShader = reinterpret_cast<const VertexShader*>(shader);
            value = vertexShader->vertexElementsAndInterpolators[vertexShader->field18 + vertexShader->vertexElementCount + i];
            interpolators.emplace(i, fmt::format("output.o{}{}", USAGE_VARIABLES[uint32_t(interpolator.usage)], uint32_t(interpolator.usageIndex)));
        }
    }

    if (!isPixelShader)
    {
    #ifdef UNLEASHED_RECOMP
        if (!hasMtxProjection)
            out += "\toutput.oPos = 0.0;\n";
    #endif

        for (auto& [usage, usageIndex] : INTERPOLATORS)
            println("\toutput.o{}{} = 0.0;", USAGE_VARIABLES[uint32_t(usage)], usageIndex);

        out += "\n";
    }

    for (size_t i = 0; i < 32; i++)
    {
        if (!printedRegisters[i])
        {
            print("\tfloat4 r{} = ", i);
            if (isPixelShader && i == ((shader->fieldC >> 8) & 0xFF))
            {
                out += "float4((iPos.xy - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);\n";
            }
        #ifdef UNLEASHED_RECOMP
            else if (!isPixelShader && hasIndexCount && i == 0)
            {
                out += "float4(iVertexId + g_IndexCount.x * iInstanceId, 0.0, 0.0, 0.0);\n";
            }
        #endif
            else
            {
                out += "0.0;\n";
            }
        }
    }

    out += "\tint a0 = 0;\n";
    out += "\tint aL = 0;\n";
    out += "\tbool p0 = false;\n";
    out += "\tfloat ps = 0.0;\n";
    if (isPixelShader)
    {
#ifdef UNLEASHED_RECOMP
        out += "\tfloat2 pixelCoord = 0.0;\n";
#endif
        out += "#ifdef __air__\n";
        out += "\tCubeMapData cubeMapData = CubeMapData{};\n";
        out += "#else\n";
        out += "\tCubeMapData cubeMapData = (CubeMapData)0;\n";
        out += "#endif\n";
    }

    const be<uint32_t>* code = reinterpret_cast<const be<uint32_t>*>(shaderData + shaderContainer->virtualSize + shader->physicalOffset);

    union
    {
        ControlFlowInstruction controlFlow[2];
        struct
        {
            uint32_t code0;
            uint32_t code1;
            uint32_t code2;
            uint32_t code3;
        };
    };

    auto controlFlowCode = code;
    uint32_t instrAddress = 0;
    uint32_t instrSize = shader->size;
    bool simpleControlFlow = true;

    while (instrAddress < instrSize)
    {
        code0 = controlFlowCode[0];
        code1 = controlFlowCode[1] & 0xFFFF;
        code2 = (controlFlowCode[1] >> 16) | (controlFlowCode[2] << 16);
        code3 = controlFlowCode[2] >> 16;

        for (auto& cfInstr : controlFlow)
        {
            uint32_t address = 0;

            switch (cfInstr.opcode)
            {
            case ControlFlowOpcode::Exec:
            case ControlFlowOpcode::ExecEnd:
                address = cfInstr.exec.address;
                break;

            case ControlFlowOpcode::CondExec:
            case ControlFlowOpcode::CondExecEnd:
            case ControlFlowOpcode::CondExecPredClean:
            case ControlFlowOpcode::CondExecPredCleanEnd:
                address = cfInstr.condExec.address;
                break;

            case ControlFlowOpcode::CondExecPred:
            case ControlFlowOpcode::CondExecPredEnd:
                address = cfInstr.condExecPred.address;
                break;

            case ControlFlowOpcode::CondJmp:
            {
                if (cfInstr.condJmp.isUnconditional || cfInstr.condJmp.direction)
                    simpleControlFlow = false;
                else
                    ++ifEndLabels[cfInstr.condJmp.address];

                break;
            }
            }

            if (address != 0)
                instrSize = std::min<uint32_t>(instrSize, address * 12);
        }

        controlFlowCode += 3;
        instrAddress += 12;
    }

    if (simpleControlFlow)
    {
        out += '\n';
        indentation = 1;
    }
    else
    {
        out += "\n\tuint pc = 0;\n";
        out += "\twhile (true)\n";
        out += "\t{\n";
        out += "\t\tswitch (pc)\n";
        out += "\t\t{\n";
    }

    controlFlowCode = code;
    instrAddress = 0;
    uint32_t pc = 0;

    while (instrAddress < instrSize)
    {
        code0 = controlFlowCode[0];
        code1 = controlFlowCode[1] & 0xFFFF;
        code2 = (controlFlowCode[1] >> 16) | (controlFlowCode[2] << 16);
        code3 = controlFlowCode[2] >> 16;

        for (auto& cfInstr : controlFlow)
        {
            if (!simpleControlFlow)
            {
                indentation = 3;
                println("\t\tcase {}:", pc);
            }
            else
            {
                auto findResult = ifEndLabels.find(pc);
                if (findResult != ifEndLabels.end())
                {
                    for (uint32_t i = 0; i < findResult->second; i++)
                    {
                        --indentation;
                        indent();
                        out += "}\n";
                    }
                }
            }

            ++pc;

            uint32_t address = 0;
            uint32_t count = 0;
            uint32_t sequence = 0;
            bool shouldReturn = false;
            bool shouldCloseCurlyBracket = false;

            switch (cfInstr.opcode)
            {
            case ControlFlowOpcode::Exec:
            case ControlFlowOpcode::ExecEnd:
                address = cfInstr.exec.address;
                count = cfInstr.exec.count;
                sequence = cfInstr.exec.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::ExecEnd);
                break;

            case ControlFlowOpcode::CondExec:
            case ControlFlowOpcode::CondExecEnd:
            case ControlFlowOpcode::CondExecPredClean:
            case ControlFlowOpcode::CondExecPredCleanEnd:
                address = cfInstr.condExec.address;
                count = cfInstr.condExec.count;
                sequence = cfInstr.condExec.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::CondExecEnd || cfInstr.opcode == ControlFlowOpcode::CondExecEnd);
                break;

            case ControlFlowOpcode::CondExecPred:
            case ControlFlowOpcode::CondExecPredEnd:
                address = cfInstr.condExecPred.address;
                count = cfInstr.condExecPred.count;
                sequence = cfInstr.condExecPred.sequence;
                shouldReturn = (cfInstr.opcode == ControlFlowOpcode::CondExecPredEnd);
                break;

            case ControlFlowOpcode::LoopStart:
                if (simpleControlFlow)
                {
                    indent();
                #ifdef UNLEASHED_RECOMP
                    print("UNROLL ");
                #endif
                    println("for (aL = 0; aL < i{}.x; aL++)", uint32_t(cfInstr.loopStart.loopId));
                    indent();
                    out += "{\n";
                    ++indentation;
                }
                else 
                {
                    out += "\t\t\taL = 0;\n";
                }
                break;

            case ControlFlowOpcode::LoopEnd:
                if (simpleControlFlow)
                {
                    --indentation;
                    indent();
                    out += "}\n";
                }
                else
                {
                    out += "\t\t\t++aL;\n";
                    println("\t\t\tif (aL < i{}.x)", uint32_t(cfInstr.loopEnd.loopId));
                    out += "\t\t\t{\n";
                    println("\t\t\t\tpc = {};", uint32_t(cfInstr.loopEnd.address));
                    out += "\t\t\t\tcontinue;\n";
                    out += "\t\t\t}\n";
                }
                break;

            case ControlFlowOpcode::CondJmp:
            {
                if (cfInstr.condJmp.isUnconditional)
                {
                    assert(!simpleControlFlow);
                    println("\t\t\tpc = {};", uint32_t(cfInstr.condJmp.address));
                    out += "\t\t\tcontinue;\n";
                }
                else
                {
                    indent();
                    if (cfInstr.condJmp.isPredicated)
                    {
                        println("if ({}p0)", cfInstr.condJmp.condition ^ simpleControlFlow ? "" : "!");
                    }
                    else
                    {
                        auto findResult = boolConstants.find(cfInstr.condJmp.boolAddress);
                        if (findResult != boolConstants.end())
                            println("if ((g_Booleans & {}) {}= 0)", findResult->second, cfInstr.condJmp.condition ^ simpleControlFlow ? "!" : "=");
                        else
                            println("if (b{} {}= 0)", uint32_t(cfInstr.condJmp.boolAddress), cfInstr.condJmp.condition ^ simpleControlFlow ? "!" : "=");
                    }

                    if (simpleControlFlow)
                    {
                        indent();
                        out += "{\n";
                        ++indentation;
                    }
                    else
                    {
                        out += "\t\t\t{\n";
                        println("\t\t\t\tpc = {};", uint32_t(cfInstr.condJmp.address));
                        out += "\t\t\t\tcontinue;\n";
                        out += "\t\t\t}\n";
                    }
                }
                break;
            }
            }

            auto instructionCode = code + address * 3;
            
            for (uint32_t i = 0; i < count; i++)
            {
                union
                {
                    VertexFetchInstruction vertexFetch;
                    TextureFetchInstruction textureFetch;
                    AluInstruction alu;
                    struct
                    {
                        uint32_t code0;
                        uint32_t code1;
                        uint32_t code2;
                    };
                };
            
                code0 = instructionCode[0];
                code1 = instructionCode[1];
                code2 = instructionCode[2];
            
                if ((sequence & 0x1) != 0)
                {
                    if (vertexFetch.opcode == FetchOpcode::VertexFetch)
                    {
                        recompile(vertexFetch, address + i);
                    }
                    else
                    {
                    #ifdef UNLEASHED_RECOMP
                        if (textureFetch.constIndex == 10) // g_GISampler
                        {
                            specConstantsMask |= SPEC_CONSTANT_BICUBIC_GI_FILTER;

                            indent();
                            out += "if (g_SpecConstants() & SPEC_CONSTANT_BICUBIC_GI_FILTER)\n";
                            indent();
                            out += "{\n";

                            ++indentation;
                            recompile(textureFetch, true);
                            --indentation;

                            indent();
                            out += "}\n";
                            indent();
                            out += "else\n";
                            indent();
                            out += "{\n";

                            ++indentation;
                            recompile(textureFetch, false);
                            --indentation;

                            indent();
                            out += "}\n";
                        }
                        else
                    #endif
                        {
                            recompile(textureFetch, false);
                        }
                    }
                }
                else
                {
                    recompile(alu);
                }
            
                sequence >>= 2;
                instructionCode += 3;
            }

            if (shouldReturn)
            {
                if (isPixelShader)
                {
                    specConstantsMask |= SPEC_CONSTANT_ALPHA_TEST;

                    indent();
                    out += "BRANCH if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST)\n";
                    indent();
                    out += "{\n";

                    indent();
                    out += "\tclip(output.oC0.w - g_AlphaThreshold);\n";

                    indent();
                    out += "}\n";

                #ifdef UNLEASHED_RECOMP
                    specConstantsMask |= SPEC_CONSTANT_ALPHA_TO_COVERAGE;

                    indent();
                    out += "else if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TO_COVERAGE)\n";
                    indent();
                    out += "{\n";

                    indent();
                    out += "\toutput.oC0.w *= 1.0 + computeMipLevel(pixelCoord) * 0.25;\n";
                    indent();
                    out += "\toutput.oC0.w = 0.5 + (output.oC0.w - g_AlphaThreshold) / max(fwidth(output.oC0.w), 1e-6);\n";

                    indent();
                    out += "}\n";
                #endif
                }
                else
                {
                    out += "\toPos.xy += g_HalfPixelOffset * oPos.w;\n";
                }

                if (simpleControlFlow)
                {
                    indent();
                #ifdef UNLEASHED_RECOMP
                    if (hasMtxProjection)
                    {
                        out += "continue;\n";
                    }
                    else
                #endif
                    {
                        out += "return output;\n";
                    }
                }
                else
                {
                    out += "\t\t\tbreak;\n";
                }
            }

            if (shouldCloseCurlyBracket)
            {
                --indentation;
                indent();
                out += "}\n";
            }
        }

        controlFlowCode += 3;
        instrAddress += 12;
    }

    if (!simpleControlFlow)
    {
        out += "\t\t\tbreak;\n";
        out += "\t\t}\n";
        out += "\t\tbreak;\n";
        out += "\t}\n";
    }

#ifdef UNLEASHED_RECOMP
    if (hasMtxProjection)
        out += "\t}\n";
#endif

    out += "\treturn output;\n";

    out += "}";
}
