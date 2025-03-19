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
            std::cout << mat.data_[i][j] << " ";
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


    return 0;
}
