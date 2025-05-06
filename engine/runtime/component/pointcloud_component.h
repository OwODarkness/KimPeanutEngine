#ifndef KPENGINE_RUNTIME_POINTCLOUD_COMPONENT_H
#define KPENGINE_RUNTIME_POINTCLOUD_COMPONENT_H

#include <memory>
#include <string>

#include "primitive_component.h"

namespace kpengine{
    class RenderPointCloud;

    class PointCloudComponent: public PrimitiveComponent{
    public:
        PointCloudComponent();
        PointCloudComponent(const std::string& relatve_path);
        ~PointCloudComponent();
        void Initialize() override;
        protected:
        virtual void RegisterSceneProxy() override;
        virtual void UnRegisterSceneProxy() override;
    protected:
        std::shared_ptr<RenderPointCloud> pointcloud_;
    };
}

#endif