#include <memory>
#include <iostream>
#include "editor/include/editor.h"
#include "runtime/engine.h"

#include "runtime/core/math/math_header.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

void Debug(const glm::mat4 &mat)
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            std::cout << mat[i][j] << " ";
        }
        std::cout << std::endl;
    }
}
void Debug(const kpengine::Matrix4f &mat)
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            std::cout << mat[i][j] << " ";
        }
        std::cout << std::endl;
    }
}
int main(int argc, char **argv)
{

    using Engine = kpengine::runtime::Engine;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();

    engine->Initialize();
    engine->Run();
    engine->Clear();

   glm::vec3 pos = {0.f, 2.f, 0.f};
   glm::vec3 direction ={0.2f, -1.0f, 0.0f};
   float fov = 2.f *  glm::radians(17.5f);
   float aspect = 1.f;
   float near_plane = 0.1f;
   float far_plane = 25.f;
   glm::mat4 proj = glm::perspective(fov, aspect, near_plane, far_plane);
   glm::mat4 view = glm::lookAt(pos, pos + direction, {0.f, 1.f, 0.f});

   using namespace kpengine;
   Vector3f pos1 = {0.f, 2.f, 0.f};
   Vector3f direction1 ={0.2f, -1.0f, 0.f};
   Matrix4f view1 = Matrix4f::MakeCameraMatrix(pos1, direction1, {0.f, 1.f, 0.f});
    Matrix4f proj1 = Matrix4f::MakePerProjMatrix(2 * math::DegreeToRadian(17.5f), aspect, near_plane, far_plane);
   Debug(proj * view);
   std::cout << std::endl;
   Debug((proj1 * view1) .Transpose());

    return 0;
}
