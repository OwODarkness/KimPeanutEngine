#ifndef KPENGINE_RUNTIME_ASYNC_ASYNC_QUEUE_H
#define KPENGINE_RUNTIME_ASYNC_ASYNC_QUEUE_H

#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace kpengine::async
{
    // A small mutex-guarded FIFO. Two instances compose the design's two SPSC
    // pipes (incoming: render thread -> loading thread; ready: loading thread ->
    // render thread). It is also MPSC-safe, so multiple producers can come later
    // without changing this type. Deliberately generic: it carries whatever the
    // caller puts in it, so core/async never needs to know about asset types.
    template <typename T>
    class AsyncQueue
    {
    public:
        void Push(T item)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            items_.push_back(std::move(item));
        }

        // Returns false if empty; never blocks.
        bool TryPop(T& out)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (items_.empty())
            {
                return false;
            }
            out = std::move(items_.front());
            items_.pop_front();
            return true;
        }

        std::size_t Size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return items_.size();
        }

        bool Empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return items_.empty();
        }

    private:
        mutable std::mutex mutex_;
        std::deque<T> items_;
    };
}

#endif
