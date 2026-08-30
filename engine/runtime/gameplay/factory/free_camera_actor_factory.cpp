#include "gameplay/factory/free_camera_actor_factory.h"

#include "gameplay/actor/actor.h"
#include "gameplay/component/camera_component.h"
#include "gameplay/world/gameplay_world.h"

namespace kpengine::gameplay
{
    ActorHandle CreateFreeCameraActor(GameplayWorld &world, const FreeCameraActorDesc &desc)
    {
        const ActorHandle handle = world.CreateActor();
        Actor *const actor = world.FindActor(handle);
        CameraComponent *const camera =
            actor != nullptr ? actor->AddComponent<CameraComponent>() : nullptr;
        if (camera == nullptr || !actor->SetRootComponent(camera))
        {
            (void)world.DestroyActor(handle);
            return {};
        }

        camera->SetLocalTransform(desc.transform);
        camera->SetFieldOfView(desc.field_of_view_degrees);
        camera->SetNearPlane(desc.near_plane);
        camera->SetFarPlane(desc.far_plane);
        camera->SetOrthographicHeight(desc.orthographic_height);
        camera->SetProjectionMode(desc.projection_mode);
        camera->SetCameraEnabled(desc.enabled);
        camera->SetPriority(desc.priority);

        if (!world.InitializeActor(handle) || !world.ActivateActor(handle))
        {
            (void)world.DestroyActor(handle);
            return {};
        }
        return handle;
    }
}
