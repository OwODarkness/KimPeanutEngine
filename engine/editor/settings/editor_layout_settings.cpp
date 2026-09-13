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
            return nlohmann::json{{"version", state.version}, {"splits", std::move(splits)}};
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
        if (version != kEditorLayoutStateVersion)
        {
            // Keep the whole layout rather than half-applying a format we do not know.
            Note(diagnostic, "layout: unsupported version " + std::to_string(version));
            return state;
        }
        state.version = version;

        const auto splits = json.find("splits");
        if (splits == json.end() || !splits->is_object())
        {
            if (splits != json.end())
            {
                Note(diagnostic, "layout: splits is not an object");
            }
            return state;
        }

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
}
