#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_BINDLESS_TEXTURE_TABLE_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_BINDLESS_TEXTURE_TABLE_H

#include <cstdint>
#include <vector>

#include <glad/glad.h>

#include "common/api.h"
#include "common/bindless_texture.h"

namespace kpengine::graphics
{
    class SamplerManager;
    class TextureManager;

    // Private GL_ARB_bindless_texture implementation of the common sampled
    // texture table. The shader reads the resident 64-bit handles from an SSBO
    // at the logical V1 table binding; no GLuint64 reaches Render.
    class OpenglBindlessTextureTable final
    {
    public:
        bool Initialize();
        void Destroy();

        BindlessTextureHandle Acquire(TextureHandle texture, SamplerHandle sampler,
                                      TextureManager &textures, SamplerManager &samplers);
        bool Release(BindlessTextureHandle handle, BindlessSubmissionSerial retire_after);
        void BeginFrame(TextureManager &textures, SamplerManager &samplers);
        void EndFrame();
        void WaitIdle();
        void Bind() const;

        bool ReferencesTexture(TextureHandle handle) const;
        bool ReferencesSampler(SamplerHandle handle) const;
        bool IsReady() const { return table_buffer_ != 0; }
        uint32_t GetCapacity() const { return capacity_; }
        BindlessSubmissionSerial GetPendingSubmissionSerial() const { return next_submission_serial_; }
        BindlessSubmissionSerial GetLastSubmittedSerial() const { return last_submitted_serial_; }

    private:
        using GetTextureSamplerHandleProc = GLuint64(APIENTRYP)(GLuint texture, GLuint sampler);
        using MakeTextureHandleResidentProc = void(APIENTRYP)(GLuint64 handle);
        using MakeTextureHandleNonResidentProc = void(APIENTRYP)(GLuint64 handle);

        struct Entry
        {
            TextureHandle texture{};
            SamplerHandle sampler{};
            GLuint64 native_handle = 0;
            uint32_t revision = 0;
            bool live = false;
        };
        struct RetiredReference
        {
            TextureHandle texture{};
            SamplerHandle sampler{};
            GLuint64 native_handle = 0;
            BindlessSubmissionSerial retire_after = 0;
        };

        bool HasRequiredExtensions() const;
        void CollectCompleted(BindlessSubmissionSerial completed_submission);
        void ApplyPendingWrites(TextureManager &textures, SamplerManager &samplers);

        BindlessTextureSlotAllocator allocator_{0};
        std::vector<Entry> entries_;
        std::vector<RetiredReference> retired_references_;
        std::vector<uint32_t> applied_revisions_;
        GetTextureSamplerHandleProc get_texture_sampler_handle_ = nullptr;
        MakeTextureHandleResidentProc make_resident_ = nullptr;
        MakeTextureHandleNonResidentProc make_non_resident_ = nullptr;
        GLuint table_buffer_ = 0;
        GLsync in_flight_sync_ = nullptr;
        uint32_t capacity_ = 0;
        BindlessSubmissionSerial next_submission_serial_ = 1;
        BindlessSubmissionSerial last_submitted_serial_ = 0;
    };
}

#endif
