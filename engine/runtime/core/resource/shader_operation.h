#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_OPERATION_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_OPERATION_H

#include <cstdint>
#include <string>
#include <vector>
#include "base/type.h"
#include "base/graphics_type.h"

namespace kpengine::resource{

// One stage of the shader processing pipeline. The processor drives a pipeline
// of ShaderOperation stages in order; each stage reports its phase and name
// through the observer so callers (warmup loading screen, editor) can show
// progress.
enum class ShaderProcessPhase
{
    Preprocess,
    Compile,
    Reflect,
    Save
};

// The data that flows between pipeline stages. Each stage reads what it needs
// and writes what it produces; a later stage consumes an earlier stage's output
// (e.g. compile consumes preprocess's source, reflect consumes compile's bytes).
// Deliberately free of asset types — the processor copies identity out of the
// ShaderResource it already holds, so the operation interface stays decoupled
// from the asset graph.
struct ShaderProcessContext
{
    std::string file_name;              // where the source came from (errors/logs)
    std::string source;                 // preprocess output / compile input
    std::vector<std::string> defines;   // preprocessor macros
    ShaderStage stage = ShaderStage::SHADER_STAGE_UNKNOW;
    ShaderFormat format = ShaderFormat::Unknown;
    std::vector<uint8_t> byte_code;     // compile output
};

// Base interface for a shader sub-operation. Compile is the only stage
// implemented today (ShaderCompiler / SPIRVCompiler); preprocess, reflect, and
// save are the planned stages. The processor is the orchestrator: it owns the
// pipeline and drives each stage in order, reporting progress through the
// observer.
class ShaderOperation
{
public:
    virtual ~ShaderOperation() = default;
    virtual void Initialize(GraphicsAPIType api_type) = 0;
    virtual ShaderProcessPhase GetPhase() const = 0;
    virtual const char* GetName() const = 0;    // for logs/UI, e.g. "compile"
    // Run the stage. Returns true on success, false on failure (e.g. a compile
    // error); the caller records the failure and stops the pipeline.
    virtual bool Run(ShaderProcessContext& context) = 0;
};

}

#endif
