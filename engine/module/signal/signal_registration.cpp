#include <memory>

#include "runtime/host/application_host.h"
#include "tools/signal_lab_host.h"

namespace kpengine::signal
{
    namespace
    {
        const runtime::ApplicationHostRegistration registration{
            {"signal-lab",
             [](runtime::Engine &) { return std::make_unique<SignalLabHost>(); },
             runtime::ApplicationHostLayout::SharedWindowPresentation,
             false}};
    }
}
