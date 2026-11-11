#ifndef KPENGINE_EDITOR_SETTINGS_PROPERTY_FIELDS_VIEW_H
#define KPENGINE_EDITOR_SETTINGS_PROPERTY_FIELDS_VIEW_H

#include "editor_ui/ui_backend.h"
#include "reflection/reflection_types.h"
#include "settings/settings_types.h"

#include <optional>
#include <string_view>

namespace kpengine::editor
{
    class SettingsPropertyFieldsView final
    {
    public:
        static ui::UiEditResult Draw(ui::UiContext &context,
                                     std::string_view section_key,
                                     const settings::SettingPropertyDefinition &property,
                                     const reflection::ReflectionValue &value,
                                     bool writable = true);
        static ui::UiNodeId StableId(std::string_view section_key,
                                    std::string_view property_key) noexcept;
    };
}

#endif
