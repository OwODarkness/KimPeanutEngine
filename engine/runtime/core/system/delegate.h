#ifndef KPENGINE_RUNTIME_DELEGATE_H
#define KPENGINE_RUNTIME_DELEGATE_H

// No parameters
#define DECLARE_DELEGATE(DelegateName) typedef DelegateRegister<void> DelegateName;

// With parameters
#define DECLARE_DELEGATE_OneParam(DelegateName, T1) typedef DelegateRegister<void, T1> DelegateName;
#define DECLARE_DELEGATE_TwoParams(DelegateName, T1, T2) typedef DelegateRegister<void, T1, T2> DelegateName;
#define DECLARE_DELEGATE_ThreeParams(DelegateName, T1, T2, T3) typedef DelegateRegister<void, T1, T2, T3> DelegateName;
#define DECLARE_DELEGATE_FourParams(DelegateName, T1, T2, T3, T4) typedef DelegateRegister<void, T1, T2, T3, T4> DelegateName;
#define DECLARE_DELEGATE_FiveParams(DelegateName, T1, T2, T3, T4, T5) typedef DelegateRegister<void, T1, T2, T3, T4, T5> DelegateName;
#define DECLARE_DELEGATE_SixParams(DelegateName, T1, T2, T3, T4, T5, T6) typedef DelegateRegister<void, T1, T2, T3, T4, T5, T6> DelegateName;
#define DECLARE_DELEGATE_SevenParams(DelegateName, T1, T2, T3, T4, T5, T6, T7) typedef DelegateRegister<void, T1, T2, T3, T4, T5, T6, T7> DelegateName;
#define DECLARE_DELEGATE_EightParams(DelegateName, T1, T2, T3, T4, T5, T6, T7, T8) typedef DelegateRegister<void, T1, T2, T3, T4, T5, T6, T7, T8> DelegateName;
#define DECLARE_DELEGATE_NineParams(DelegateName, T1, T2, T3, T4, T5, T6, T7, T8, T9) typedef DelegateRegister<void, T1, T2, T3, T4, T5, T6, T7, T8, T9> DelegateName;

// With return value
#define DECLARE_DELEGATE_RetVal(ReturnType, DelegateName) typedef DelegateRegister<ReturnType> DelegateName;
#define DECLARE_DELEGATE_OneParam_RetVal(ReturnType, DelegateName, T1) typedef DelegateRegister<ReturnType, T1> DelegateName;
#define DECLARE_DELEGATE_TwoParams_RetVal(ReturnType, DelegateName, T1, T2) typedef DelegateRegister<ReturnType, T1, T2> DelegateName;
#define DECLARE_DELEGATE_ThreeParams_RetVal(ReturnType, DelegateName, T1, T2, T3) typedef DelegateRegister<ReturnType, T1, T2, T3> DelegateName;
#define DECLARE_DELEGATE_FourParams_RetVal(ReturnType, DelegateName, T1, T2, T3, T4) typedef DelegateRegister<ReturnType, T1, T2, T3, T4> DelegateName;
#define DECLARE_DELEGATE_FiveParams_RetVal(ReturnType, DelegateName, T1, T2, T3, T4, T5) typedef DelegateRegister<ReturnType, T1, T2, T3, T4, T5> DelegateName;
#define DECLARE_DELEGATE_SixParams_RetVal(ReturnType, DelegateName, T1, T2, T3, T4, T5, T6) typedef DelegateRegister<ReturnType, T1, T2, T3, T4, T5, T6> DelegateName;
#define DECLARE_DELEGATE_SevenParams_RetVal(ReturnType, DelegateName, T1, T2, T3, T4, T5, T6, T7) typedef DelegateRegister<ReturnType, T1, T2, T3, T4, T5, T6, T7> DelegateName;
#define DECLARE_DELEGATE_EightParams_RetVal(ReturnType, DelegateName, T1, T2, T3, T4, T5, T6, T7, T8) typedef DelegateRegister<ReturnType, T1, T2, T3, T4, T5, T6, T7, T8> DelegateName;
#define DECLARE_DELEGATE_NineParams_RetVal(ReturnType, DelegateName, T1, T2, T3, T4, T5, T6, T7, T8, T9) typedef DelegateRegister<ReturnType, T1, T2, T3, T4, T5, T6, T7, T8, T9> DelegateName;

#include <utility>
#include <cassert>

namespace kpenigne{
    template<typename ReturnType, typename...VARS>
class DelegateRegister{
public:
    typedef void* InstancePtr;
    typedef ReturnType(*FuncPtr)(InstancePtr, VARS...);
    typedef std::pair<InstancePtr, FuncPtr> Wrappe r;

    DelegateRegister():m_wrapper({nullptr, nullptr}){}

    template<ReturnType (*Function)(VARS...)>
    static ReturnType InternalFunction(InstancePtr, VARS... val)
    {
        return (Function)(val...);
    } 

    template<class Class, ReturnType (Class::*Function)(VARS...)>
    static ReturnType InternalClassFunction(InstancePtr ptr, VARS... val)
    {
        return (static_cast<Class*>(ptr)->*Function)(val...);
    }

    template<ReturnType (*Function)(VARS...)>
    void Bind()
    {
        m_wrapper.first = nullptr;
        m_wrapper.second = &InternalFunction<Function>;
    }   

    template<class Class, ReturnType (Class::*Function)(VARS...)>
    void Bind(InstancePtr ptr)
    {
        m_wrapper.first = ptr;
        m_wrapper.second = &InternalClassFunction<Class, Function>;
    }

    ReturnType Execute(VARS...val)
    {
        assert(m_wrapper.second != nullptr);   
        if(m_wrapper.first == nullptr)
        {
            return (m_wrapper.second)(nullptr, val...);
        }
        else
        {
            return (m_wrapper.second)(m_wrapper.first, val...);
        }
    }

    ReturnType ExecuteIfBound(VARS...val)
    {
        if(m_wrapper.second == nullptr)
        {
            return {};
        }
        if(m_wrapper.first == nullptr)
        {
            return (m_wrapper.second)(nullptr, val...);
        }
        else
        {
            return (m_wrapper.second)(m_wrapper.first, val...);
        }
    }

    void UnBind()
    {
        m_wrapper.first = nullptr;
        m_wrapper.second = nullptr;
    }

private:
    Wrapper m_wrapper;
};
}
#endif// KPENGINE_RUNTIME_DELEGATE_H