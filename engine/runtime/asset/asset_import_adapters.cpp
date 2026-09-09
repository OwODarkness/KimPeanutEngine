#include "asset_import_adapters.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace kpengine::asset
{
    bool RegisterModelImportProvider(ImportProviderRegistry &registry,
                                     ModelImportService &service,
                                     ModelImportSettings settings,
                                     std::string &diagnostic,
                                     ModelImportProgressCallback progress_callback,
                                     ModelImportExecutionPolicy execution)
    {
        if (settings.importer_id.empty())
        {
            settings.importer_id = "assimp";
        }
        const std::string provider_id = settings.importer_id;
        ImportProviderDescriptor descriptor{};
        descriptor.id = provider_id;
        descriptor.version = settings.importer_version;
        descriptor.kind = ImportProviderKind::Model;
        descriptor.source_suffixes = {"obj", "fbx", "gltf", "glb", "stl"};
        descriptor.callback = [&service, settings, progress_callback, execution](const ImportProviderRequest &request)
        {
            ModelImportRequest model_request{};
            model_request.asset_root = request.asset_root;
            model_request.archive_root = request.archive_root;
            model_request.source_path = request.source_path;
            model_request.settings = settings;
            model_request.progress_callback = progress_callback;
            model_request.execution = execution;
            const ModelImportResult model_result = service.Import(model_request);

            ImportProviderResult result{};
            result.status = model_result.status == ModelImportStatus::UpToDate
                                ? ImportProviderStatus::UpToDate
                                : ImportProviderStatus::Imported;
            result.product = std::make_shared<
                TypedImportProduct<ModelImportResult, ImportProviderKind::Model>>(model_result);
            return result;
        };
        return registry.Register(std::move(descriptor), diagnostic);
    }

    bool RegisterTextureImportProvider(ImportProviderRegistry &registry,
                                      TextureCookSettings settings,
                                      std::string &diagnostic)
    {
        ImportProviderDescriptor descriptor{};
        descriptor.id = "image";
        descriptor.version = 1;
        descriptor.kind = ImportProviderKind::Texture;
        descriptor.source_suffixes = {"png", "jpg", "jpeg", "tga", "bmp", "hdr"};
        descriptor.callback = [settings](const ImportProviderRequest &request)
        {
            TextureImportRequest texture_request{};
            texture_request.source_path = request.source_path.is_absolute()
                                              ? request.source_path
                                              : request.asset_root / request.source_path;
            texture_request.settings = settings;
            const ImportedTexture imported = TextureImporter{}.Import(texture_request);
            const CookedTexture cooked = TextureCooker{}.Cook(imported);
            PublishCookedTextureProduct(request.archive_root, cooked);

            ImportProviderResult result{};
            result.status = ImportProviderStatus::Cooked;
            result.product = std::make_shared<
                TypedImportProduct<CookedTexture, ImportProviderKind::Texture>>(cooked);
            return result;
        };
        return registry.Register(std::move(descriptor), diagnostic);
    }
}
