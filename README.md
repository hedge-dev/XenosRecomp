# XenosRecomp

XenosRecomp is a tool that converts Xbox 360 shader binaries to HLSL. The resulting files can be recompiled to DXIL and SPIR-V using the DirectX Shader Compiler (DXC) for use in Direct3D 12 (D3D12) and Vulkan.

The current implementation is designed around [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), a recompilation project that implements a translation layer for the renderer rather than emulating the Xbox 360 GPU. Unleashed Recompiled specific implementations are placed under the `UNLEASHED_RECOMP` preprocessor macro.

Users are expected to modify the recompiler to fit their needs. **Do not expect the recompiler to work out of the box.**

## Implementation Details

Several components of the recompiler are currently incomplete or missing. Unimplemented or inaccurate features exist mainly because they were either unnecessary for Unleashed Recompiled or did not cause visible issues.

### Shader Container

Xbox 360 shaders are stored in a container that includes constant buffer reflection data, definitions, interpolators, vertex declarations, instructions, and more. It has been reverse-engineered just enough for use in Unleashed Recompiled, but additional research may be needed for other games.

### Instructions

Vector/ALU instructions are converted directly and should work in most cases.

Issues might happen when instructions perform dynamic constant indexing on multiple operands.

Instructions that result in `INF` or `NaN` might not be handled correctly. Most operations are clamped to `FLT_MAX`, but their behavior has not been verified in all scenarios.

Dynamic register indexing is unimplemented. A possible solution is converting registers into an array that instructions dynamically index into, instead of treating them as separate local variables.

### Control Flow

Since HLSL does not support `goto`, control flow instructions are implemented using a `while` loop with a `switch` statement, where a local `pc` variable determines the currently executing block.

The current implementation has not been thoroughly tested, as Sonic Unleashed contains very few shaders with complex control flow. However, any issues should be relatively easy to fix if problematic cases can be found.

For shaders with simple control flow, the recompiler may choose to flatten it, removing the while loop and switch statements. This allows DXC to optimize the shader more efficiently.

### Constants

Both vertex and pixel shader stages use three constant buffers:

* Vertex shader constants: 4096 bytes (256 `float4` registers)
* Pixel shader constants: 3584 bytes (224 `float4` registers)
* Shared constants: Used specifically by Unleashed Recompiled

Vertex and pixel shader constants are copied directly from the guest render device, and shaders expect them in little-endian format.

Constant buffer registers are populated using reflection data embedded in the shader binaries. If this data is missing, the recompiler will not function. However, support can be added by defining a `float4` array that covers the entire register range.

Integer constants are unimplemented. If the target game requires them, you will need to make new constant buffer slots or append them to the existing ones.

Vertex and pixel shader boolean constants each contain 16 elements. These are packed into a 32-bit integer and stored in the shared constants buffer, where the Nth bit represents the value of the Nth boolean register. The Xbox 360 GPU supposedly supports up to 128 boolean registers, which may require increasing the size of the `g_Booleans` data type for other games.

All constant buffers are implemented as root constant buffers in D3D12, making them easy to upload to the GPU using a linear allocator. In Vulkan, the GPU virtual addresses of constant buffers are passed as push constants. Constants are accessed via preprocessor macros that load values from the GPU virtual addresses using `vk::RawBufferLoad`. These macros ensure the shader function body remains the same for both DXIL and SPIR-V.

Out-of-bounds dynamic constant accesses should return 0. However, since root constant buffers in D3D12 and raw buffer loads in Vulkan do not enforce this behavior, the shader developer must handle it. To solve this, each dynamic index access is clamped to the valid range, and out-of-bounds registers are forced to become 0.

### Vertex Fetch

A common approach to vertex fetching is passing vertex data as a shader resource view and building special shaders depending on the vertex declaration. Instead, Unleashed Recompiled converts vertex declarations into native D3D12/Vulkan input declarations, allowing vertex shaders to receive data as inputs. While this has its limitations, it removes the need for runtime shader permutation compilation based on vertex declarations.

Unleashed Recompiled endian swaps vertex data before uploading it to the GPU by treating buffers as arrays of 32-bit integers. This causes the element order for 8-bit and 16-bit vertex formats to be swizzled. While no visual errors have been observed for 8-bit formats, 16-bit formats get swizzled to YXWZ. This is corrected using a `g_SwappedTexcoords` variable in the shared constants buffer, where each bit indicates whether the corresponding `TEXCOORD` semantic requires re-swizzling. While this assumption holds for Sonic Unleashed, other games may require additional support for other semantics.

Xbox 360 supports the `R11G11B10` vertex format, which is unsupported on desktop hardware. The recompiler implements this by using a specialization constant that manually unpacks this format for `NORMAL`, `TANGENT` and `BINORMAL` semantics in the vertex shader. Similar to `TEXCOORD` swizzling, this assumes the format is only used for these semantics.

Certain semantics are forced to be `uint4` instead of `float4` for specific shaders in Sonic Unleashed. This is also something that needs to be handled manually for other games.

