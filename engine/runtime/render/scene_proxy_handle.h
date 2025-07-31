#ifndef KPENGINE_RUNTIME_SCENE_PROXY_HANDLE_H
#define KPENGINE_RUNTIME_SCENE_PROXY_HANDLE_H

#include <climits>

struct SceneProxyHandle{
    int id;
    int generation;

    SceneProxyHandle():id(0), generation(0){}
    SceneProxyHandle(int _id, int _generation):id(_id), generation(_generation){}
    inline bool IsValid() const {return id != -1;}
    static SceneProxyHandle InValid(){return {-1, 0};}
};

#endif