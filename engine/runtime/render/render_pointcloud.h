#ifndef KPENGINE_RUNTIME_RENDER_POINTCLOUD_H
#define KPENGINE_RUNTIME_RENDER_POINTCLOUD_H

#include <memory>
#include <string>

namespace kpengine{
    class RenderPointCloudResource;

    class RenderPointCloud{
    public:
        RenderPointCloud();
        RenderPointCloud(const std::string& relative_path);

        ~RenderPointCloud();
        void Initialize();

        RenderPointCloudResource* GetPointCloudResource() {return pointcloud_resource_.get();}
        std::string GetName() const{return name_;}
    public:
        unsigned int vao_;
        unsigned int vbo_;
    private:
        std::unique_ptr<RenderPointCloudResource> pointcloud_resource_;
        std::string name_;
    
    };
    
}
#endif