#ifndef KPENGINE_RUNTIME_RENDER_RENDER_PASS_H
#define KPENGINE_RUNTIME_RENDER_RENDER_PASS_H

#include <cstdint>
#include <string>
#include <vector>

namespace kpengine::render
{
    // Logical render resources. These names belong to the render scheduler; they
    // deliberately do not expose RHI handles or API-private resource state.
    enum class RenderPassResource : uint8_t
    {
        SceneColor,
        SceneHdr,
        GBuffer,
        DirectionalShadow,
        Count,
    };

    enum class RenderPassAccess : uint8_t
    {
        Read,
        Write,
    };

    struct RenderPassResourceUse
    {
        RenderPassResource resource = RenderPassResource::SceneColor;
        RenderPassAccess access = RenderPassAccess::Read;
    };

    struct RenderPassDeclaration
    {
        std::string name;
        std::vector<RenderPassResourceUse> resources;
        bool is_terminal = false;
    };

    // A deliberately ordered first step toward a render graph. It verifies data
    // flow now; later phases may derive ordering and lifetimes from the same
    // declarations without moving resource ownership into render.
    class RenderPassSchedule
    {
    public:
        bool AddPass(RenderPassDeclaration declaration);
        bool Validate(std::string &error) const;
        bool IsValid() const;

    private:
        std::vector<RenderPassDeclaration> passes_;
    };
}

#endif
