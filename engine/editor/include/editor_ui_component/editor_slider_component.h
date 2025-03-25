#ifndef KPENGINE_EDITOR_SLIDER_COMPONENT_H
#define KPENGINE_EDITOR_SLIDER_COMPONENT_H
#include "editor/include/editor_ui_component.h"
#include <string>
namespace kpengine{
namespace ui{
    template<typename T>
    class EditorSliderComponent: public EditorUIComponent{
    public:
    EditorSliderComponent(T* data, T min_bound,T max_bound):
    data_(data), min_bound_(min_bound),max_bound_(max_bound)
    {

    }
    void Render() override
    {

    }
    private:
    T* data_;
    T min_bound_;
    T max_bound_;
    };

   // Explicit specialization for float
   template<>
   class EditorSliderComponent<float> : public EditorUIComponent {
   public:
       EditorSliderComponent(const std::string& title, float* data, float min_bound, float max_bound)
           : EditorUIComponent(),
             data_(data),
             min_bound_(min_bound),
             max_bound_(max_bound),
             title_(title)
       {
       }

       void Render() override
       {
            if(data_ == nullptr)
            {
                return ;
            }
           // Note: Corrected parameter order - min first, then max
           ImGui::SliderFloat(title_.c_str(), data_, min_bound_, max_bound_);
       }

   private:
       float* data_;
       float min_bound_;
       float max_bound_;
       std::string title_;
   };

   template<>
   class EditorSliderComponent<int> : public EditorUIComponent {
   public:
       EditorSliderComponent(int* data, int min_bound, int max_bound)
           : EditorUIComponent(),
             data_(data),
             min_bound_(min_bound),
             max_bound_(max_bound)
       {
       }
   
       void Render() override
       {
            if(data_ == nullptr)
            {
                return ;
            }
           ImGui::SliderInt("##int_slider", data_, min_bound_, max_bound_);
       }
   
   private:
       int* data_;
       int min_bound_;
       int max_bound_;
   };
}
}

#endif