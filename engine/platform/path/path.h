#ifndef KPENGINE_PLATHFORM_PATH_H
#define KPENGINE_PLATHFORM_PATH_H

#include <string>
#include <filesystem>

#include "platform/global.h"

namespace kpengine
{
    std::string GetTextureDirectory();

    std::string GetModelDirectory();

    std::string GetShaderDirectory();
}

#endif