#ifndef KPENGINE_EDITOR_LOADING_ICOSAHEDRON_H
#define KPENGINE_EDITOR_LOADING_ICOSAHEDRON_H

#include <array>
#include <cstdint>

namespace kpengine::editor
{
    struct LoadingIcosahedronVertex
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    struct LoadingIcosahedronFace
    {
        uint8_t a = 0;
        uint8_t b = 0;
        uint8_t c = 0;
    };

    struct LoadingIcosahedronEdge
    {
        uint8_t a = 0;
        uint8_t b = 0;
        uint8_t face_a = 0;
        uint8_t face_b = 0;
    };

    struct LoadingIcosahedronTopology
    {
        std::array<LoadingIcosahedronVertex, 12> vertices{};
        std::array<LoadingIcosahedronFace, 20> faces{};
        std::array<LoadingIcosahedronEdge, 30> edges{};
    };

    struct LoadingIcosahedronPoint
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct LoadingIcosahedronProjectedEdge
    {
        uint8_t a = 0;
        uint8_t b = 0;
        float front_facing_weight = 0.0f;
    };

    struct LoadingIcosahedronFrame
    {
        std::array<LoadingIcosahedronPoint, 12> vertices{};
        std::array<LoadingIcosahedronProjectedEdge, 30> edges{};
    };

    const LoadingIcosahedronTopology &GetLoadingIcosahedronTopology() noexcept;

    LoadingIcosahedronFrame ProjectLoadingIcosahedron(
        double elapsed_seconds, LoadingIcosahedronPoint center,
        float radius_pixels) noexcept;
}

#endif // KPENGINE_EDITOR_LOADING_ICOSAHEDRON_H
