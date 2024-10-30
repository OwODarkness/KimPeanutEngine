#ifndef RUNTIME_FRAME_BUFFER_H
#define RUNTIME_FRAME_BUFFER_H

namespace kpengine
{

    class FrameBuffer
    {
    public:
        FrameBuffer(int width, int height);
        ~FrameBuffer();
        void Initialize();
        void ReSizeFrameBuffer(int width, int height);
        void BindFrameBuffer();
        void UnBindFrameBuffer();
        unsigned int GetTexture() const{return texture_;}
    private:
        unsigned int fbo_;//frame buffer object
        unsigned int rbo_;
        unsigned int texture_;
    public:
        int width_;
        int height_;

        
    };
};
#endif