#ifndef KPENGINE_RUNTIME_SIGNAL_SIGNAL_MIXER_H
#define KPENGINE_RUNTIME_SIGNAL_SIGNAL_MIXER_H

#include <optional>
#include <string>

#include "signal/sample_block.h"

namespace kpengine::signal
{
    std::optional<SampleBlock> MixSignals(const SampleBlockView &first,
                                          const SampleBlockView &second,
                                          std::string &diagnostic);
}

#endif
