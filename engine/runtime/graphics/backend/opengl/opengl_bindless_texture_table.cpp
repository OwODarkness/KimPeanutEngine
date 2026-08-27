#include "opengl_bindless_texture_table.h"

#include <algorithm>
#include <string_view>

#include <GLFW/glfw3.h>

#include "common/sampler.h"
#include "common/sampler_manager.h"
#include "common/texture.h"
#include "common/texture_manager.h"
#include "opengl_sampler.h"
#include "opengl_texture.h"

namespace kpengine::graphics
{
    bool OpenglBindlessTextureTable::Initialize()
    {
        if (!HasRequiredExtensions())
        {
            return false;
        }
        get_texture_sampler_handle_ = reinterpret_cast<GetTextureSamplerHandleProc>(
            glfwGetProcAddress("glGetTextureSamplerHandleARB"));
        make_resident_ = reinterpret_cast<MakeTextureHandleResidentProc>(
            glfwGetProcAddress("glMakeTextureHandleResidentARB"));
        make_non_resident_ = reinterpret_cast<MakeTextureHandleNonResidentProc>(
            glfwGetProcAddress("glMakeTextureHandleNonResidentARB"));
        if (!get_texture_sampler_handle_ || !make_resident_ || !make_non_resident_)
        {
            return false;
        }

        GLint max_storage_block_size = 0;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &max_storage_block_size);
        if (max_storage_block_size < static_cast<GLint>(sizeof(GLuint64)))
        {
            return false;
        }
        capacity_ = std::min<uint32_t>(BindlessTextureTableLayout::max_capacity,
                                       static_cast<uint32_t>(max_storage_block_size /
                                                             sizeof(GLuint64)));
        if (!IsBindlessTextureTableCapacityValid(capacity_))
        {
            capacity_ = 0;
            return false;
        }

