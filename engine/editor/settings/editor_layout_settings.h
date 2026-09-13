#ifndef KPENGINE_EDITOR_LAYOUT_SETTINGS_H
#define KPENGINE_EDITOR_LAYOUT_SETTINGS_H

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_tool_row_model.h"

// ImGui-free, like the layout and tool-row models it describes, so the round-trip is
// unit-testable.
//
// The file is keyed by splitter and region ID, never by index: inserting or removing a
// split (which ED3 does) must not silently reattach a saved size to a different seam.
//
// It is written to its own file under save/, NOT to config/settings.json. That file is
// checked in and parsed by two independent readers -- the editor's, and live2d's for
// window_background_color -- so a runtime rewrite would clobber another subsystem's key.

namespace kpengine::editor
{
    inline constexpr int kEditorLayoutStateVersion = 3;

    // Versions 1 and 2 are still READ. Version 1 is known and simply has no placements.
    // Version 2 keyed placements by REGION, which a dock cannot do — a dock holds several
    // panels, so a region key cannot name one of them. Refusing v2 would discard a user's
    // arrangement on upgrade for no reason, so its records are converted instead.
    inline constexpr int kEditorLayoutStateMinVersion = 1;

    // Sentinel for "the file did not specify this split", so a partial or older file
    // leaves the remaining splits at the model's defaults instead of at zero.
    inline constexpr float kEditorLayoutUnspecified = -1.0f;

    // One panel's placement: which dock it is in, and whether it is locked. Keyed by PANEL
    // because that is the thing a dock cannot name: several panels share one dock key,
    // nothing shares a panel id.
    struct EditorPlacementRecord
    {
        std::string panel_id;
        // An EditorLayoutModel::RegionKey, or empty for a panel that floats on its own.
        std::string dock_key;
        bool locked = false;
    };

    struct EditorLayoutState
    {
        int version = kEditorLayoutStateVersion;
        std::array<float, kEditorSplitterCount> fractions = {};
        std::vector<EditorPlacementRecord> placements;

        EditorLayoutState();
    };

    // Reads the persisted layout. A missing file is normal, not an error: this file is
    // generated, so a fresh checkout simply has defaults. A malformed file, an unknown
    // version, or an unknown key yields defaults and appends to `diagnostic` rather than
    // throwing, because a broken layout preference must never stop the editor starting.
    EditorLayoutState ReadEditorLayoutState(const std::string &path,
                                            std::string *diagnostic = nullptr);

    // Writes atomically: create the parent directory, write a unique temp file, then
    // rename over the destination. Throws on failure, mirroring the repo's other
    // durable writes; a half-written layout file would be worse than none.
    void WriteEditorLayoutState(const std::string &path, const EditorLayoutState &state);

    EditorLayoutState CaptureLayoutState(const EditorLayoutModel &model);

    // Applies only the splits the state actually specified.
    void ApplyLayoutState(const EditorLayoutState &state, EditorLayoutModel &model);

    // Placements are captured separately from the splits because they read a different
    // model. Called after CaptureLayoutState, before WriteEditorLayoutState.
    void CapturePlacementState(const EditorToolRowModel &model, EditorLayoutState &state);

    // Applies the placements, skipping any that this build cannot honour: an unknown dock
    // key or an unknown panel id. Must run AFTER every panel has been registered, since a
    // placement names a panel that has to exist. Ignores the lock, deliberately: restoring
    // where a panel was is not a drag, and a locked panel must come back where it was.
    void ApplyPlacementState(const EditorLayoutState &state, EditorToolRowModel &model,
                             std::string *diagnostic = nullptr);
}

#endif // KPENGINE_EDITOR_LAYOUT_SETTINGS_H
