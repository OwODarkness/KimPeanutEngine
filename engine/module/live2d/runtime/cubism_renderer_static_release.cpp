// The Framework's CubismFramework::Dispose() calls StaticRelease() even when
// no official backend renderer is compiled. Live2D production rendering will
// use KimPeanutEngine's RHI, so this intentionally empty hook replaces the
// backend-owned implementation without pulling OpenGL or Vulkan into the
// module.

#include "Rendering/CubismRenderer.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

void CubismRenderer::StaticRelease()
{
}

}}}}
