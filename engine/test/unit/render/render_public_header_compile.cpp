#include "render/render_system.h"

#include <type_traits>

static_assert(std::is_destructible_v<kpengine::render::RenderSystem>);
