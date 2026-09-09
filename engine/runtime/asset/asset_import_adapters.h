#ifndef KPENGINE_RUNTIME_ASSET_ASSET_IMPORT_ADAPTERS_H
#define KPENGINE_RUNTIME_ASSET_ASSET_IMPORT_ADAPTERS_H

#include <string>

#include "asset_import_registry.h"
#include "model_import_service.h"
#include "texture_importer.h"

namespace kpengine::asset
{
    // These adapters bind the existing typed offline services to the generic
    // provider contract. The service remains the owner of its typed request,
    // result, publication, and archive transaction.
    bool RegisterModelImportProvider(ImportProviderRegistry &registry,
                                     ModelImportService &service,
                                     ModelImportSettings settings,
                                     std::string &diagnostic,
                                     ModelImportProgressCallback progress_callback = {},
                                     ModelImportExecutionPolicy execution = {});

    bool RegisterTextureImportProvider(ImportProviderRegistry &registry,
                                      TextureCookSettings settings,
                                      std::string &diagnostic);
}

#endif
