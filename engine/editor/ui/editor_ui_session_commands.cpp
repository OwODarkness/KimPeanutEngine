#include "editor/ui/editor_ui_session_commands.h"

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <nlohmann/json.hpp>

namespace kpengine::editor
{
    namespace
    {
        using Json = nlohmann::json;

        const char *UiRoleName(const ui::UiRole role)
        {
            using ui::UiRole;
            switch (role)
            {
            case UiRole::Generic: return "generic"; case UiRole::Button: return "button";
            case UiRole::CheckBox: return "checkbox"; case UiRole::Selectable: return "selectable";
            case UiRole::TextInput: return "text_input"; case UiRole::NumericInput: return "numeric_input";
            case UiRole::Slider: return "slider"; case UiRole::Combo: return "combobox";
            case UiRole::Window: return "window"; case UiRole::Group: return "group";
            case UiRole::Image: return "image"; case UiRole::Canvas: return "canvas";
            case UiRole::Session: return "session"; case UiRole::Panel: return "panel";
            case UiRole::TabBar: return "tab_list"; case UiRole::Tab: return "tab";
            case UiRole::Table: return "table"; case UiRole::Row: return "row";
            case UiRole::Heading: return "heading"; case UiRole::Text: return "text";
            case UiRole::VirtualList: return "virtual_list";
            case UiRole::Splitter: return "splitter"; case UiRole::Chart: return "chart";
            }
            return "generic";
        }

        const char *UiActionName(const ui::UiActionKind action)
        {
            using ui::UiActionKind;
            switch (action)
            {
            case UiActionKind::Activate: return "activate"; case UiActionKind::Toggle: return "toggle";
            case UiActionKind::Select: return "select"; case UiActionKind::Edit: return "edit";
            case UiActionKind::Commit: return "commit"; case UiActionKind::Cancel: return "cancel";
            case UiActionKind::Close: return "close";
            }
            return "unknown";
        }

        const char *UiGeometryName(const ui::UiGeometryProvenance provenance)
        {
            using ui::UiGeometryProvenance;
            switch (provenance)
            {
            case UiGeometryProvenance::Unavailable: return "unavailable";
            case UiGeometryProvenance::TestFixture: return "test_fixture";
            case UiGeometryProvenance::ActualBackend: return "actual_backend";
            case UiGeometryProvenance::DerivedFromChildren: return "derived_from_children";
            }
            return "unavailable";
        }

        Json UiRectJson(const ui::UiRect &rect)
        {
            return Json::array({rect.min.x, rect.min.y, rect.max.x - rect.min.x,
                                rect.max.y - rect.min.y});
        }

