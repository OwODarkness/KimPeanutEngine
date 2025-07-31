#ifndef KPENGINE_RUNTIME_FRAME_BUFFER_H
#define KPENGINE_RUNTIME_FRAME_BUFFER_H

#include <string>
#include <unordered_map>
namespace kpengine
{

    class FrameBuffer
    {
    public:
        FrameBuffer(int width, int height);
        ~FrameBuffer();
        void Initialize();
        void Finalize();
        void ReSizeFrameBuffer(int width, int height);
        void BindFrameBuffer();
        void UnBindFrameBuffer();

        void AddColorAttachment(const std::string &name,
                                unsigned int internal_format,
                                unsigned int format,
                                unsigned int type);
        unsigned int GetTexture(size_t index) const;
        unsigned int GetTexture(const std::string& name) const;
        unsigned int GetFBO() const{return fbo_;}
    private:
        unsigned int fbo_; // frame buffer object
        unsigned int rbo_;
        std::vector<unsigned int> color_attachments_;
        std::unordered_map<std::string, size_t> name_to_index_;
        std::vector<unsigned int> draw_buffers_;
    public:
        int width_;
        int height_;
    };
};
#endif