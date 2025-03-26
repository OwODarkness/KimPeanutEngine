#include <memory> 
#include "editor/include/editor.h"
#include "runtime/engine.h"

#include <iostream>
#include "runtime/core/math/math_header.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>


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
            std::cout << mat[i][j] << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}


int main(int argc, char** argv)
{
    // using Engine = kpengine::runtime::Engine;
    // using Editor = kpengine::editor::Editor;

    // std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    // engine->Initialize();

    // std::unique_ptr<Editor> editor = std::make_unique<Editor>();
    // editor->Initialize(engine.get());
    
    // editor->Run();
    // editor->Clear();

    kpengine::Rotator3f rotator;
    rotator.roll_ = 30.f;
    kpengine::Matrix4f mat = kpengine::Matrix4f::MakeRotationMatrix(rotator);
    print(mat);
    glm::mat4 transform = glm::mat4(1);
    transform = glm::rotate(transform, glm::radians(30.f), glm::vec3(0.f, 0.f, 1.f));
    print(transform);

    
    return 0;
}