Instanced geometry is handled completely manually on the Xbox 360. In Sonic Unleashed, the index buffer is passed as a vertex stream, and shaders use it to arbitrarily fetch vertex data, relying on a `g_IndexCount` constant to determine the index of the current instance. Unleashed Recompiled handles this by expecting instanced data to be in the second vertex stream and the index buffer to be in the `POSITION1` semantic. This behavior is completely game specific and must be manually implemented for other games.

Vulkan vertex locations are currently hardcoded for Unleashed Recompiled, chosen based on Sonic Unleashed's shaders while taking the 16 location limit into account. A generic solution would assign unique locations per vertex shader and dynamically create vertex declarations at runtime.

Mini vertex fetch instructions and vertex fetch bindings are unimplemented.

### Textures & Samplers

Textures and samplers use a bindless approach. Descriptor indices are stored in the shared constant buffer, with separate indices for each texture type to prevent mismatches in the shader. 1D textures are unimplemented but could be added easily.

Several texture fetch features, such as specifying LOD levels or sampler filters, are unimplemented. Currently, only the pixel offset value is supported, which is primarily used for shadow mapping.

Some Xbox 360 sampler types may be unsupported on desktop hardware. These cases are unhandled and require specialized implementations in the recompiler.

Cube textures are normally sampled using the `cube` instruction, which computes the face index and 2D texture coordinates. This can be implemented on desktop hardware by sampling `Texture2DArray`, however this lacks linear filtering across cube edges. The recompiler instead stores an array of cube map directions locally. Each `cube` instruction stores a direction in this array, and the output register holds the direction index. When the shader performs a texture fetch, the direction is dynamically retrieved from the array and used in `TextureCube` sampling. DXC optimizes this array away, ensuring the final DXIL/SPIR-V shader uses the direction directly.

This approach works well for simple control flow but may cause issues with complex shaders where optimizations might fail, leading to the array actually being dynamically indexed. A proper solution could implement the `cube` instruction exactly as the hardware does, and then reverse this computation during texture sampling. I chose not to do this approach in the end, as DXC was unable to optimize away redundant computations due to the lossy nature of the calculation.

### Specialization Constants

The recompiler implements several specialization constants, primarily as enhancements for Unleashed Recompiled. Currently, these are simple flags that enable or disable specific shader behaviors. The generic ones include:

- A flag indicating that the `NORMAL`, `TANGENT`, and `BINORMAL` semantics use the `R11G11B10` vertex format, enabling manual unpacking in the vertex shader.
- A flag indicating that the pixel shader performs alpha testing. Since modern desktop hardware lacks a fixed function pipeline for alpha testing, this flag inserts a "less than alpha threshold" check at the end of the pixel shader. Additional comparison types may need to be implemented depending on the target game.

While specialization constants are straightforward to implement in SPIR-V, DXIL lacks native support for them. This is solved by compiling shaders as libraries with a declared, but unimplemented function that returns the specialization constant value. At runtime, Unleashed Recompiled generates an implementation of this function, compiles it into a library, and links it with the shader to produce a final specialized shader binary. For more details on this technique, [check out this article](https://therealmjp.github.io/posts/dxil-linking/).

### Other Unimplemented Features

* Memory export.
* Point size.
* Possibly more that I am not aware of.

## Usage

Shaders can be directly converted to HLSL by providing the input file path, output HLSL file path, and the path to the `shader_common.h` file located in the XenosRecomp project directory:

```
XenosRecomp [input shader file path] [output HLSL file path] [header file path]
```

### Shader Cache

Alternatively, the recompiler can process an entire directory by scanning for shader binaries within the specified path. In this mode, valid shaders are converted and recompiled into a DXIL/SPIR-V cache, formatted for use with Unleashed Recompiled. This cache is then exported as a .cpp file for direct embedding into the executable:

```
XenosRecomp [input directory path] [output .cpp file path] [header file path]
```

At runtime, shaders are mapped to their recompiled versions using a 64-bit XXH3 hash lookup. This scanning method is particularly useful for games that store embedded shaders within executables or uncompressed archive formats.

SPIR-V shaders are compressed using smol-v to improve zstd compression efficiency, while DXIL shaders are compressed as-is.

## Building

The project requires CMake 3.20 and a C++ compiler with C++17 support to build. While compilers other than Clang might work, they have not been tested. Since the repository includes submodules, ensure you clone it recursively.

## Special Thanks

This recompiler would not have been possible without the [Xenia](https://github.com/xenia-project/xenia) emulator. Nearly every aspect of the development was guided by referencing Xenia's shader translator and research.

## Final Words

I hope this recompiler proves useful in some way to help with your own recompilation efforts! While the implementation isn't as generic as I hoped it would be, the optimization opportunities from game specific implementations were too significant to ignore and paid off in the end.

If you find and fix mistakes in the recompiler or successfully implement missing features in a generic way, contributions would be greatly appreciated.