        Json UiValueJson(const ui::UiValue &value)
        {
            return std::visit([](const auto &item) -> Json
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, bool>) return item;
                else if constexpr (std::is_same_v<T, std::int64_t> ||
                                   std::is_same_v<T, std::uint64_t>)
                    return std::to_string(item);
                else if constexpr (std::is_same_v<T, double>)
                    return std::isfinite(item) ? Json(item) : Json(nullptr);
                else return item;
            }, value);
        }

        Json UiColorJson(const ui::UiColor &color)
        {
            return Json::array({color.r, color.g, color.b, color.a});
        }

        Json UiThemeJson(const ui::UiTheme &theme)
        {
            return {{"font_role", theme.font == ui::UiFontRole::Code ? "code" : "body"},
                {"colors", {{"surface", UiColorJson(theme.surface)}, {"panel", UiColorJson(theme.panel)},
                    {"raised", UiColorJson(theme.raised)}, {"border", UiColorJson(theme.border)},
                    {"text", UiColorJson(theme.text)}, {"muted", UiColorJson(theme.muted)},
                    {"accent", UiColorJson(theme.accent)}, {"accent_active", UiColorJson(theme.accent_active)},
                    {"selected", UiColorJson(theme.selected)}, {"selected_hovered", UiColorJson(theme.selected_hovered)},
                    {"error", UiColorJson(theme.error)}, {"success", UiColorJson(theme.success)}}},
                {"metrics", {{"window_padding", {theme.window_padding_x, theme.window_padding_y}},
                    {"frame_padding", {theme.frame_padding_x, theme.frame_padding_y}},
                    {"item_spacing", {theme.item_spacing_x, theme.item_spacing_y}},
                    {"inner_spacing", {theme.inner_spacing_x, theme.inner_spacing_y}},
                    {"window_rounding", theme.window_rounding}, {"frame_rounding", theme.frame_rounding},
                    {"window_border_size", theme.window_border_size},
                    {"frame_border_size", theme.frame_border_size}}}};
        }

        Json UiSnapshotJson(const ui::UiSnapshotCapture &capture, const bool include_geometry,
                            const bool include_styles)
        {
            Json elements = Json::array();
            std::size_t unmeasured = 0;
            for (const ui::UiNodeObservation &node : capture.nodes)
            {
                Json item{{"element_id", std::to_string(node.id)},
                    {"parent_id", node.parent == 0 ? Json(nullptr) : Json(std::to_string(node.parent))},
                    {"role", UiRoleName(node.role)},
                    {"title", node.label.empty() ? node.description : node.label},
                    {"state", {{"enabled", node.enabled}}}, {"actions", Json::array()}};
                if (!node.semantic_key.empty()) item["semantic_id"] = node.semantic_key;
                if (!node.description.empty() && node.description != node.label)
                    item["description"] = node.description;
                if (!node.disabled_reason.empty()) item["disabled_reason"] = node.disabled_reason;
                if (node.value) item["value"] = UiValueJson(*node.value);
                if (node.selected) item["state"]["selected"] = *node.selected;
                if (node.active) item["state"]["active"] = *node.active;
                if (node.focused) item["state"]["focused"] = *node.focused;
                if (node.hovered) item["state"]["hovered"] = *node.hovered;
                if (node.visible) item["state"]["visible"] = *node.visible;
                if (node.expanded) item["state"]["expanded"] = *node.expanded;
                if (!node.unit.empty()) item["unit"] = node.unit;
                if (!node.domain_summary.empty()) item["summary"] = node.domain_summary;
                if (node.minimum || node.maximum || node.step)
                {
                    Json range = Json::object();
                    if (node.minimum) range["min"] = UiValueJson(*node.minimum);
                    if (node.maximum) range["max"] = UiValueJson(*node.maximum);
                    if (node.step) range["step"] = UiValueJson(*node.step);
                    item["range"] = std::move(range);
                }
                for (const ui::UiActionKind action : node.actions)
                    item["actions"].push_back(UiActionName(action));
                if (node.last_action) item["last_action"] = UiActionName(*node.last_action);
                if (!node.choices.empty())
                {
                    item["choices"] = Json::array();
                    for (const ui::UiChoice &choice : node.choices)
                        item["choices"].push_back({{"key", choice.key}, {"label", choice.label},
                                                   {"value", UiValueJson(choice.value)}});
                }
                if (!node.table_columns.empty()) item["columns"] = node.table_columns;
                if (node.table_column) item["column_index"] = *node.table_column;
                if (node.chart)
                {
                    const auto axis_json = [](const ui::UiChartAxisObservation &axis)
                    {
                        return Json{{"label", axis.label}, {"unit", axis.unit},
                            {"min", axis.minimum}, {"max", axis.maximum},
                            {"scale", axis.scale == ui::UiPlotScale::Log10 ? "log10" : "linear"}};
                    };
                    Json chart{{"x", axis_json(node.chart->x)}, {"y", axis_json(node.chart->y)},
                        {"empty_label", node.chart->empty_label}, {"truncated", node.chart->truncated},
                        {"series", Json::array()}};
                    for (const auto &series : node.chart->series)
                    {
                        Json points = Json::array();
                        for (const ui::UiPlotPoint &point : series.points)
                            points.push_back({point.x, point.y});
                        chart["series"].push_back({{"label", series.label},
                            {"kind", series.kind == ui::UiPlotSeriesKind::Bars ? "bars" :
                                series.kind == ui::UiPlotSeriesKind::Points ? "points" : "line"},
                            {"emission_strength", series.emission_strength},
                            {"point_count", series.point_count}, {"points", std::move(points)}});
                    }
                    item["chart"] = std::move(chart);
                }
                if (include_geometry)
                {
                    Json geometry{{"bounds", node.geometry == ui::UiGeometryProvenance::Unavailable
                            ? Json(nullptr) : UiRectJson(node.bounds)},
                        {"provenance", UiGeometryName(node.geometry)}};
                    if (node.clipped_bounds) geometry["clipped_bounds"] = UiRectJson(*node.clipped_bounds);
                    if (node.clipped) geometry["clipped"] = *node.clipped;
                    item["geometry"] = std::move(geometry);
                    if (node.geometry == ui::UiGeometryProvenance::Unavailable) ++unmeasured;
                }
                if (include_styles && !node.style_id.empty()) item["style_id"] = node.style_id;
                if (node.role == ui::UiRole::VirtualList)
                    item["virtualization"] = {{"total", node.virtual_list_total},
                        {"materialized_range", {node.virtual_list_first, node.virtual_list_last}}};
                elements.push_back(std::move(item));
            }

            Json styles = Json::array();
            if (include_styles)
                for (const ui::UiThemeObservation &style : capture.styles)
                    styles.push_back({{"id", style.id}, {"declared", UiThemeJson(style.declared)}});
            Json workspaces = Json::array();
            for (const ui::UiWorkspaceSnapshot &workspace : capture.workspaces)
            {
                Json regions = Json::array();
                for (const ui::UiWorkspaceRegionSnapshot &region : workspace.regions)
                {
                    Json tabs = Json::array();
                    for (const std::string &tab : region.tabs) tabs.push_back(tab);
                    regions.push_back({{"id", region.id}, {"title", region.label},
                        {"bounds", region.has_bounds ? UiRectJson(region.bounds) : Json(nullptr)},
                        {"tabs", std::move(tabs)}, {"active_tab", region.active_tab
                            ? Json(*region.active_tab) : Json(nullptr)}});
                }
                Json splits = Json::array();
                for (const ui::UiWorkspaceSplitSnapshot &split : workspace.splits)
                    splits.push_back({{"id", split.id},
                        {"axis", split.axis == ui::UiWorkspaceAxis::Horizontal ? "x" : "y"},
                        {"children", split.children},
                        {"bounds", split.has_bounds ? UiRectJson(split.bounds) : Json(nullptr)},
                        {"requested_ratio", split.requested_ratio},
                        {"resolved_ratio", split.resolved_ratio}});
                Json panels = Json::array();
                for (const ui::UiWorkspacePanelSnapshot &panel : workspace.panels)
                {
                    Json item{{"id", panel.id}, {"title", panel.title},
                        {"open", panel.open}, {"active", panel.active},
                        {"bounds", panel.has_bounds ? UiRectJson(panel.bounds) : Json(nullptr)}};
                    item["region"] = panel.region ? Json(*panel.region) : Json(nullptr);
                    panels.push_back(std::move(item));
                }
                workspaces.push_back({{"id", workspace.id}, {"root", workspace.root},
                    {"regions", std::move(regions)}, {"splits", std::move(splits)},
                    {"panels", std::move(panels)}});
            }
            Json result{{"schema_version", 1},
                {"snapshot_id", std::to_string(capture.session) + "/" + std::to_string(capture.frame)},
                {"session_id", std::to_string(capture.session)}, {"frame", std::to_string(capture.frame)},
                {"host", capture.host}, {"mode", capture.mode},
                {"view", include_styles ? "visual" : include_geometry ? "spatial" : "semantic"},
                {"coordinate_space", { {"bounds", "main_viewport_logical"}, {"unit", "logical_pixel"},
                    {"origin", {capture.viewport.logical_origin.x, capture.viewport.logical_origin.y}},
                    {"extent", {capture.viewport.logical_extent.x, capture.viewport.logical_extent.y}},
                    {"framebuffer_scale", {capture.viewport.framebuffer_scale.x, capture.viewport.framebuffer_scale.y}}}},
                {"elements", std::move(elements)}, {"workspaces", std::move(workspaces)},
                {"styles", std::move(styles)},
                {"semantic_contract", {
                    {"value", "current intrinsic value; absent when not applicable"},
                    {"selected", "selection state; omitted when unavailable or inapplicable"},
                    {"active", "currently interacted with; omitted when unavailable or inapplicable"},
                    {"focused", "keyboard or navigation focus; omitted when unavailable"},
                    {"enabled", "whether the control accepts interaction"}}},
                {"coverage", {{"status", "partial"},
                    {"unavailable", {"higher_window_occlusion", "resolved_backend_style"}},
                    {"unmeasured_geometry_elements", unmeasured},
                    {"partial_capture", capture.partial_coverage},
                    {"omitted_elements", capture.omitted_nodes},
                    {"reasons", capture.coverage_reasons}}}};
            return result;
        }

    }

    namespace
    {
        struct SessionRecord
        {
            ui::UiSnapshotStore *store = nullptr;
            std::string host;
            std::string mode;
        };

        struct SessionHub
        {
            std::mutex mutex;
            std::map<ui::UiSessionId, SessionRecord> sessions;
            runtime::command::CommandRegistration sessions_command;
            runtime::command::CommandRegistration snapshot_command;
        };

        std::mutex g_hubs_mutex;
        std::unordered_map<runtime::command::CommandRegistry *, std::weak_ptr<SessionHub>> g_hubs;

        bool InstallCommands(runtime::command::CommandRegistry &registry,
                             const std::shared_ptr<SessionHub> &hub,
                             std::string &diagnostic)
        {
            using namespace runtime::command;
            const std::weak_ptr<SessionHub> weak_hub = hub;
            const auto install = [&registry, &diagnostic](CommandDesc command,
                CommandRegistration &slot)
            {
                CommandRegistrationResult result = registry.Register(std::move(command));
                if (!result.IsSuccess())
                {
                    diagnostic = result.diagnostic;
                    return false;
                }
                slot = std::move(result.registration);
                return true;
            };
            if (!install({"ui.sessions", "Editor", "List GUI sessions registered by Editor instances",
                    CommandCategory::Engine, CommandFlags::AgentAllowed, {},
                    [weak_hub](const CommandCall &, const CommandContext &context)
                    {
                        const auto locked_hub = weak_hub.lock();
                        if (!locked_hub)
                            return CommandResult{CommandStatus::Success, "No GUI sessions are attached",
                                                 context.request_id,
                                                 {{"sessions", Json{{"items", Json::array()}}.dump()}}};
                        Json items = Json::array();
                        {
                            std::scoped_lock lock(locked_hub->mutex);
                            for (const auto &[id, record] : locked_hub->sessions)
                                items.push_back({{"session_id", std::to_string(id)},
                                    {"host", record.host}, {"mode", record.mode}});
                        }
                        CommandResult result{CommandStatus::Success,
                            items.empty() ? "No GUI sessions are attached" : "Registered GUI sessions",
                            context.request_id, {}};
                        result.structured_json.emplace("sessions", Json{{"items", std::move(items)}}.dump());
                        return result;
                    }, CommandThread::Immediate}, hub->sessions_command))
                return false;

            CommandSchema schema{{
                {"session_id", CommandValueType::String, false, {}, {}},
                {"view", CommandValueType::Enum, false, std::string{"visual"},
                 {"semantic", "spatial", "visual"}}}};
            if (!install({"ui.snapshot", "Editor", "Query a completed Editor UI session frame",
                    CommandCategory::Engine, CommandFlags::AgentAllowed, std::move(schema),
                    [weak_hub](const CommandCall &call, const CommandContext &context)
                    {
                        const auto locked_hub = weak_hub.lock();
                        if (!locked_hub)
                            return CommandResult{CommandStatus::NotFound,
                                "No GUI session is registered", context.request_id, {}};
                        ui::UiSessionId session = 0;
                        ui::UiSnapshotStore *store = nullptr;
                        {
                            std::scoped_lock lock(locked_hub->mutex);
                            const auto requested = call.arguments.find("session_id");
                            if (requested != call.arguments.end())
                            {
                                const auto *value = std::get_if<std::string>(&requested->second);
                                if (value == nullptr)
                                    return CommandResult{CommandStatus::InvalidArguments,
                                        "session_id must be a string", context.request_id, {}};
                                try { session = static_cast<ui::UiSessionId>(std::stoull(*value)); }
                                catch (const std::exception &)
                                { return CommandResult{CommandStatus::InvalidArguments,
                                    "session_id must be an unsigned integer string", context.request_id, {}}; }
                                const auto found = locked_hub->sessions.find(session);
                                if (found == locked_hub->sessions.end())
                                    return CommandResult{CommandStatus::NotFound,
                                        "GUI session was not found", context.request_id, {}};
                                store = found->second.store;
                            }
                            else
                            {
                                if (locked_hub->sessions.size() != 1)
                                    return CommandResult{CommandStatus::InvalidArguments,
                                        "session_id is required when zero or multiple GUI sessions are registered",
                                        context.request_id, {}};
                                session = locked_hub->sessions.begin()->first;
                                store = locked_hub->sessions.begin()->second.store;
                            }
                            const auto capture = store->Latest(session);
                            if (!capture)
                                return CommandResult{CommandStatus::Busy,
                                    "No successfully submitted UI snapshot is available yet",
                                    context.request_id, {}};
                            try
                            {
                                std::string view = "visual";
                                if (const auto requested = call.arguments.find("view");
                                    requested != call.arguments.end())
                                    view = std::get<std::string>(requested->second);
                                CommandResult result{CommandStatus::Success, "UI snapshot captured",
                                                     context.request_id, {}};
                                const bool geometry = view != "semantic";
                                const bool styles = view == "visual";
                                result.structured_json.emplace("snapshot",
                                    UiSnapshotJson(*capture, geometry, styles).dump());
                                return result;
                            }
                            catch (const std::exception &error)
                            {
                                return CommandResult{CommandStatus::Failed,
                                    std::string{"UI snapshot serialization failed: "} + error.what(),
                                    context.request_id, {}};
                            }
                        }
                    }, CommandThread::Immediate}, hub->snapshot_command))
            {
                hub->sessions_command = {};
                return false;
            }
            diagnostic.clear();
            return true;
        }
    }

    EditorUiSessionRegistration::~EditorUiSessionRegistration()
    {
        Reset();
    }

    EditorUiSessionRegistration::EditorUiSessionRegistration(
        EditorUiSessionRegistration &&other) noexcept
        : release_(std::move(other.release_))
    {
        other.release_ = {};
    }

    EditorUiSessionRegistration &EditorUiSessionRegistration::operator=(
        EditorUiSessionRegistration &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            release_ = std::move(other.release_);
            other.release_ = {};
        }
        return *this;
    }

    void EditorUiSessionRegistration::Reset() noexcept
    {
        if (!release_) return;
        try { release_(); }
        catch (...) {}
        release_ = {};
    }

    EditorUiSessionRegistration RegisterEditorUiSession(
        runtime::command::CommandRegistry &registry,
        ui::UiSnapshotStore &store,
        const ui::UiSessionId session,
        std::string host,
        std::string mode,
        std::string &diagnostic)
    {
        diagnostic.clear();
        if (session == 0)
        {
            diagnostic = "Editor UI session id must be non-zero";
            return {};
        }

        std::scoped_lock hubs_lock(g_hubs_mutex);
        std::shared_ptr<SessionHub> hub;
        if (const auto found = g_hubs.find(&registry); found != g_hubs.end())
            hub = found->second.lock();
        if (!hub)
        {
            hub = std::make_shared<SessionHub>();
            if (!InstallCommands(registry, hub, diagnostic)) return {};
            g_hubs.insert_or_assign(&registry, hub);
        }
        {
            std::scoped_lock sessions_lock(hub->mutex);
            if (!hub->sessions.emplace(session, SessionRecord{&store, std::move(host), std::move(mode)}).second)
            {
                diagnostic = "Editor UI session id is already registered";
                return {};
            }
        }

        return EditorUiSessionRegistration{[hub, registry_ptr = &registry, session]
        {
            std::scoped_lock hubs_lock(g_hubs_mutex);
            std::scoped_lock sessions_lock(hub->mutex);
            hub->sessions.erase(session);
            if (!hub->sessions.empty()) return;
            hub->snapshot_command = {};
            hub->sessions_command = {};
            const auto found = g_hubs.find(registry_ptr);
            if (found != g_hubs.end() && found->second.lock() == hub)
                g_hubs.erase(found);
        }};
    }

}
