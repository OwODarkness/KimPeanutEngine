#include <memory> 
#include "editor/include/editor.h"
#include "runtime/engine.h"

#include <iostream>
#include "runtime/core/math/math_header.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

void print(kpengine::Vector3f vec)
{
    std::cout << vec[0] << " " << vec[1] << " " << vec[2] << "\n"; 
}

void print(glm::vec3 vec)
{
    std::cout << vec[0] << " " << vec[1] << " " << vec[2] << "\n"; 
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


    return 0;
}
