#include "editor/settings/editor_layout_settings.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace kpengine::editor
{
    namespace
    {
        void Note(std::string *diagnostic, std::string message)
        {
            if (diagnostic != nullptr)
            {
                if (!diagnostic->empty())
                {
                    *diagnostic += "; ";
                }
                *diagnostic += message;
            }
        }

        // One nested object per split rather than a bare number map, so a later stage can
        // add per-split fields without migrating the file shape.
        nlohmann::json ToJson(const EditorLayoutState &state)
        {
            nlohmann::json splits = nlohmann::json::object();
            for (std::size_t index = 0; index < kEditorSplitterCount; ++index)
            {
                const auto id = static_cast<EditorSplitterId>(index);
                const char *const key = EditorLayoutModel::SplitterKey(id);
                if (key == nullptr || key[0] == '\0')
                {
                    continue;
                }
                const float fraction = state.fractions[index];
                if (fraction < 0.0f)
                {
                    // Unspecified: omit rather than persist a sentinel that a reader
                    // would have to know about.
                    continue;
                }
                splits[key] = nlohmann::json{{"amount", fraction}};
            }

            // Keyed by panel. It was keyed by dock until docks could hold several panels,
            // at which point a dock key stopped naming one of them.
            nlohmann::json placements = nlohmann::json::object();
            for (const EditorPlacementRecord &record : state.placements)
            {
                placements[record.panel_id] =
                    nlohmann::json{{"dock", record.dock_key}, {"locked", record.locked}};
            }

            return nlohmann::json{{"version", state.version},
                                  {"splits", std::move(splits)},
                                  {"placements", std::move(placements)}};
        }
    }

    EditorLayoutState::EditorLayoutState()
    {
        fractions.fill(kEditorLayoutUnspecified);
    }

    EditorLayoutState ReadEditorLayoutState(const std::string &path, std::string *diagnostic)
    {
        EditorLayoutState state;

        std::error_code error;
        if (!std::filesystem::exists(path, error) || error)
        {
            // Normal for a fresh checkout: defaults, no complaint.
            return state;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            Note(diagnostic, "layout: could not open " + path);
            return state;
        }

        nlohmann::json json;
        try
        {
            json = nlohmann::json::parse(file);
        }
        catch (const std::exception &e)
        {
            Note(diagnostic, std::string{"layout: malformed JSON ("} + e.what() + ")");
            return state;
        }

        if (!json.is_object())
        {
            Note(diagnostic, "layout: root is not an object");
            return state;
        }

        const int version = json.value("version", kEditorLayoutStateVersion);
        if (version < kEditorLayoutStateMinVersion || version > kEditorLayoutStateVersion)
        {
            // Keep the whole layout rather than half-applying a format we do not know.
            // Version 1 is inside this range and simply has no placements: dropping it
            // would discard the user's saved split sizes for no benefit.
            Note(diagnostic, "layout: unsupported version " + std::to_string(version));
            return state;
        }
        state.version = version;

        // Both blocks are read even when the other is missing or malformed: the file has
        // two independent halves, and a bad split must not cost the user their
        // placements. Neither half returns early any more.
        const auto splits = json.find("splits");
        if (splits != json.end() && !splits->is_object())
        {
            Note(diagnostic, "layout: splits is not an object");
        }
        else if (splits != json.end())
        {
            for (const auto &[key, value] : splits->items())
            {
                const EditorSplitterId id = EditorLayoutModel::SplitterFromKey(key);
                if (id == EditorSplitterId::None)
                {
                    // Forward compatibility: a key this build does not know is not fatal.
                    Note(diagnostic, "layout: unknown split '" + key + "'");
                    continue;
                }

                const auto amount = value.is_object() ? value.find("amount") : value.end();
                if (amount == value.end() || !amount->is_number())
                {
                    Note(diagnostic, "layout: split '" + key + "' has no numeric amount");
                    continue;
                }

                const float fraction = amount->get<float>();
                if (!(fraction >= 0.0f) || fraction > 1.0f)
                {
                    Note(diagnostic, "layout: split '" + key + "' amount is out of range");
                    continue;
                }
                state.fractions[static_cast<std::size_t>(id)] = fraction;
            }
        }

        const auto placements = json.find("placements");
        if (placements != json.end() && !placements->is_object())
        {
            Note(diagnostic, "layout: placements is not an object");
        }
        else if (placements != json.end())
        {
            for (const auto &[key, value] : placements->items())
            {
                if (version < 3)
                {
                    // Version 2 keyed by DOCK and stored a bare panel id: {"gpu_profiler":
                    // "log"}. Read it into the panel-keyed shape so an arrangement saved by
                    // the previous build survives, rather than being dropped on upgrade.
                    if (EditorLayoutModel::RegionFromKey(key) == EditorLayoutSlot::Count)
                    {
                        Note(diagnostic, "layout: unknown region '" + key + "'");
                        continue;
                    }
                    if (!value.is_string())
                    {
                        Note(diagnostic, "layout: region '" + key + "' has no panel id");
                        continue;
                    }
                    state.placements.push_back(
                        EditorPlacementRecord{value.get<std::string>(), key, false});
                    continue;
                }

                // Version 3 keys by panel and stores {dock, locked}; an empty dock means
                // the panel floats on its own.
                EditorPlacementRecord record;
                record.panel_id = key;
                if (!value.is_object())
                {
                    Note(diagnostic, "layout: panel '" + key + "' has no placement object");
                    continue;
                }
                const auto dock = value.find("dock");
                if (dock == value.end() || !dock->is_string())
                {
                    Note(diagnostic, "layout: panel '" + key + "' has no dock key");
                    continue;
                }
                record.dock_key = dock->get<std::string>();
                if (!record.dock_key.empty() &&
                    EditorLayoutModel::RegionFromKey(record.dock_key) == EditorLayoutSlot::Count)
                {
                    Note(diagnostic, "layout: panel '" + key + "' names an unknown dock '" +
                                         record.dock_key + "'");
                    continue;
                }
                // Absent means unlocked, so a file written before the lock existed reads
                // as every panel unlocked rather than as every panel locked.
                record.locked = value.value("locked", false);
                state.placements.push_back(std::move(record));
            }
        }

        return state;
    }

    void WriteEditorLayoutState(const std::string &path, const EditorLayoutState &state)
    {
        const std::filesystem::path destination(path);
        const std::filesystem::path parent = destination.parent_path();
        if (!parent.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                throw std::runtime_error("layout: could not create " + parent.string() +
                                         ": " + error.message());
            }
        }

        // Write beside the destination and rename, so a crash mid-write cannot leave a
        // truncated file that the next launch would have to reject.
        const std::filesystem::path temporary =
            destination.string() + ".tmp." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

        try
        {
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    throw std::runtime_error("layout: could not open " + temporary.string());
                }
                file << ToJson(state).dump(2) << '\n';
                if (!file.good())
                {
                    throw std::runtime_error("layout: write failed for " + temporary.string());
                }
            }

            std::error_code error;
            std::filesystem::rename(temporary, destination, error);
            if (error)
            {
                throw std::runtime_error("layout: could not replace " + destination.string() +
                                         ": " + error.message());
            }
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }

    EditorLayoutState CaptureLayoutState(const EditorLayoutModel &model)
    {
        EditorLayoutState state;
        for (std::size_t index = 0; index < kEditorSplitterCount; ++index)
        {
            const auto id = static_cast<EditorSplitterId>(index);
            if (EditorLayoutModel::SplitterKey(id)[0] == '\0' ||
                !model.IsSplitterDraggable(id))
            {
                // The status bar's height is content-derived; persisting it would pin a
                // theme-dependent measurement into a user's layout file.
                continue;
            }
            state.fractions[index] = model.SplitterFraction(id);
        }
        return state;
    }

    void ApplyLayoutState(const EditorLayoutState &state, EditorLayoutModel &model)
    {
        for (std::size_t index = 0; index < kEditorSplitterCount; ++index)
        {
            const float fraction = state.fractions[index];
            if (fraction < 0.0f)
            {
                continue;  // unspecified: leave the model's default in place
            }
            model.SetSplitterFraction(static_cast<EditorSplitterId>(index), fraction);
        }
    }

    void CapturePlacementState(const EditorToolRowModel &model, EditorLayoutState &state)
    {
        state.placements.clear();
        // EVERY entry is recorded, not only the docked ones. With docks, "which dock" is
        // the whole placement, and a panel that floats has a placement too — none — so
        // skipping it would silently return it to its default dock on the next launch.
        for (std::size_t index = 0; index < model.GetEntryCount(); ++index)
        {
            const EditorToolRowEntry *const entry = model.GetEntry(index);
            if (entry == nullptr)
            {
                continue;
            }
            EditorPlacementRecord record;
            record.panel_id = entry->id;
            // Dock locks belong to the row container, but the v3 file is panel-keyed.
            // Repeat the effective container lock on each member so older files and the
            // existing tolerant reader remain usable.
            record.locked = model.IsLocked(index);
            if (entry->dock.has_value())
            {
                const char *const dock_key = EditorLayoutModel::RegionKey(*entry->dock);
                if (dock_key == nullptr || dock_key[0] == '\0')
                {
                    // A region with no stable key must not be written under a made-up one.
                    continue;
                }
                record.dock_key = dock_key;
            }
            state.placements.push_back(std::move(record));
        }
    }

    void ApplyPlacementState(const EditorLayoutState &state, EditorToolRowModel &model,
                             std::string *diagnostic)
    {
        // Restore positions before locks. A dock lock belongs to the destination container,
        // so applying the first record's lock early could block a later panel that is still
        // being moved out of that same dock.
        for (const EditorPlacementRecord &record : state.placements)
        {
            if (record.panel_id.empty())
            {
                continue;
            }
            const std::optional<std::size_t> index = model.IndexOf(record.panel_id);
            if (!index.has_value())
            {
                Note(diagnostic, "layout: unknown panel '" + record.panel_id + "'");
                continue;
            }

            bool restored = false;
            if (record.dock_key.empty())
            {
                restored = !model.GetDock(*index).has_value() || model.FloatPanel(*index);
            }
            else
            {
                restored = model.MoveToDockById(
                    record.panel_id, EditorLayoutModel::RegionFromKey(record.dock_key));
            }
            if (!restored)
            {
                Note(diagnostic, "layout: panel '" + record.panel_id +
                                     "' could not be moved to '" + record.dock_key + "'");
            }
        }

        // Locks are restored last, after all panels have joined their containers. Repeated
        // records for one dock intentionally converge on the final recorded state.
        for (const EditorPlacementRecord &record : state.placements)
        {
            const std::optional<std::size_t> index = model.IndexOf(record.panel_id);
            if (index.has_value())
            {
                model.SetLocked(*index, record.locked);
            }
        }
    }
}
