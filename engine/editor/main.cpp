#include <memory> 
#include "editor/include/editor.h"
#include "runtime/engine.h"

#include <iostream>
#include "runtime/core/math/math_header.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

void print(kpengine::Vector4f v)
{
    for(int i = 0;i<4;i++)
    {
        std::cout << v[i] << " ";
    }
    std::cout << std::endl;
}
void print(kpengine::Matrix4f mat)
{
    for(int i = 0;i<4;i++)
    {
        for(int j = 0;j<4;j++)
        {
            std::cout << mat[i][j] << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}
void print(glm::mat4 mat)
{
    for(int i = 0;i<4;i++)
    {
        for(int j = 0;j<4;j++)
        {
            std::cout << mat[j][i] << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}


int main(int argc, char** argv)
{
    using Engine = kpengine::runtime::Engine;
    using Editor = kpengine::editor::Editor;

    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    engine->Initialize();

    std::unique_ptr<Editor> editor = std::make_unique<Editor>();
    editor->Initialize(engine.get());
    
    editor->Run();
    editor->Clear();

    // kpengine::Rotator3f rotator;
    // rotator.yaw_ = 30.f;

    // kpengine::Matrix4f mat = kpengine::Matrix4f::MakeRotationMatrix(rotator);
    // print(mat);
    // glm::mat4 transform = glm::mat4(1);

    // transform = glm::rotate(transform, glm::radians(30.f), glm::vec3(0.f, 1.f, 0.f));

    // print(transform);
    
    // kpengine::Vector3f eye{1.f, 2.f, 3.f};
    // kpengine::Vector3f dir{0.3f, 0.4f, 0.5f};
    // kpengine::Vector3f up{0.f, 1.f, 0.f};
    // kpengine::Matrix4f mat = kpengine::Matrix4f::MakeCameraMatrix(eye, dir, up);
    // print(mat);
    // glm::mat4 transform = glm::mat4(1);
    // glm::vec3 eye1{1.f, 2.f, 3.f};
    // glm::vec3 dir1{0.3f, 0.4f, 0.5f};
    // glm::vec3 up1{0.f, 1.f, 0.f};
    // transform = glm::lookAt(eye1, eye1 + dir1, up1);

    // kpengine::Vector4f v{1.f, 2.f, 3.f, 1.f};
    //  kpengine::Matrix4f mat = kpengine::Matrix4f::MakeOrthProjMatrix(-10.0f, 10.0f, -10.0f, 10.0f, 1.f, 100.f);
     // glm::mat4 transform = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.f, 100.f);

    // print(mat);print(transform);

    // kpengine::Matrix4f mat = kpengine::Matrix4f::MakePerProjMatrix(45.f, 3.f/2.f, 1.f, 100.f);
    // glm::mat4 transform = glm::perspective(glm::radians(45.f), 3.f / 2.f, 1.f, 100.f);
    //  print(mat);
    //  print(transform);

    return 0;
}