        glCreateBuffers(1, &table_buffer_);
        glNamedBufferData(table_buffer_, static_cast<GLsizeiptr>(capacity_ * sizeof(GLuint64)),
                          nullptr, GL_DYNAMIC_DRAW);
        allocator_ = BindlessTextureSlotAllocator{capacity_};
        entries_.resize(capacity_);
        applied_revisions_.assign(capacity_, 0);
        return true;
    }

    void OpenglBindlessTextureTable::Destroy()
    {
        WaitIdle();
        for (const Entry &entry : entries_)
        {
            if (entry.native_handle != 0)
            {
                make_non_resident_(entry.native_handle);
            }
        }
        for (const RetiredReference &entry : retired_references_)
        {
            if (entry.native_handle != 0)
            {
                make_non_resident_(entry.native_handle);
            }
        }
        if (table_buffer_ != 0)
        {
            glDeleteBuffers(1, &table_buffer_);
            table_buffer_ = 0;
        }
        entries_.clear();
        retired_references_.clear();
        applied_revisions_.clear();
        allocator_ = BindlessTextureSlotAllocator{0};
        capacity_ = 0;
        get_texture_sampler_handle_ = nullptr;
        make_resident_ = nullptr;
        make_non_resident_ = nullptr;
    }

    BindlessTextureHandle OpenglBindlessTextureTable::Acquire(
        TextureHandle texture, SamplerHandle sampler, TextureManager &textures,
        SamplerManager &samplers)
    {
        if (!IsReady() || !textures.GetTexture(texture) || !samplers.GetSampler(sampler))
        {
            return {};
        }
        const BindlessTextureHandle handle = allocator_.Allocate();
        if (!handle.IsValid())
        {
            return {};
        }
        Entry &entry = entries_[handle.id];
        entry.texture = texture;
        entry.sampler = sampler;
        entry.native_handle = 0;
        entry.live = true;
        ++entry.revision;
        if (entry.revision == 0)
        {
            ++entry.revision;
        }
        return handle;
    }

    bool OpenglBindlessTextureTable::Release(BindlessTextureHandle handle,
                                             BindlessSubmissionSerial retire_after)
    {
        if (!allocator_.IsAllocated(handle))
        {
            return false;
        }
        Entry &entry = entries_[handle.id];
        retired_references_.push_back(
            {entry.texture, entry.sampler, entry.native_handle, retire_after});
        entry.live = false;
        entry.native_handle = 0;
        return allocator_.Release(handle, retire_after);
    }

    void OpenglBindlessTextureTable::BeginFrame(TextureManager &textures, SamplerManager &samplers)
    {
        if (in_flight_sync_ != nullptr)
        {
            const GLenum result = glClientWaitSync(in_flight_sync_, GL_SYNC_FLUSH_COMMANDS_BIT,
                                                   GL_TIMEOUT_IGNORED);
            if (result == GL_WAIT_FAILED)
            {
                return;
            }
            glDeleteSync(in_flight_sync_);
            in_flight_sync_ = nullptr;
            CollectCompleted(last_submitted_serial_);
        }
        ApplyPendingWrites(textures, samplers);
    }

    void OpenglBindlessTextureTable::EndFrame()
    {
        if (!IsReady())
        {
            return;
        }
        in_flight_sync_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        last_submitted_serial_ = next_submission_serial_;
        ++next_submission_serial_;
    }

    void OpenglBindlessTextureTable::WaitIdle()
    {
        if (in_flight_sync_ != nullptr)
        {
            glClientWaitSync(in_flight_sync_, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
            glDeleteSync(in_flight_sync_);
            in_flight_sync_ = nullptr;
        }
        if (last_submitted_serial_ != 0)
        {
            CollectCompleted(last_submitted_serial_);
        }
    }

    void OpenglBindlessTextureTable::Bind() const
    {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                         BindlessTextureTableLayout::descriptor_binding, table_buffer_);
    }

    bool OpenglBindlessTextureTable::ReferencesTexture(TextureHandle handle) const
    {
        return std::any_of(entries_.begin(), entries_.end(), [handle](const Entry &entry) {
                   return entry.live && entry.texture == handle;
               }) ||
               std::any_of(retired_references_.begin(), retired_references_.end(), [handle](const RetiredReference &entry) {
                   return entry.texture == handle;
               });
    }

    bool OpenglBindlessTextureTable::ReferencesSampler(SamplerHandle handle) const
    {
        return std::any_of(entries_.begin(), entries_.end(), [handle](const Entry &entry) {
                   return entry.live && entry.sampler == handle;
               }) ||
               std::any_of(retired_references_.begin(), retired_references_.end(), [handle](const RetiredReference &entry) {
                   return entry.sampler == handle;
               });
    }

    bool OpenglBindlessTextureTable::HasRequiredExtensions() const
    {
        GLint extension_count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
        bool has_bindless_texture = false;
        bool has_gpu_shader_int64 = false;
        for (GLint index = 0; index < extension_count; ++index)
        {
            const char *extension = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, index));
            if (!extension)
            {
                continue;
            }
            has_bindless_texture = has_bindless_texture ||
                                   std::string_view(extension) == "GL_ARB_bindless_texture";
            has_gpu_shader_int64 = has_gpu_shader_int64 ||
                                  std::string_view(extension) == "GL_ARB_gpu_shader_int64";
        }
        return has_bindless_texture && has_gpu_shader_int64;
    }

    void OpenglBindlessTextureTable::CollectCompleted(BindlessSubmissionSerial completed_submission)
    {
        allocator_.CollectCompleted(completed_submission);
        const auto retained = std::remove_if(retired_references_.begin(), retired_references_.end(),
                                             [this, completed_submission](const RetiredReference &entry) {
                                                 if (entry.retire_after > completed_submission)
                                                 {
                                                     return false;
                                                 }
                                                 if (entry.native_handle != 0)
                                                 {
                                                     make_non_resident_(entry.native_handle);
                                                 }
                                                 return true;
                                             });
        retired_references_.erase(retained, retired_references_.end());
    }

    void OpenglBindlessTextureTable::ApplyPendingWrites(TextureManager &textures,
                                                         SamplerManager &samplers)
    {
        for (uint32_t index = 0; index < entries_.size(); ++index)
        {
            Entry &entry = entries_[index];
            if (!entry.live || applied_revisions_[index] == entry.revision)
            {
                continue;
            }
            Texture *texture = textures.GetTexture(entry.texture);
            Sampler *sampler = samplers.GetSampler(entry.sampler);
            if (!texture || !sampler)
            {
                continue;
            }
            const OpenglTextureResource texture_resource =
                ConvertToOpenglTextureResource(texture->GetTextueHandle());
            const OpenglSamplerResource sampler_resource =
                ConvertToOpenglSamplerResource(sampler->GetSampleHandle());
            entry.native_handle = get_texture_sampler_handle_(texture_resource.image,
                                                               sampler_resource.sampler);
            make_resident_(entry.native_handle);
            glNamedBufferSubData(table_buffer_, static_cast<GLintptr>(index * sizeof(GLuint64)),
                                 sizeof(GLuint64), &entry.native_handle);
            applied_revisions_[index] = entry.revision;
        }
    }
}
