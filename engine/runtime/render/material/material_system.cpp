#include "render/material/material_system.h"

#include <chrono>
#include <unordered_set>

namespace kpengine::render
{
    void MaterialSystem::SetResourceResolver(IMaterialResourceResolver *resolver)
    {
        resource_resolver_ = resolver;
        MarkChanged();
        RefreshResources();
    }

    void MaterialSystem::RefreshResources(bool force_ready)
    {
        bool changed = false;
        for (auto &[id, record] : templates_)
        {
            (void)id;
            if (record.resolution.state == MaterialResourceState::Pending)
            {
                const MaterialResolution before = record.resolution;
                ResolveTemplate(record.handle, record);
                changed = changed || before.state != record.resolution.state ||
                          before.diagnostic != record.resolution.diagnostic;
            }
        }
        for (auto &[id, record] : instances_)
        {
            (void)id;
            const auto template_it = templates_.find(template_handles_.Get(record.template_handle));
            if (template_it != templates_.end() &&
                (record.resolution.state == MaterialResourceState::Pending ||
                 (force_ready && record.resolution.state == MaterialResourceState::Ready)))
            {
                const MaterialResolution before = record.resolution;
                ResolveInstance(record.handle, template_it->second, record);
                changed = changed || force_ready ||
                          before.state != record.resolution.state ||
                          before.diagnostic != record.resolution.diagnostic;
            }
        }
        if (changed)
        {
            MarkChanged();
        }
    }
    MaterialTemplateHandle MaterialSystem::CreateTemplate(const MaterialTemplateDesc &desc)
    {
        if (!IsTemplateDescValid(desc))
        {
            return {};
        }

        std::unordered_map<std::string, MaterialParameterID> parameter_ids;
        for (uint32_t index = 0; index < desc.parameters.size(); ++index)
        {
            parameter_ids.emplace(desc.parameters[index].name, MaterialParameterID{index});
        }

        const MaterialTemplateHandle handle = template_handles_.Create();
        auto [template_it, inserted] = templates_.emplace(
            handle.id, MaterialTemplateRecord{handle, desc, std::move(parameter_ids), {}, 0});
        (void)inserted;
        ResolveTemplate(handle, template_it->second);
        MarkChanged();
        return handle;
    }

    bool MaterialSystem::DestroyTemplate(MaterialTemplateHandle handle)
    {
        const auto template_it = templates_.find(template_handles_.Get(handle));
        if (template_it == templates_.end() || template_it->second.instance_count != 0)
        {
            return false;
        }

        if (resource_resolver_)
        {
            resource_resolver_->ReleaseTemplate(handle);
        }
        templates_.erase(template_it);
        const bool destroyed = template_handles_.Destroy(handle);
        if (destroyed)
        {
            MarkChanged();
        }
        return destroyed;
    }

    const MaterialTemplateDesc *MaterialSystem::FindTemplate(MaterialTemplateHandle handle) const
    {
        const auto template_it = templates_.find(template_handles_.Get(handle));
        return template_it != templates_.end() ? &template_it->second.desc : nullptr;
    }

    bool MaterialSystem::IsTemplateValid(MaterialTemplateHandle handle) const
    {
        return FindTemplate(handle) != nullptr;
    }

    MaterialResolution MaterialSystem::GetTemplateResolution(MaterialTemplateHandle handle) const
    {
        const auto template_it = templates_.find(template_handles_.Get(handle));
        return template_it != templates_.end() ? template_it->second.resolution : MaterialResolution{};
    }

    MaterialParameterID MaterialSystem::FindParameterID(MaterialTemplateHandle template_handle,
                                                        std::string_view name) const
    {
        const auto template_it = templates_.find(template_handles_.Get(template_handle));
        if (template_it == templates_.end())
        {
            return {};
        }

        const auto parameter_it = template_it->second.parameter_ids.find(std::string{name});
        return parameter_it != template_it->second.parameter_ids.end() ? parameter_it->second
                                                                         : MaterialParameterID{};
    }

