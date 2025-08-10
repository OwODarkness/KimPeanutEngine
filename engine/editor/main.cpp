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

    glm::vec3 lightDir =  {-0.2f, -1.0f, -0.3f};
    glm::mat4 lightView = glm::lookAt(-2.f * lightDir , glm::vec3(0.0), glm::vec3(0.0, 1.0, 0.0));
    glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 25.0f);
    glm::mat4 space = proj * lightView;
    using namespace kpengine;
    Vector3f eye_pos = -2.f * Vector3f(-0.2f, -1.0f, -0.3f);
    Vector3f gaze_dir = (Vector3f(0.f, 0.f, 0.f) - eye_pos).GetSafetyNormalize();
    Vector3f up = Vector3f(0.f, 1.f, 0.f);

    Matrix4f proj1 = Matrix4f::MakeOrthProjMatrix(-10.f, 10.f, -10.f, 10.f, 0.1f, 25.f);
    Matrix4f view1 = Matrix4f::MakeCameraMatrix(eye_pos, gaze_dir, up);
    Debug(proj * lightView);

    std::cout << "\n";
    Debug((proj1 * view1).Transpose()); // No transpose if you use consistent matrix order
    return 0;
}
