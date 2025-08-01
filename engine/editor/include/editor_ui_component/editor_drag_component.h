#ifndef KPENGINE_EDITOR_DRAG_COMPONENT_H
#define KPENGINE_EDITOR_DRAG_COMPONENT_H

#include <string>
#include "editor/include/editor_ui_component.h"
#include "runtime/core/math/math_header.h"

namespace kpengine {
namespace ui {

    // Base template
    template<typename T>
    class EditorDragComponent : public EditorUIComponent {
    public:
        EditorDragComponent(const std::string& label, T* data, float speed, T min_value, T max_value)
            : label_(label), data_(data), speed_(speed), min_value_(min_value), max_value_(max_value)
        {}

        void Render() override {
        }

        void BindData(T* data)
        {
            data_ = data;
        }

    protected:
        std::string label_;
        T* data_;
        float speed_;
        T min_value_;
        T max_value_;
    };

    // Specialization for float
    template<>
    class EditorDragComponent<float> : public EditorUIComponent {
    public:
        EditorDragComponent(const std::string& label, float* data, float speed = 0.1f, float min_value = 0.0f, float max_value = 1.0f)
            : label_(label), data_(data), speed_(speed), min_value_(min_value), max_value_(max_value)
        {}

        void Render() override {
            if (data_) {
                ImGui::DragFloat(label_.c_str(), data_, speed_, min_value_, max_value_, "%.3f");
            }
        }

    private:
        std::string label_;
        float* data_;
        float speed_;
        float min_value_;
        float max_value_;
    };

    // Specialization for int
    template<>
    class EditorDragComponent<int> : public EditorUIComponent {
    public:
        EditorDragComponent(const std::string& label, int* data, float speed = 1.0f, int min_value = 0, int max_value = 100)
            : label_(label), data_(data), speed_(speed), min_value_(min_value), max_value_(max_value)
        {}

        void Render() override {
            if (data_) {
                ImGui::DragInt(label_.c_str(), data_, speed_, min_value_, max_value_);
            }
        }

    private:
        std::string label_;
        int* data_;
        float speed_;
        int min_value_;
        int max_value_;
    };




} // namespace ui
} // namespace kpengine

#endif // KPENGINE_EDITOR_DRAG_COMPONENT_H
