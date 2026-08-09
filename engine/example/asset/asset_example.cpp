#include "asset_example.h"
#include <iostream>
#include <future>
#include <string>
#include "asset/asset_manager.h"
#include "asset/texture.h"
#include "asset/model.h"
#include "asset/mesh.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "config/path.h"

namespace kpengine::example{

    void TextureLoadSync()
    {
        using namespace asset;
        auto &asset_manager = AssetManager::GetInstance();

        std::string path = GetTextureDirectory() + "default.png";

        AssetID id = asset_manager.LoadSync(path);

        auto texture = asset_manager.GetResource<TextureResource>(id);
        if (!texture)
        {
            std::cout << "[TextureLoadAsync] loaded asset holds no texture resource" << std::endl;
            return;
        }

                const TextureData &data = *texture->resource;
        std::cout << "[TextureLoadAsync] loaded " << data.width << "x" << data.height
                  << ", " << data.pixels.size() << " bytes" << std::endl;
    }

    void TextureLoadAsync()
    {
        using namespace asset;
        AssetManager &asset_manager = AssetManager::GetInstance();

        std::string path = GetTextureDirectory() + "default.png";

        // Kick off the load on a worker thread (same pipeline as LoadSync).
        std::future<AssetID> future = asset_manager.LoadAsync(path);

        // ... do other work here while the texture loads in the background ...

        // Wait for the result. Call get()/wait() before the future dies —
        // destroying a pending future blocks until the load finishes anyway.
        AssetID id = future.get();
        if (!id.IsValid())
        {
            std::cout << "[TextureLoadAsync] failed to load " << path << std::endl;
            return;
        }

        auto texture = asset_manager.GetResource<TextureResource>(id);
        if (!texture)
        {
            std::cout << "[TextureLoadAsync] loaded asset holds no texture resource" << std::endl;
            return;
        }

        const TextureData &data = *texture->resource;
        std::cout << "[TextureLoadAsync] loaded " << data.width << "x" << data.height
                  << ", " << data.pixels.size() << " bytes" << std::endl;
    }

    void ModelLoadSync()
    {
        using namespace asset;
        AssetManager &asset_manager = AssetManager::GetInstance();

        std::string path = GetModelDirectory() + "sphere/sphere.obj";

        AssetID id = asset_manager.LoadSync(path);
        if (!id.IsValid())
        {
            std::cout << "[ModelLoadSync] failed to load " << path << std::endl;
            return;
        }

        auto model = asset_manager.GetResource<ModelResource>(id);
        if (!model)
        {
            std::cout << "[ModelLoadSync] loaded asset holds no model resource" << std::endl;
            return;
        }

        auto mesh = model->GetMesh();
        if (!mesh)
        {
            std::cout << "[ModelLoadSync] model has no mesh data" << std::endl;
            return;
        }

        std::cout << "[ModelLoadSync] loaded " << mesh->vertex_count << " vertices, "
                  << mesh->face_count << " faces" << std::endl;
    }

    void ModelLoadAsync()
    {
        using namespace asset;
        AssetManager &asset_manager = AssetManager::GetInstance();

        std::string path = GetModelDirectory() + "sphere/sphere.obj";

        // Kick off the load on a worker thread (same pipeline as LoadSync).
        std::future<AssetID> future = asset_manager.LoadAsync(path);

        // ... do other work here while the model loads in the background ...

        // Wait for the result. Call get()/wait() before the future dies —
        // destroying a pending future blocks until the load finishes anyway.
        AssetID id = future.get();
        if (!id.IsValid())
        {
            std::cout << "[ModelLoadAsync] failed to load " << path << std::endl;
            return;
        }

        auto model = asset_manager.GetResource<ModelResource>(id);
        if (!model)
        {
            std::cout << "[ModelLoadAsync] loaded asset holds no model resource" << std::endl;
            return;
        }

        auto mesh = model->GetMesh();
        if (!mesh)
        {
            std::cout << "[ModelLoadAsync] model has no mesh data" << std::endl;
            return;
        }

        std::cout << "[ModelLoadAsync] loaded " << mesh->vertex_count << " vertices, "
                  << mesh->face_count << " faces" << std::endl;
    }

    void ShaderProgramLoad()
    {
        using namespace asset;
        AssetManager &asset_manager = AssetManager::GetInstance();

        std::string path = GetShaderDirectory() + "simple_triangle.shader";

        // Loading a .shader file only parses the program meta — which stages it
        // has, where the sources live, entry points, defines. No compilation
        // happens here; that is a separate step driven by the target graphics API.
        AssetID id = asset_manager.LoadSync(path);
        if (!id.IsValid())
        {
            std::cout << "[ShaderProgramLoad] failed to load " << path << std::endl;
            return;
        }

        auto program = asset_manager.GetResource<ShaderProgramResource>(id);
        if (!program)
        {
            std::cout << "[ShaderProgramLoad] loaded asset holds no shader program resource" << std::endl;
            return;
        }

        // Each stage was registered as its own KPAT_Shader asset. Query them by
        // (stage, source format); the ShaderResources are Uncompiled — meta only.
        const ShaderStage stages[] = {
            ShaderStage::SHADER_STAGE_VERTEX,
            ShaderStage::SHADER_STAGE_FRAGMENT,
        };
        for (ShaderStage stage : stages)
        {
            auto shader = program->GetShader(stage, ShaderFormat::SHADER_FORMAT_GLSL);
            if (!shader)
            {
                continue;
            }
            std::cout << "[ShaderProgramLoad] source: " << shader->desc.file
                      << ", entry: " << shader->desc.entry
                      << ", defines: " << shader->desc.defines.size()
                      << ", status: uncompiled (meta only)" << std::endl;
        }
    }
}