    MaterialInstanceHandle MaterialSystem::CreateInstance(const MaterialInstanceDesc &desc)
    {
        const auto template_it = templates_.find(template_handles_.Get(desc.template_handle));
        if (template_it == templates_.end() || !AreOverridesValid(template_it->second, desc.overrides))
        {
            return {};
        }

        std::unordered_map<uint32_t, MaterialParameterValue> overrides;
        for (const MaterialParameterOverride &override : desc.overrides)
        {
            overrides.emplace(override.parameter_id.value, override.value);
        }

        const MaterialInstanceHandle handle = instance_handles_.Create();
        auto [instance_it, inserted] = instances_.emplace(
            handle.id, MaterialInstanceRecord{handle, desc.template_handle, std::move(overrides), {}, 1});
        (void)inserted;
        ++template_it->second.instance_count;
        ResolveInstance(handle, template_it->second, instance_it->second);
        MarkChanged();
        return handle;
    }

    bool MaterialSystem::UpdateInstance(
        MaterialInstanceHandle handle, const std::vector<MaterialParameterOverride> &overrides)
    {
        const auto instance_it = instances_.find(instance_handles_.Get(handle));
        if (instance_it == instances_.end())
        {
            return false;
        }

        const auto template_it = templates_.find(template_handles_.Get(instance_it->second.template_handle));
        if (template_it == templates_.end() || !AreOverridesValid(template_it->second, overrides))
        {
            return false;
        }

        std::unordered_map<uint32_t, MaterialParameterValue> updated_overrides;
        for (const MaterialParameterOverride &override : overrides)
        {
            updated_overrides.emplace(override.parameter_id.value, override.value);
        }
        instance_it->second.overrides = std::move(updated_overrides);
        instance_it->second.resolution = {};
        ++instance_it->second.revision;
        ResolveInstance(handle, template_it->second, instance_it->second);
        MarkChanged();
        return true;
    }

    bool MaterialSystem::DestroyInstance(MaterialInstanceHandle handle)
    {
        const auto instance_it = instances_.find(instance_handles_.Get(handle));
        if (instance_it == instances_.end())
        {
            return false;
        }

        const auto template_it = templates_.find(template_handles_.Get(instance_it->second.template_handle));
        if (template_it == templates_.end() || template_it->second.instance_count == 0)
        {
            return false;
        }

        if (resource_resolver_)
        {
            resource_resolver_->ReleaseInstance(handle);
        }
        --template_it->second.instance_count;
        instances_.erase(instance_it);
        const bool destroyed = instance_handles_.Destroy(handle);
        if (destroyed)
        {
            MarkChanged();
        }
        return destroyed;
    }

    MaterialTemplateHandle MaterialSystem::GetInstanceTemplate(MaterialInstanceHandle handle) const
    {
        const auto instance_it = instances_.find(instance_handles_.Get(handle));
        return instance_it != instances_.end() ? instance_it->second.template_handle
                                               : MaterialTemplateHandle{};
    }

    uint64_t MaterialSystem::GetInstanceRevision(MaterialInstanceHandle handle) const
    {
        const auto instance_it = instances_.find(instance_handles_.Get(handle));
        return instance_it != instances_.end() ? instance_it->second.revision : 0;
    }

    void MaterialSystem::MarkChanged() noexcept
    {
        if (++revision_ == 0)
        {
            ++revision_;
        }
    }

    const MaterialParameterValue *MaterialSystem::GetParameterValue(
        MaterialInstanceHandle instance_handle, MaterialParameterID parameter_id) const
    {
        const auto instance_it = instances_.find(instance_handles_.Get(instance_handle));
        if (instance_it == instances_.end())
        {
            return nullptr;
        }

        const auto template_it = templates_.find(template_handles_.Get(instance_it->second.template_handle));
        if (template_it == templates_.end() || !parameter_id.IsValid() ||
            parameter_id.value >= template_it->second.desc.parameters.size())
        {
            return nullptr;
        }

        const auto override_it = instance_it->second.overrides.find(parameter_id.value);
        return override_it != instance_it->second.overrides.end()
                   ? &override_it->second
                   : &template_it->second.desc.parameters[parameter_id.value].default_value;
    }

    bool MaterialSystem::IsInstanceValid(MaterialInstanceHandle handle) const
    {
        return instances_.find(instance_handles_.Get(handle)) != instances_.end();
    }

    MaterialResolution MaterialSystem::GetInstanceResolution(MaterialInstanceHandle handle) const
    {
        const auto started = std::chrono::steady_clock::now();
        const auto instance_it = instances_.find(instance_handles_.Get(handle));
        const MaterialResolution result =
            instance_it != instances_.end() ? instance_it->second.resolution : MaterialResolution{};
        ++profile_counters_.resolution_calls;
        profile_counters_.resolution_cpu_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        return result;
    }

