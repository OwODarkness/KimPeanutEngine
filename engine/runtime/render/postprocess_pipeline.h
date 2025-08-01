#ifndef KPENGINE_RUNTIME_POSTPROCESS_PIPELINE_H
#define KPENGINE_RUNTIME_POSTPROCESS_PIPELINE_H

#include <vector>
#include <memory>
#include "postprocess_context.h"
namespace kpengine{
    class PostProcessEffect;

    class PostProcessPipeline{
    public:
        PostProcessPipeline();
    public:
        void Initialize();
        void Execute(const PostProcessContext& context);
        void AddEffect(std::shared_ptr<PostProcessEffect> effect);
    private:
        std::vector<std::shared_ptr<PostProcessEffect>> effects_;
        bool is_initialized_{};
    };
}

#endif