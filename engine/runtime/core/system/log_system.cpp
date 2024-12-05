#include "log_system.h"

#include <iostream>
namespace kpengine{
    void LogSystem::AddLog(std::string msg)
    {
        logs.push_back(std::move(msg));
        //std::cout << logs.at(logs.size()-1) << std::endl;
    }
}