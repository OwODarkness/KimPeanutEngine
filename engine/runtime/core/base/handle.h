#ifndef KPENGINE_RUNTIME_HANDLE_H
#define KPENGINE_RUNTIME_HANDLE_H

#include <cstdint>
#include <vector>

#define KPENGINE_NULL_HANDLE UINT32_MAX
namespace kpengine{

    /**
     *  define a empty struct like struct Texture{}; 
     *  now, Texture is a Tag, using TextureHandle = Handle<Tag>
     **/
     
    template<typename Tag>
    struct Handle{  
        uint32_t id = KPENGINE_NULL_HANDLE;
        uint32_t generation = 0;
    
        bool IsValid() const{return id != KPENGINE_NULL_HANDLE;}

        bool operator==(const Handle& rhs) const
        {
            return id == rhs.id && generation == rhs.generation;
        }
    };

    /**
     * T is the struct or type of resource you want to use handle system to manager
     * Like struct TextureSource can be pass as T
    **/
    template<typename HandleT>
    class HandleSystem{ 
    public:
        HandleSystem(): generations_({}), free_slots_({}){}
        HandleT Create()
        {
            uint32_t id = KPENGINE_NULL_HANDLE;
            if(!free_slots_.empty())
            {
                id = free_slots_.back();
                free_slots_.pop_back();
                generations_[id]++;
            }
            else
            {
                id = static_cast<uint32_t>(generations_.size());
                generations_.push_back(0);
            }

            return {id, generations_[id]};
        }

        uint32_t Get(const HandleT& handle)
        {
            if(IsHandleValid(handle))
            {
                return handle.id;
            }
            return KPENGINE_NULL_HANDLE;
        }

        bool Destroy(const HandleT& handle)
        {
            if(IsHandleValid(handle))
            {
                free_slots_.push_back(handle.id);
                return true;
            }
            return false;
        }

    private:
        bool IsHandleValid(const HandleT& handle)
        {
            return handle.IsValid() && handle.generation == generations_[handle.id];
        }

    private:
        std::vector<uint32_t> generations_;
        std::vector<uint32_t> free_slots_;
    };

};

#endif