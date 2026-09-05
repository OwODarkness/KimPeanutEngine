# Third-Party Dependencies

This directory contains external libraries used by KimPeanutEngine. Vendored
source is kept separate from engine code and should not be modified except when
the upstream library is intentionally upgraded or patched.

## Dependency inventory

| Dependency | Version | License | Integration | CMake target | Main consumers |
|---|---:|---|---|---|---|
| Assimp | TBD | Verify upstream | Prebuilt shared library; `assimp-vc143-mt.lib` + DLL | `KP::Assimp` | Asset |
| EnTT | 3.16.0 | MIT | Header-only vendored source | `EnTT::EnTT` | Reflection |
| GLFW | 3.4 headers; binary provenance TBD | Verify upstream | Prebuilt static library | `KP::Glfw` | Window, Input, Graphics |
| glad | TBD | Verify upstream | Compiled static source | `KP::Glad` | Window, Input, Graphics, Editor |
| GoogleTest | Source tree 1.16.0 | BSD-3-Clause | Built from the upstream submodule CMake files through a parent-owned wrapper | `KP::GoogleTest` | Unit tests |
| Dear ImGui | 1.91.4 WIP | Verify upstream | Compiled static source and selected backends | `KP::ImGui` | Editor UI |
| httplib | TBD | Verify upstream | Header-only vendored source | `KP::HttpLib` | TTS |
| Lua | 5.3 binary naming; exact revision TBD | Verify upstream | Prebuilt static library | `KP::Lua` | Script |
| magic_enum | 0.9.6 | Verify upstream | Header-only vendored source | `KP::MagicEnum` | Core, Asset, Log |
| meshoptimizer | TBD | Verify upstream | Prebuilt static library | `KP::Meshoptimizer` | Not currently consumed |
| miniaudio | Header matches `D:\library\miniaudio`; Debug binary from that build | Verify upstream | Prebuilt Debug static library with matching PDB | `KP::Miniaudio` | Asset, Audio, TTS |
| nlohmann/json | 3.12.0 | MIT | Header-only vendored source | `KP::NlohmannJson` | Asset, Bootstrap, Editor, Commands |
| OpenSSL | TBD | Apache-2.0 | Vendored files; current build consumer TBD | TBD | TBD |
| sol2 | 3.5.0 | MIT | Header-only vendored source | `KP::Sol2` | Script |
| stb_image | TBD | Public domain / MIT-style dedication | Compiled static source | `KP::StbImage` | ImageIO, Window |
| Vulkan/shaderc bundle | TBD | Verify upstream components | System Vulkan target plus local shaderc import | `KP::Vulkan`, `KP::Shaderc` | Graphics, Resource, Editor |

`TBD` means the repository does not yet record enough provenance to reproduce
or audit that dependency confidently. Do not infer a version from a filename.

## Integration rules

- Prefer an upstream CMake target or a small local wrapper target.
- Keep third-party include directories private or `SYSTEM` where appropriate.
- Link dependencies directly to the engine module that consumes them.
- Do not expose third-party types through engine-facing contracts unless the
  dependency is deliberately part of that contract.
- Keep configuration-specific library and DLL paths inside imported targets.
- Preserve upstream license and notice files when vendoring source.
- Do not copy upstream tests, build directories, or repository metadata unless
  they are needed for the selected integration strategy.

## Current build notes

- The engine baseline is C++17.
- EnTT 3.16.0 is used through `EnTT::EnTT`; the Reflection module owns the
  adapter and does not expose EnTT types through its public contracts.
- Prebuilt libraries are represented by per-dependency imported targets, while
  GoogleTest is built from its upstream source submodule. Miniaudio's Debug
  library is packaged beside its matching PDB so MSVC can resolve dependency
  symbols during Debug links. Configuration-specific miniaudio Debug/Release
  variants remain future work.
- The central `third_party/CMakeLists.txt` only orchestrates dependency
  subdirectories. New dependency details belong in that dependency's wrapper.

## Upgrade checklist

When upgrading a dependency, update this file with:

1. The exact upstream version or commit.
2. The source URL and license/notice information.
3. The selected integration mode and CMake target.
4. Required static libraries, import libraries, and runtime DLLs.
5. The focused build/test evidence for every consuming module.
