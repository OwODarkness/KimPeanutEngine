#include "editor/asset/editor_asset_file_dialog.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace kpengine::editor
{
    namespace
    {
#ifdef _WIN32
        std::optional<std::wstring> ToWide(const std::string_view utf8)
        {
            if (utf8.empty()) return std::wstring{};
            if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return std::nullopt;
            const int input_size = static_cast<int>(utf8.size());
            const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                utf8.data(), input_size, nullptr, 0);
            if (required <= 0) return std::nullopt;
            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), input_size,
                                    result.data(), required) != required)
                return std::nullopt;
            return result;
        }

        std::optional<std::string> ToUtf8(const std::wstring_view wide)
        {
            if (wide.empty()) return std::string{};
            if (wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return std::nullopt;
            const int input_size = static_cast<int>(wide.size());
            const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                wide.data(), input_size, nullptr, 0, nullptr, nullptr);
            if (required <= 0) return std::nullopt;
            std::string result(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), input_size,
                                    result.data(), required, nullptr, nullptr) != required)
                return std::nullopt;
            return result;
        }

        std::optional<std::vector<wchar_t>> BuildFilterBuffer(
            const std::span<const EditorFileDialogFilter> filters)
        {
            std::vector<wchar_t> result;
            const auto append = [&result](const std::string_view value)
            {
                if (value.find('\0') != std::string_view::npos) return false;
                const auto converted = ToWide(value);
                if (!converted || converted->empty()) return false;
                result.insert(result.end(), converted->begin(), converted->end());
                result.push_back(L'\0');
                return true;
            };

            if (filters.empty())
            {
                if (!append("All files") || !append("*.*")) return std::nullopt;
            }
            else
            {
                for (const EditorFileDialogFilter &filter : filters)
                {
                    if (!append(filter.label) || !append(filter.patterns))
                        return std::nullopt;
                }
            }
            result.push_back(L'\0');
            return result;
        }

        std::vector<std::string> ParseFileSelection(const std::vector<wchar_t> &buffer)
        {
            const wchar_t *cursor = buffer.data();
            const wchar_t *const end = cursor + buffer.size();
            const wchar_t *first_end = std::find(cursor, end, L'\0');
            if (first_end == end || first_end == cursor) return {};

            const std::wstring_view first{cursor,
                static_cast<std::size_t>(first_end - cursor)};
            cursor = first_end + 1;
            if (cursor == end || *cursor == L'\0')
            {
                const auto path = ToUtf8(first);
                return path ? std::vector<std::string>{*path} : std::vector<std::string>{};
            }

            const std::filesystem::path directory{first};
            std::vector<std::string> result;
            while (cursor < end && *cursor != L'\0')
            {
                const wchar_t *name_end = std::find(cursor, end, L'\0');
                if (name_end == end) return {};
                const std::filesystem::path selected = directory /
                    std::filesystem::path{std::wstring_view{cursor,
                        static_cast<std::size_t>(name_end - cursor)}};
                const auto path = ToUtf8(selected.native());
                if (!path) return {};
                result.push_back(*path);
                cursor = name_end + 1;
            }
            return result;
        }

        struct ItemListOwner
        {
            using ItemList = decltype(SHBrowseForFolderW(nullptr));
            ItemList item = nullptr;

            ~ItemListOwner() noexcept
            {
                CoTaskMemFree(static_cast<void *>(item));
            }
        };
#endif
    }

    std::vector<std::string> PickFiles(const EditorOpenFilesRequest &request)
    {
#ifdef _WIN32
        if (request.title.find('\0') != std::string_view::npos) return {};
        const auto title = ToWide(request.title);
        const auto filter = BuildFilterBuffer(request.filters);
        if (!title || !filter) return {};

        std::vector<wchar_t> path_buffer(65'536, L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrTitle = title->empty() ? nullptr : title->c_str();
        dialog.lpstrFilter = filter->data();
        dialog.lpstrFile = path_buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(path_buffer.size());
        dialog.nFilterIndex = 1;
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                       OFN_HIDEREADONLY;
        if (request.allow_multiple) dialog.Flags |= OFN_ALLOWMULTISELECT;
        if (!GetOpenFileNameW(&dialog)) return {};
        return ParseFileSelection(path_buffer);
#else
        (void)request;
        return {};
#endif
    }

    std::string PickFolder(const std::string_view title)
    {
#ifdef _WIN32
        if (title.find('\0') != std::string_view::npos) return {};
        const auto wide_title = ToWide(title);
        if (!wide_title) return {};

        BROWSEINFOW dialog{};
        dialog.lpszTitle = wide_title->empty() ? nullptr : wide_title->c_str();
        dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        const ItemListOwner selected{SHBrowseForFolderW(&dialog)};
        if (selected.item == nullptr) return {};

        std::array<wchar_t, MAX_PATH> path{};
        const bool valid = SHGetPathFromIDListW(selected.item, path.data()) != FALSE;
        if (!valid) return {};
        const auto result = ToUtf8(path.data());
        return result.value_or(std::string{});
#else
        (void)title;
        return {};
#endif
    }
}
