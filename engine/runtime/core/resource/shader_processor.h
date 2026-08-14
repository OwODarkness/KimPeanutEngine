#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_PROCESSOR_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_PROCESSOR_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>
#include "asset/shader.h"
#include "base/type.h"
#include "shader_operation.h"

namespace kpengine::resource{
    class ShaderCompiler;
    class ShaderCache;

    // Fire-and-forget progress reporter, passed to Process. nullptr (or an
    // empty std::function) means no reporting — the pipeline runs identically.
    // Fired once per phase boundary: (phase, done, total, current shader). The
    // observer only watches; it never controls the pipeline.
    using ShaderProcessObserver = std::function<void(
        ShaderProcessPhase phase,
        int done, int total,
        const asset::ShaderResource* shader)>;

    class ShaderProcessor{
    public:
        ShaderProcessor();
        ~ShaderProcessor();
        void Initialize(GraphicsAPIType api_type);
        void Process(ShaderCache* cache,
                     const std::vector<std::shared_ptr<asset::ShaderResource>>& assets,
                     ShaderProcessObserver observer = nullptr);
        // Distinct shader references (content hash — the ShaderCache key) that
        // finished processing this run. Reference-based: a stage shared across
        // programs counts once instead of once per program.
        std::size_t GetProcessedShaderCount() const { return processed_hashes_.size(); }
    private:
        std::vector<std::unique_ptr<ShaderOperation>> operations_;
        GraphicsAPIType api_;
        // True when this API's artifact is the final GLSL source rather than
        // binary byte code (OpenGL). Selects the operation built at Initialize,
        // skips the content-addressed cache (source is cheap to produce), and
        // tells the write-back which ShaderData field to fill.
        bool keep_source_ = false;
        std::unordered_set<uint64_t> processed_hashes_;
    };
}

#endif
