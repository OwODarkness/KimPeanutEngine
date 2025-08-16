#ifndef KPENGINE_RUNTIME_LOG_SYSTEM_H
#define KPENGINE_RUNTIME_LOG_SYSTEM_H

#include <string>
#include <vector>


#include "runtime/core/log/log_entry.h"

namespace kpengine::program{
    struct LogEntry;
}

namespace kpengine {

    class LogSystem {
    public:
        const std::vector<program::LogEntry>& GetLogs() const;
        void Tick(float delta_time);
        
    };
}

#endif