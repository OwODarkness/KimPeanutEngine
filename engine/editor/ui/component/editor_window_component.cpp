#include "editor/ui/component/editor_window_component.h"
namespace kpengine::editor
{
    namespace
    {
        constexpr ImVec4 kFocusedTitleText(1.0f, 0.58f, 0.18f, 1.0f);
    }

    EditorWindowComponent::EditorWindowComponent(const std::string &title, EditorWindowConfig config)
        : title_(title), config_(config), locked_(config.locked) {}

    void EditorWindowComponent::Render()
    {
        if (!IsVisible())
        {
            return;
        }

        // A window that places itself. A panel hosted by the dock host is drawn by the
        // host instead, inside a window the host owns, so its own geometry never applies —
        // which is why there is no longer a second, layout-driven branch here.
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImVec2 pos(viewport->WorkPos.x + config_.pos_x_ratio * viewport->WorkSize.x,
                         viewport->WorkPos.y + config_.pos_y_ratio * viewport->WorkSize.y);
        const ImVec2 size(config_.width_ratio * viewport->WorkSize.x,
                          config_.height_ratio * viewport->WorkSize.y);
        // Locked: pin to the viewport every frame; unlocked: set once, let the user move.
        const ImGuiCond cond = locked_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos(pos, cond);
        ImGui::SetNextWindowSize(size, cond);

        ImGuiWindowFlags flags = config_.extra_flags;
        if (locked_)
        {
            flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        }

        // ImGui renders the native title before Begin() returns. Use the
        // previous frame's focus state so only the title receives the
        // focus accent; the content keeps the normal theme text color.
        if (focused_last_frame_)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kFocusedTitleText);
        }
        // Write the close click into a local, never into is_open_: latching there
        // would make closing terminal, because the window would stop being
        // submitted and nothing could reopen it.
        bool open = true;
        ImGui::Begin(title_.c_str(), HasCloseButton() ? &open : nullptr, flags);
        if (focused_last_frame_)
        {
            ImGui::PopStyleColor();
        }
        RenderFocusAccent();
        // Here the padlock still means "pin to my own geometry": this window places
        // itself, so unlocking it is what lets the user move it. A hosted panel's padlock
        // means something else — see the placement model's `locked`.
        if (RenderLockButton(locked_))
        {
            locked_ = !locked_;
        }
        RenderContent();
        focused_last_frame_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::End();

        if (!open)
        {
            if (visibility_ != nullptr)
            {
                visibility_->SetOpen(false);
            }
            else
            {
                is_open_ = false;
            }
        }
    }

    void EditorWindowComponent::SetVisibility(EditorWindowVisibility *visibility) noexcept
    {
        visibility_ = visibility;
    }

    bool EditorWindowComponent::IsVisible() const noexcept
    {
        return visibility_ != nullptr ? visibility_->IsOpen() : is_open_;
    }

    void EditorWindowComponent::RenderFocusAccent()
    {
        // Begin() clips drawing and culls items to the content area (below the title
        // bar), so the chrome here would be invisible. Push a clip over the bar instead.
        const ImVec2 win_pos = ImGui::GetWindowPos();
        const float title_h = ImGui::GetFrameHeight();
        const ImVec2 clip_max(win_pos.x + ImGui::GetWindowWidth(), win_pos.y + title_h);
        ImGui::PushClipRect(win_pos, clip_max, false);

        const ImVec2 content_cursor = ImGui::GetCursorScreenPos();
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const ImU32 focus_color =
            ImGui::GetColorU32(focused ? ImGuiCol_NavHighlight : ImGuiCol_Border);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(win_pos.x, win_pos.y),
            ImVec2(win_pos.x + ImGui::GetWindowWidth(), win_pos.y + 2.0f), focus_color);

        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(content_cursor);
    }

    bool EditorWindowComponent::RenderLockButton(bool locked)
    {
        // Draws and hit-tests the padlock in the title bar, and reports a click. The flag
        // belongs to the CALLER: this window keeps its own, and the dock host keeps its
        // panels' locks in the placement model, so one drawing serves both without either
        // having to own the other's state.
        bool clicked = false;
        const ImVec2 win_pos = ImGui::GetWindowPos();
        const float title_h = ImGui::GetFrameHeight();
        const ImVec2 clip_max(win_pos.x + ImGui::GetWindowWidth(), win_pos.y + title_h);
        ImGui::PushClipRect(win_pos, clip_max, false);

        const ImVec2 content_cursor = ImGui::GetCursorScreenPos();
        // Lock toggle in the title bar, matching the native close button's geometry.
        const ImGuiStyle &style = ImGui::GetStyle();
        const float button_sz = ImGui::GetFontSize();
        const float pad_r = style.FramePadding.x + button_sz + style.ItemInnerSpacing.x; // leaves room for close
        const ImVec2 btn_size(button_sz, button_sz);
        const ImVec2 btn_pos(win_pos.x + ImGui::GetWindowWidth() - pad_r - button_sz,
                             win_pos.y + style.FramePadding.y);
        ImGui::SetCursorScreenPos(btn_pos);
        clicked = ImGui::Button("##lock", btn_size);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", locked ? "Unlock (drag to a dock)" : "Lock (cannot be dragged)");
        }

        // Padlock: filled body when locked, hollow body + lifted shackle when unlocked.
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec2 c(btn_pos.x + btn_size.x * 0.5f, btn_pos.y + btn_size.y * 0.58f);
        const float r = btn_size.x * 0.30f;
        const float shackle_y = c.y - r * (locked ? 0.95f : 1.4f);
        draw->AddCircle(ImVec2(c.x, shackle_y), r * 0.55f, col, 0, 2.0f);
        const ImVec2 body_min(c.x - r, c.y - r * 0.6f);
        const ImVec2 body_max(c.x + r, c.y + r);
        if (locked)
            draw->AddRectFilled(body_min, body_max, col, r * 0.5f);
        else
            draw->AddRect(body_min, body_max, col, r * 0.5f, 0, 2.0f);

        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(content_cursor);
        return clicked;
    }

    void EditorWindowComponent::RenderContent()
    {
        for (int i = 0; i < components_.size(); i++)
        {
            components_[i]->Render();
        }
    }

    void EditorWindowComponent::AddComponent(std::shared_ptr<EditorUIComponent> component)
    {
        components_.push_back(component);
    }

    EditorWindowComponent::~EditorWindowComponent() = default;

}
