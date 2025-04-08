#ifndef KPENGINE_RUNTIME_SCENE_PROXY_HANDLE_H
#define KPENGINE_RUNTIME_SCENE_PROXY_HANDLE_H

#include <climits>

struct SceneProxyHandle{
    unsigned int id;
    unsigned int generation;
    SceneProxyHandle():id(0), generation(0){}
    SceneProxyHandle(unsigned int _id, unsigned int _generation):id(_id), generation(_generation){}
    inline bool IsValid() const {return id != UINT32_MAX;}
    static SceneProxyHandle InValid(){return {UINT32_MAX, 0};}
};

#endif