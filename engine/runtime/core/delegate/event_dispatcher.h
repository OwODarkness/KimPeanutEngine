#ifndef KPENGINE_RUNTIME_DELEGATE_EVENT_DISPATCHER_H
#define KPENGINE_RUNTIME_DELEGATE_EVENT_DISPATCHER_H

#include <functional>
#include <vector>
#include <iostream>
namespace kpengine{
    template<typename Event>
    class EventDispatcher{

    public:
        void Bind(std::function<void(const Event&)> callback)
        {
            callbacks.push_back(callback);
        }

        void Dispatch(const Event& event)
        {
            for(const auto& callback : callbacks)
            {
                callback(event);
            }
        }

    private:
        std::vector<std::function<void(const Event&)>> callbacks;
    };

}

#endif