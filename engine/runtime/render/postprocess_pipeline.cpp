#include "postprocess_pipeline.h"

#include "postprocess_effect.h"
namespace kpengine{
    PostProcessPipeline::PostProcessPipeline():
    effects_{},
    is_initialized_{false}
    {
    }

    void PostProcessPipeline::Initialize()
    {
        for(auto& item : effects_)
        {
            item->Initialize();
        }
        is_initialized_ = true;
    }

    void PostProcessPipeline::Execute(const PostProcessContext& context)
    {
        for(auto& item : effects_)
        {
            item->Execute(context);
        }
    }

    void PostProcessPipeline::AddEffect(std::shared_ptr<PostProcessEffect> effect)
    {
        if(!effect) return ;
        effects_.push_back(effect);
        if(is_initialized_)
        {
            effect->Initialize();
        }
    }
}