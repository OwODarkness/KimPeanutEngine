#include "pointcloud_component.h"

#include "runtime_global_context.h"
#include "render/render_scene.h"
#include "render/render_system.h"
#include "render/render_pointcloud.h"
#include "render/pointcloud_scene_proxy.h"
#include "log/logger.h"



namespace kpengine{
    PointCloudComponent::PointCloudComponent():
    pointcloud_(nullptr)
    {
    }

    PointCloudComponent::PointCloudComponent(const std::string& relative_path):
    pointcloud_(std::make_shared<RenderPointCloud>(relative_path))
    {

    }

    void PointCloudComponent::Initialize()
    {
        pointcloud_->Initialize();
        scene_proxy_ = std::make_shared<PointCloudSceneProxy>();
        PointCloudSceneProxy* pointcloud_proxy = dynamic_cast<PointCloudSceneProxy*>(scene_proxy_.get());
        if(pointcloud_proxy)
        {
            pointcloud_proxy->vao_ = pointcloud_->vao_;
            pointcloud_proxy->pointcloud_resource_ref_ = pointcloud_->GetPointCloudResource();
            pointcloud_proxy->Initialize();
        }
        RegisterSceneProxy();
    }

    void PointCloudComponent::RegisterSceneProxy()
    {
        proxy_handle_ = runtime::global_runtime_context.render_system_->GetRenderScene()->AddProxy(scene_proxy_);
        if(proxy_handle_.IsValid())
        {
            KP_LOG("PointCloudLog", LOG_LEVEL_INFO, "proxy %s register succeed", pointcloud_->GetName().c_str());
        }
        else
        {
            KP_LOG("PointCloudLog", LOG_LEVEL_ERROR, "proxy %s register failed", pointcloud_->GetName().c_str());
        }
    }

    void PointCloudComponent::UnRegisterSceneProxy()
    {
        runtime::global_runtime_context.render_system_->GetRenderScene()->RemoveProxy(proxy_handle_);
    }

    PointCloudComponent::~PointCloudComponent()
    {
        UnRegisterSceneProxy();
    }
}