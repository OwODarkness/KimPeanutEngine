#include "editor/ui/component/editor_window_component.h"
namespace kpengine::editor
{

    EditorWindowComponent::EditorWindowComponent(const std::string &title, EditorWindowConfig config)
        : title_(title), config_(config), locked_(config.locked) {}

    void EditorWindowComponent::Render()
    {
        if (is_open_)
        {
            ImGuiViewport *viewport = ImGui::GetMainViewport();
            const ImVec2 pos(viewport->WorkPos.x + config_.pos_x_ratio * viewport->WorkSize.x,
                             viewport->WorkPos.y + config_.pos_y_ratio * viewport->WorkSize.y);
            const ImVec2 size(config_.width_ratio * viewport->WorkSize.x,
                              config_.height_ratio * viewport->WorkSize.y);

            // Locked: pin to the viewport every frame; unlocked: set once, let the user move.
            const ImGuiCond cond = locked_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
            ImGui::SetNextWindowPos(pos, cond);
            ImGui::SetNextWindowSize(size, cond);

            ImGuiWindowFlags flags = 0;
            if (locked_)
            {
                flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
            }
            ImGui::Begin(title_.c_str(), &is_open_, flags);
            RenderWindowChrome();
            RenderContent();
            ImGui::End();
        }
    }

    void EditorWindowComponent::RenderWindowChrome()
    {
        // Begin() clips drawing and culls items to the content area (below the title
        // bar), so the chrome here would be invisible. Push a clip over the bar instead.
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
        if (ImGui::Button("##lock", btn_size))
        {
            locked_ = !locked_;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", locked_ ? "Unlock (move/resize)" : "Lock (pin to viewport)");
        }

        // Padlock: filled body when locked, hollow body + lifted shackle when unlocked.
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec2 c(btn_pos.x + btn_size.x * 0.5f, btn_pos.y + btn_size.y * 0.58f);
        const float r = btn_size.x * 0.30f;
        const float shackle_y = c.y - r * (locked_ ? 0.95f : 1.4f);
        draw->AddCircle(ImVec2(c.x, shackle_y), r * 0.55f, col, 0, 2.0f);
        const ImVec2 body_min(c.x - r, c.y - r * 0.6f);
        const ImVec2 body_max(c.x + r, c.y + r);
        if (locked_)
            draw->AddRectFilled(body_min, body_max, col, r * 0.5f);
        else
            draw->AddRect(body_min, body_max, col, r * 0.5f, 0, 2.0f);

        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(content_cursor);
    }

    void EditorWindowComponent::RenderContent()
    {
        pos_x = ImGui::GetWindowPos().x;
        pos_y = ImGui::GetWindowPos().y;
        width_ = (int)ImGui::GetContentRegionAvail().x;
        height_ = (int)ImGui::GetContentRegionAvail().y;

        for (int i = 0; i < components_.size(); i++)
        {
            components_[i]->Render();
        }
    }

    void EditorWindowComponent::AddComponent(std::shared_ptr<EditorUIComponent> component)
    {
        components_.push_back(component);
    }

    void EditorWindowComponent::SetLocked(bool locked)
    {
        locked_ = locked;
    }

    bool EditorWindowComponent::IsLocked() const
    {
        return locked_;
    }

    EditorWindowComponent::~EditorWindowComponent() = default;

}