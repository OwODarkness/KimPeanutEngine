#ifndef KPENGINE_RUNTIME_POSTPROCESS_EFFECT_H
#define KPENGINE_RUNTIME_POSTPROCESS_EFFECT_H

#include "postprocess_context.h"
namespace kpengine
{
    class PostProcessEffect
    {
    public:
        virtual void Execute(const PostProcessContext &context) = 0;
        virtual void Initialize() = 0;
        virtual ~PostProcessEffect() = default;
    };
}

#endif