    std::optional<MaterialDrawClass> MaterialSystem::GetDrawClass(MaterialInstanceHandle handle) const
    {
        const auto started = std::chrono::steady_clock::now();
        const auto instance_it = instances_.find(instance_handles_.Get(handle));
        if (instance_it == instances_.end())
        {
            ++profile_counters_.resolution_calls;
            profile_counters_.resolution_cpu_ms +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started)
                    .count();
            return std::nullopt;
        }
        const auto template_it = templates_.find(template_handles_.Get(instance_it->second.template_handle));
        if (template_it == templates_.end())
        {
            ++profile_counters_.resolution_calls;
            profile_counters_.resolution_cpu_ms +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started)
                    .count();
            return std::nullopt;
        }
        const std::optional<MaterialDrawClass> result =
            template_it->second.desc.pipeline_state.blend_mode == MaterialBlendMode::Opaque
                ? MaterialDrawClass::Opaque
                : MaterialDrawClass::AlphaBlend;
        ++profile_counters_.resolution_calls;
        profile_counters_.resolution_cpu_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        return result;
    }

    bool MaterialSystem::IsTemplateDescValid(const MaterialTemplateDesc &desc) const
    {
        if (!desc.shader_program.IsValid() ||
            desc.shader_program.type != asset::AssetType::KPAT_ShaderProgram ||
            desc.domain != MaterialDomain::Surface || desc.compatible_passes.empty())
        {
            return false;
        }

        std::unordered_set<std::string> parameter_names;
        for (const MaterialParameterDesc &parameter : desc.parameters)
        {
            if (parameter.name.empty() || !parameter_names.insert(parameter.name).second)
            {
                return false;
            }
            if (!IsParameterValueValid(parameter.default_value))
            {
                return false;
            }
        }
        return true;
    }

    bool MaterialSystem::IsParameterValueValid(const MaterialParameterValue &value) const
    {
        const auto *texture_value = std::get_if<MaterialTextureSamplerValue>(&value);
        return texture_value == nullptr ||
               (texture_value->texture_asset.IsValid() &&
                texture_value->texture_asset.type == asset::AssetType::KPAT_Texture);
    }

    bool MaterialSystem::AreOverridesValid(
        const MaterialTemplateRecord &template_record,
        const std::vector<MaterialParameterOverride> &overrides) const
    {
        std::unordered_set<uint32_t> overridden_parameter_ids;
        for (const MaterialParameterOverride &override : overrides)
        {
            if (!override.parameter_id.IsValid() ||
                override.parameter_id.value >= template_record.desc.parameters.size() ||
                !overridden_parameter_ids.insert(override.parameter_id.value).second ||
                override.value.index() !=
                    template_record.desc.parameters[override.parameter_id.value].default_value.index() ||
                !IsParameterValueValid(override.value))
            {
                return false;
            }
        }
        return true;
    }

    std::vector<MaterialParameterValue> MaterialSystem::GetEffectiveValues(
        const MaterialTemplateRecord &template_record,
        const MaterialInstanceRecord &instance_record) const
    {
        std::vector<MaterialParameterValue> values;
        values.reserve(template_record.desc.parameters.size());
        for (uint32_t index = 0; index < template_record.desc.parameters.size(); ++index)
        {
            const auto override_it = instance_record.overrides.find(index);
            values.push_back(override_it != instance_record.overrides.end()
                                 ? override_it->second
                                 : template_record.desc.parameters[index].default_value);
        }
        return values;
    }

    void MaterialSystem::ResolveTemplate(MaterialTemplateHandle handle, MaterialTemplateRecord &record)
    {
        record.resolution = resource_resolver_ ? resource_resolver_->ResolveTemplate(handle, record.desc)
                                                : MaterialResolution{};
    }

    void MaterialSystem::ResolveInstance(MaterialInstanceHandle handle,
                                         const MaterialTemplateRecord &template_record,
                                         MaterialInstanceRecord &record)
    {
        if (template_record.resolution.state != MaterialResourceState::Ready)
        {
            record.resolution = template_record.resolution;
            return;
        }
        record.resolution = resource_resolver_
                                ? resource_resolver_->ResolveInstance(
                                      handle,
                                      template_record.desc, GetEffectiveValues(template_record, record))
                                : MaterialResolution{};
    }
}
