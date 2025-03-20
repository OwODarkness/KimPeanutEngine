#include <memory> 

#include "editor/include/editor.h"
#include "runtime/engine.h"


#include "runtime/core/math/math_header.h"

#include<iostream>

void print(const kpengine::Matrix3f& mat)
{
    for(int i = 0;i<3;i++)
    {
        for(int j = 0;j<3;j++)
        {
            std::cout << mat[i][j] << " ";
        }
        std::cout << "\n";
    }
}

void print(const kpengine::Matrix4f& mat)
{
    for(int i = 0;i<4;i++)
    {
        for(int j = 0;j<4;j++)
        {
            std::cout << mat[i][j] << " ";
        }
        std::cout << "\n";
    }
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

    kpengine::Matrix3f mat1 = {2, 1, 4, 1, 4, 3, 4, 3, 2};
    kpengine::Matrix4f mat{
        2, 1, 1, 0,
        1, 3, 2, 1,
        1, 2, 3, 1,
        0, 1, 1, 2
    };
    std::cout << mat1.Determinant() << std::endl;
    print(mat.Inverse());
    return 0;
}
