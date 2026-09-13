#ifndef KPENGINE_EDITOR_LAYOUT_SETTINGS_H
#define KPENGINE_EDITOR_LAYOUT_SETTINGS_H

#include <array>
#include <string>

#include "editor/ui/component/editor_layout_model.h"

// ImGui-free, like the layout model it describes, so the round-trip is unit-testable.
//
// The file is keyed by splitter ID, never by index: inserting or removing a split
// (which ED3 will do) must not silently reattach a saved size to a different seam.
//
// It is written to its own file under save/, NOT to config/settings.json. That file is
// checked in and parsed by two independent readers -- the editor's, and live2d's for
// window_background_color -- so a runtime rewrite would clobber another subsystem's key.

namespace kpengine::editor
{
    inline constexpr int kEditorLayoutStateVersion = 1;

    // Sentinel for "the file did not specify this split", so a partial or older file
    // leaves the remaining splits at the model's defaults instead of at zero.
    inline constexpr float kEditorLayoutUnspecified = -1.0f;

    struct EditorLayoutState
    {
        int version = kEditorLayoutStateVersion;
        std::array<float, kEditorSplitterCount> fractions = {};

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
}

#endif // KPENGINE_EDITOR_LAYOUT_SETTINGS_H
