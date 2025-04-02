# KimPeanut Engine 
金花生游戏引擎

## 架构
KimPeanut Engine(KP Engine) 采用分层体系架构。

## 第三方说明

图形API：OpenGL

着色器语言：GLSL

UI框架：imgui

3D模型读取工具：assimp

图像读取工具：stb_image

## 引擎线程

引擎以Game Thread 为主线程，以Render Thread为副线程

## 模块说明

游戏引擎包括编辑器(Editor)和内核(Engine)

- runtime_global_context是Engine的全局上下文，用于创建全局系统对象，包括窗口系统、渲染系统、资产系统
- editor_global_context是Editor的全局上下文，用于引用runtime的信息，同时创建editor相关系统和input系统

### 委托

delegate

单薄委托

### 渲染

- render system：
- shader_manager：着色器缓存
- render_camera：摄像机
- model_loader：3D模型载入工具
- renderscene：渲染场景，将场景元素渲染成帧缓存
- mesh_resource：mesh_resource与mesh 的LOD Level成1:1关系，mesh_resource存储顶点信息和索引信息
- render_mesh：存储mesh_resource数组，根据mesh信息初始化并生成实际vertex buffer object和vertex array object
- primitive_scene_proxy：图元渲染代理，向渲染场景传递渲染信息，抽象类
- mesh_scene_proxy：网格渲染代理
- skybox：天空盒



### 数学库

engine/runtime/core/math

包括vector、matrix等

## 参考

**开源游戏引擎**

Unreal Engine

Piccolo Engine

