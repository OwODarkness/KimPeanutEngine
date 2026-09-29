#ifndef KPENGINE_TERRAIN_EDITOR_HEIGHTMAP_DEBUG_VIEW_H
#define KPENGINE_TERRAIN_EDITOR_HEIGHTMAP_DEBUG_VIEW_H

namespace kpengine::terrain
{
    class ScalarField2D;

    class HeightmapDebugView final
    {
    public:
        void RenderContent(const ScalarField2D *heightfield) const;
    };
}

#endif
