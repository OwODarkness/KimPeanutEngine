#ifndef KPENGINE_EDITOR_ASSET_FILE_DIALOG_H
#define KPENGINE_EDITOR_ASSET_FILE_DIALOG_H

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kpengine::editor
{
    // Titles, filter labels/patterns and returned paths use UTF-8.
    struct EditorFileDialogFilter
    {
        std::string_view label;
        std::string_view patterns;
    };

    struct EditorOpenFilesRequest
    {
        std::string_view title;
        std::span<const EditorFileDialogFilter> filters;
        bool allow_multiple = false;
    };

    // Paths are returned as UTF-8. Empty results mean cancellation or unsupported platform.
    std::vector<std::string> PickFiles(const EditorOpenFilesRequest &request);
    std::string PickFolder(std::string_view title);
}

#endif
