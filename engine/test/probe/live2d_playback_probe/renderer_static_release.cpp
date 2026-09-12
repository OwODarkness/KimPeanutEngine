// Probe-local copy of the module's empty StaticRelease hook. The probe does no
// rendering, but CubismFramework::Dispose() calls into it, and the probe
// deliberately does not link any engine or backend target.

#include "Rendering/CubismRenderer.hpp"

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

void CubismRenderer::StaticRelease()
{
}

}}}}
