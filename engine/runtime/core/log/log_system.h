#ifndef KPENGINE_RUNTIME_LOG_SYSTEM_H
#define KPENGINE_RUNTIME_LOG_SYSTEM_H

#include <string>
#include <vector>


#include "log/logger.h"

namespace kpengine::program{
    struct LogEntry;
}

namespace kpengine {

    class LogSystem {
    public:
        // Returns a copy; the logger may mutate its storage on another thread.
        std::vector<program::LogEntry> GetLogs() const;
        std::vector<program::LogEntry> GetLogSnapshot() const;
        void Tick(float delta_time);
        
    };
}

#endif
