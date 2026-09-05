#ifndef KPENGINE_EDITOR_THEME_H
#define KPENGINE_EDITOR_THEME_H

struct ImFont;

namespace kpengine::editor
{
    // Applies the editor's dark Codex-inspired palette to the current ImGui
    // context. Returns the optional code font for code-oriented widgets. The
    // caller owns the ImGui context and must create it first.
    ImFont *ApplyCodexTheme();
}

#endif // KPENGINE_EDITOR_THEME_H
