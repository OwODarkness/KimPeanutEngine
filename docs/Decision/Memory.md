

# MEMORY MANAGEMENT DESIGN DECISIONS

## Overview
Our graphics engine uses a hybrid memory management strategy balancing safety 
with performance. The system handles ~100MB of GPU memory and ~500MB of CPU memory
during typical gameplay.

## Decision 1: Smart vs Raw Pointers

### Smart Pointers (Ownership)
```cpp
/**
 * USED FOR: Resource ownership and automatic lifecycle management
 * 
 * Examples:
 * - Textures loaded from disk
 * - Mesh data (vertices, indices)
 * - Shader programs
 * - Game object instances
 * 
 * Why: Prevents memory leaks, simplifies exception safety
 */
std::unique_ptr<Texture> LoadTexture(const std::string& path);
std::shared_ptr<GameObject> CreateGameObject();


```



### Raw Pointers

```c++

/**

 * USED FOR: Non-owning references and performance-critical paths
 * 
 * Examples:
 * - Render system passing textures to GPU (already owned elsewhere)
 * - Physics system accessing transform data
 * - Internal renderer data structures for cache locality
 * 
 * Why: Zero-overhead, better cache performance, avoids circular references
   */
   class Renderer {
   public:
   // Raw pointer = we don't own this, just use it
   void SubmitMesh(const Mesh* mesh, const Material* material);

private:
    // But we own our internal arrays
    std::vector<std::unique_ptr<RenderBatch>> batches_;
};
```



## Vulkan Memory Management

### 1. POOL ALLOCATOR
**For:** Fixed-size, reusable allocations  
**Use when:** Small frequent allocations (uniform buffers, descriptors)  
**Pros:** Fast (O1), no fragmentation, memory reuse  
**Cons:** Fixed slot sizes only  
**Size:** ≤ 4KB recommended  

### 2. LINEAR ALLOCATOR  
**For:** Temporary, frame-scoped data  
**Use when:** Command buffers, single-use staging, per-frame data  
**Pros:** Very fast (pointer increment), zero overhead  
**Cons:** Must reset entire allocator, no individual free  
**Reset:** Every frame  

### 3. DEDICATED ALLOCATOR
**For:** Large, exclusive resources  
**Use when:** Textures >4MB, vertex buffers, render targets  
**Pros:** Best performance, guaranteed memory  
**Cons:** Memory overhead, slower allocation  
**Size:** ≥ 4MB recommended  

---