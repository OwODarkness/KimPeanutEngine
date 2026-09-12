// Standalone Cubism 5 Native R5 conformance probe for the L2D6.0 contract
// freeze.
//
// The probe drives the pinned official Framework motion/expression managers
// directly with a caller-supplied clock and reports observable parameter
// values, queue-entry lifetimes, and authored user events. It is evidence, not
// product code: it is not part of the engine build and adds no Motion
// translation unit to any shipping target.
//
// Usage:
//   Live2DPlaybackProbe [<path-to.moc3>]

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <malloc.h>

#ifdef _WIN32
#include <process.h>
#endif

#include "CubismFramework.hpp"
#include "ICubismAllocator.hpp"
#include "Live2DCubismCore.h"

#include "Motion/CubismExpressionMotion.hpp"
#include "Motion/CubismExpressionMotionManager.hpp"
#include "Motion/CubismMotion.hpp"
#include "Motion/CubismMotionManager.hpp"
#include "Motion/CubismMotionQueueEntry.hpp"
#include "Model/CubismMoc.hpp"
#include "Model/CubismModel.hpp"

#ifndef KPENGINE_LIVE2D_MOC_PATH
#define KPENGINE_LIVE2D_MOC_PATH ""
#endif

using namespace Live2D::Cubism::Framework;

namespace Core = Live2D::Cubism::Core;

namespace
{
    // ---------------------------------------------------------------- support

    class ProbeAllocator final : public ICubismAllocator
    {
    public:
        void* Allocate(const std::size_t size) override
        {
            return std::malloc(size == 0 ? 1 : size);
        }

        void Deallocate(void* memory) override
        {
            std::free(memory);
        }

        void* AllocateAligned(const std::size_t size, const std::uint32_t alignment) override
        {
            const std::size_t safe = alignment < sizeof(void*) ? sizeof(void*) : alignment;
            return _aligned_malloc(size == 0 ? 1 : size, safe);
        }

        void DeallocateAligned(void* memory) override
        {
            _aligned_free(memory);
        }
    };

    void ProbeLog(const char* message)
    {
        if (message != nullptr)
        {
            std::printf("[sdk] %s\n", message);
        }
    }

    ProbeAllocator& Allocator()
    {
        static ProbeAllocator allocator;
        return allocator;
    }

    std::vector<std::string>& EventSink()
    {
        static std::vector<std::string> sink;
        return sink;
    }

    void OnUserEvent(const CubismMotionQueueManager*, const csmString& value, void*)
    {
        EventSink().push_back(value.GetRawString());
    }

    int& FinishedCallbackCount()
    {
        static int count = 0;
        return count;
    }

    void OnMotionFinished(ACubismMotion*)
    {
        ++FinishedCallbackCount();
    }

    std::vector<std::byte> ReadFile(const std::string& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            return {};
        }
        const std::streamsize size = stream.tellg();
        stream.seekg(0, std::ios::beg);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        return bytes;
    }

    std::string Format(const char* format, ...)
    {
        char buffer[4096];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

    // ------------------------------------------------------- synthetic input

    struct EventSpec
    {
        double time = 0.0;
        std::string value;
    };

    // Authors a motion3.json with one constant-valued parameter curve. The
    // constant target makes the SDK's fade weight recoverable as
    // (observed - source) / (target - source).
    //
    // Layout mirrors the pinned R5 reader exactly: only the two *counters* live
    // under "Meta"; the "UserData" array itself is a root-level sibling of
    // "Meta" and "Curves" (CubismMotionJson::GetEventValue reads
    // GetRoot()[UserData][i][Value]). Nesting the array under "Meta" makes every
    // event read report "type mismatch" rather than fail loudly.
    //
    // Every numeric literal is separated by ',' or a newline. The pinned R5
    // Utils::CubismJson numeric reader terminates only on ',' or '\n' and
    // rejects any other trailing character, so "0.0, 1 ]" is a parse error
    // while "0.0,\n1\n]" is not. The official exporter emits one value per
    // line for the same reason.
    std::string BuildMotionJson(const char* parameterId,
                                double duration,
                                double targetValue,
                                double metaFadeIn,
                                double metaFadeOut,
                                bool loop,
                                const std::vector<EventSpec>& events)
    {
        std::size_t totalEventValueSize = 0;
        for (const EventSpec& event : events)
        {
            totalEventValueSize += event.value.size();
        }

        std::string json = "{\n";
        json += "\t\"Version\": 3,\n";
        json += "\t\"Meta\": {\n";
        json += Format("\t\t\"Duration\": %g,\n", duration);
        json += Format("\t\t\"Loop\": %s,\n", loop ? "true" : "false");
        json += "\t\t\"CurveCount\": 1,\n";
        json += "\t\t\"Fps\": 30.0,\n";
        json += "\t\t\"TotalSegmentCount\": 1,\n";
        json += "\t\t\"TotalPointCount\": 2,\n";
        json += Format("\t\t\"FadeInTime\": %g,\n", metaFadeIn);
        json += Format("\t\t\"FadeOutTime\": %g,\n", metaFadeOut);
        json += Format("\t\t\"UserDataCount\": %zu,\n", events.size());
        json += Format("\t\t\"TotalUserDataSize\": %zu\n", totalEventValueSize);
        json += "\t},\n";
        json += "\t\"Curves\": [\n";
        json += "\t\t{\n";
        json += Format("\t\t\t\"Target\": \"Parameter\",\n\t\t\t\"Id\": \"%s\",\n",
                       parameterId);
        json += "\t\t\t\"Segments\": [\n";
        json += "\t\t\t\t0.0,\n";
        json += Format("\t\t\t\t%g,\n", targetValue);
        json += "\t\t\t\t0,\n";
        json += Format("\t\t\t\t%g,\n", duration);
        json += Format("\t\t\t\t%g\n", targetValue);
        json += "\t\t\t]\n";
        json += "\t\t}\n";
        json += "\t]";
        if (!events.empty())
        {
            json += ",\n\t\"UserData\": [\n";
            for (std::size_t i = 0; i < events.size(); ++i)
            {
                json += Format("\t\t{\n\t\t\t\"Time\": %g,\n\t\t\t\"Value\": \"%s\"\n\t\t}%s\n",
                               events[i].time, events[i].value.c_str(),
                               i + 1 == events.size() ? "" : ",");
            }
            json += "\t]";
        }
        json += "\n}\n";
        return json;
    }

    std::string BuildExpressionJson(const char* parameterId,
                                    double value,
                                    const char* blend,
                                    double fadeIn,
                                    double fadeOut)
    {
        std::string json = "{\n";
        json += "\t\"Type\": \"Live2D Expression\",\n";
        json += Format("\t\"FadeInTime\": %g,\n\t\"FadeOutTime\": %g,\n", fadeIn, fadeOut);
        json += "\t\"Parameters\": [\n";
        json += "\t\t{\n";
        json += Format("\t\t\t\"Id\": \"%s\",\n", parameterId);
        json += Format("\t\t\t\"Value\": %g,\n", value);
        json += Format("\t\t\t\"Blend\": \"%s\"\n", blend);
        json += "\t\t}\n";
        json += "\t]\n";
        json += "}\n";
        return json;
    }

    CubismMotion* CreateMotion(const std::string& json)
    {
        CubismMotion* motion = CubismMotion::Create(
            reinterpret_cast<const csmByte*>(json.data()),
            static_cast<csmSizeInt>(json.size()),
            &OnMotionFinished, nullptr, false);
        if (motion == nullptr)
        {
            std::printf("MOTION CREATE FAILED for:\n%s\n", json.c_str());
        }
        return motion;
    }

    // --------------------------------------------------------------- output

    void Section(const char* name)
    {
        std::printf("\n=== %s ===\n", name);
    }

    std::string EntrySummary(CubismMotionQueueManager& manager)
    {
        csmVector<CubismMotionQueueEntry*>* entries = manager.GetCubismMotionQueueEntries();
        std::string summary = Format("entries=%u", entries->GetSize());
        for (std::uint32_t i = 0; i < entries->GetSize(); ++i)
        {
            CubismMotionQueueEntry* entry = entries->At(i);
            if (entry == nullptr)
            {
                continue;
            }
            summary += Format(" [%u weight=%.4f t=%.4f end=%.4f finished=%d fadeOut=%d]",
                              i, entry->GetStateWeight(), entry->GetStateTime(),
                              entry->GetEndTime(), entry->IsFinished() ? 1 : 0,
                              entry->IsTriggeredFadeOut() ? 1 : 0);
        }
        return summary;
    }

    // ------------------------------------------------------------- scenarios

    struct ProbeContext
    {
        CubismModel* model = nullptr;
        csmInt32 parameterIndex = 0;
        std::string parameterId;
        float sourceValue = 0.0f;
        float targetValue = 0.0f;
        float parameterMin = 0.0f;
        float parameterMax = 0.0f;
    };

    ProbeContext g_context;

    float Parameter()
    {
        return g_context.model->GetParameterValue(g_context.parameterIndex);
    }

    // Mirrors the official sample: restore the persistent checkpoint, let the
    // contributor write, then persist so relative contributions do not
    // accumulate into the next frame.
    void ResetCheckpoint()
    {
        g_context.model->LoadParameters();
        g_context.model->SetParameterValue(g_context.parameterIndex, g_context.sourceValue);
    }

    void S1_PriorityReservation()
    {
        Section("S1 priority reservation (CubismMotionManager::ReserveMotion)");
        CubismMotionManager manager;
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 2.0,
                                                 g_context.targetValue, 1.0, 1.0, false, {});
        CubismMotion* motion = CreateMotion(json);

        std::printf("initial current=%d reserve=%d\n",
                    manager.GetCurrentPriority(), manager.GetReservePriority());
        const int requested[] = {1, 1, 0, -1, 2, 1, 3, 3};
        for (const int priority : requested)
        {
            const bool accepted = manager.ReserveMotion(priority);
            std::printf("ReserveMotion(%2d) -> %d (current=%d reserve=%d)\n", priority,
                        accepted ? 1 : 0, manager.GetCurrentPriority(),
                        manager.GetReservePriority());
            if (priority == 2 && accepted)
            {
                manager.StartMotionPriority(motion, false, 2);
                std::printf("  after StartMotionPriority(2): current=%d reserve=%d\n",
                            manager.GetCurrentPriority(), manager.GetReservePriority());
            }
        }
        manager.StopAllMotions();
        ACubismMotion::Delete(motion);
    }

    void S2_StartIgnoresPriority()
    {
        Section("S2 StartMotionPriority does not enforce priority");
        CubismMotionManager manager;
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 4.0,
                                                 g_context.targetValue, 0.0, 0.0, false, {});
        CubismMotion* high = CreateMotion(json);
        CubismMotion* low = CreateMotion(json);

        manager.StartMotionPriority(high, false, 5);
        std::printf("start priority 5: current=%d\n", manager.GetCurrentPriority());
        std::printf("ReserveMotion(4) -> %d\n", manager.ReserveMotion(4) ? 1 : 0);
        manager.StartMotionPriority(low, false, 4);
        std::printf("start priority 4 anyway: current=%d %s\n",
                    manager.GetCurrentPriority(), EntrySummary(manager).c_str());
        manager.StopAllMotions();
        ACubismMotion::Delete(high);
        ACubismMotion::Delete(low);
    }

    void S3_ReplacementCrossFade()
    {
        Section("S3 replacement cross-fade timeline");
        CubismMotionManager manager;
        const std::string aJson = BuildMotionJson(g_context.parameterId.c_str(), 4.0,
                                                  g_context.targetValue, 0.5, 0.5, false, {});
        const std::string bJson = BuildMotionJson(g_context.parameterId.c_str(), 4.0,
                                                  1.0 + g_context.sourceValue, 0.5, 0.5, false, {});
        CubismMotion* a = CreateMotion(aJson);
        CubismMotion* b = CreateMotion(bJson);

        ResetCheckpoint();
        g_context.model->SaveParameters();

        manager.StartMotionPriority(a, false, 1);
        std::printf("t=0.000 start A priority=1 current=%d\n", manager.GetCurrentPriority());

        const float delta = 0.25f;
        for (int step = 1; step <= 8; ++step)
        {
            ResetCheckpoint();
            if (step == 3)
            {
                std::printf("t=%.3f ReserveMotion(2)=%d\n",
                            (step - 1) * delta, manager.ReserveMotion(2) ? 1 : 0);
                manager.StartMotionPriority(b, false, 2);
                std::printf("t=%.3f start B priority=2 current=%d reserve=%d\n",
                            (step - 1) * delta, manager.GetCurrentPriority(),
                            manager.GetReservePriority());
            }
            manager.UpdateMotion(g_context.model, delta);
            g_context.model->SaveParameters();
            std::printf("t=%.3f param=%.6f %s\n", step * delta, Parameter(),
                        EntrySummary(manager).c_str());
        }

        manager.StopAllMotions();
        ACubismMotion::Delete(a);
        ACubismMotion::Delete(b);
    }

    void S4_NaturalCompletion()
    {
        Section("S4 natural completion (non-looping)");
        CubismMotionManager manager;
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 1.0,
                                                 g_context.targetValue, 0.5, 0.0, false, {});
        CubismMotion* motion = CreateMotion(json);
        std::printf("GetLoop()=%d GetDuration()=%.4f\n",
                    motion->GetLoop() ? 1 : 0, motion->GetDuration());

        FinishedCallbackCount() = 0;
        ResetCheckpoint();
        g_context.model->SaveParameters();
        manager.StartMotionPriority(motion, false, 1);

        const float delta = 0.25f;
        for (int step = 1; step <= 6; ++step)
        {
            ResetCheckpoint();
            const bool updated = manager.UpdateMotion(g_context.model, delta);
            g_context.model->SaveParameters();
            std::printf("t=%.3f updated=%d finishedCallback=%d currentPriority=%d param=%.6f %s\n",
                        step * delta, updated ? 1 : 0, FinishedCallbackCount(),
                        manager.GetCurrentPriority(), Parameter(),
                        EntrySummary(manager).c_str());
        }
        ACubismMotion::Delete(motion);
    }

    void S5_Cancellation()
    {
        Section("S5 cancellation modes");
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 4.0,
                                                 g_context.targetValue, 0.5, 0.5, false, {});

        {
            CubismMotionManager manager;
            CubismMotion* motion = CreateMotion(json);
            FinishedCallbackCount() = 0;
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, 0.25f);
            g_context.model->SaveParameters();
            std::printf("[immediate] before StopAllMotions: %s\n", EntrySummary(manager).c_str());
            manager.StopAllMotions();
            std::printf("[immediate] after  StopAllMotions: %s finishedCallback=%d current=%d\n",
                        EntrySummary(manager).c_str(), FinishedCallbackCount(),
                        manager.GetCurrentPriority());
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, 0.25f);
            std::printf("[immediate] after next update: param=%.6f %s\n", Parameter(),
                        EntrySummary(manager).c_str());
            ACubismMotion::Delete(motion);
        }

        {
            CubismMotionManager manager;
            CubismMotion* motion = CreateMotion(json);
            FinishedCallbackCount() = 0;
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            const float delta = 0.25f;
            for (int step = 1; step <= 6; ++step)
            {
                ResetCheckpoint();
                if (step == 2)
                {
                    CubismMotionQueueEntry* entry =
                        manager.GetCubismMotionQueueEntries()->At(0);
                    entry->SetFadeout(0.4f);
                    std::printf("[faded] t=%.3f request fade-out 0.4s (end time still %.4f)\n",
                                (step - 1) * delta, entry->GetEndTime());
                }
                manager.UpdateMotion(g_context.model, delta);
                g_context.model->SaveParameters();
                std::printf("[faded] t=%.3f finishedCallback=%d param=%.6f %s\n",
                            step * delta, FinishedCallbackCount(), Parameter(),
                            EntrySummary(manager).c_str());
            }
            manager.StopAllMotions();
            ACubismMotion::Delete(motion);
        }
    }

    void S6_UserEvents()
    {
        Section("S6 authored user-event ordering");
        // ev_zero is at exactly 0.0 and ev_past_end lies beyond Duration; the
        // R5 window is (lastCheckTime, currentTime], so both are unreachable.
        const std::vector<EventSpec> events{{0.0, "ev_zero"},
                                            {0.5, "ev_half_a"},
                                            {0.5, "ev_half_b"},
                                            {1.0, "ev_one"},
                                            {2.5, "ev_past_end"}};
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 2.0,
                                                 g_context.targetValue, 0.0, 0.0, false, events);

        {
            CubismMotionManager manager;
            manager.SetEventCallback(&OnUserEvent, nullptr);
            CubismMotion* motion = CreateMotion(json);
            std::printf("no event callback set -> suppressed (see S6b)\n");
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            EventSink().clear();
            const float delta = 0.25f;
            for (int step = 1; step <= 10; ++step)
            {
                ResetCheckpoint();
                EventSink().clear();
                manager.UpdateMotion(g_context.model, delta);
                g_context.model->SaveParameters();
                std::string fired;
                for (const std::string& value : EventSink())
                {
                    fired += value + " ";
                }
                std::printf("[stepped] t=%.3f fired=[%s]\n", step * delta, fired.c_str());
            }
            ACubismMotion::Delete(motion);
        }

        {
            CubismMotionManager manager;
            manager.SetEventCallback(&OnUserEvent, nullptr);
            CubismMotion* motion = CreateMotion(json);
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            EventSink().clear();
            manager.UpdateMotion(g_context.model, 5.0f);
            g_context.model->SaveParameters();
            std::string fired;
            for (const std::string& value : EventSink())
            {
                fired += value + " ";
            }
            std::printf("[large delta 5.0] fired=[%s] %s\n", fired.c_str(),
                        EntrySummary(manager).c_str());
            ACubismMotion::Delete(motion);
        }

        {
            CubismMotionManager manager;
            manager.SetEventCallback(&OnUserEvent, nullptr);
            CubismMotion* motion = CreateMotion(json);
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            EventSink().clear();
            std::printf("[exact boundary] stepping 0.5s twice\n");
            for (int step = 1; step <= 2; ++step)
            {
                ResetCheckpoint();
                EventSink().clear();
                manager.UpdateMotion(g_context.model, 0.5f);
                std::string fired;
                for (const std::string& value : EventSink())
                {
                    fired += value + " ";
                }
                std::printf("[boundary] t=%.3f fired=[%s]\n", step * 0.5, fired.c_str());
            }
            ACubismMotion::Delete(motion);
        }

        {
            // One advance spanning the whole motion. The window is
            // (lastCheckTime, currentTime] in motion-local seconds, so every
            // authored event inside it fires on this single call, in array
            // order -- including ones authored past Duration, because the fired
            // list is collected before the finished flag is set.
            CubismMotionManager manager;
            manager.SetEventCallback(&OnUserEvent, nullptr);
            CubismMotion* motion = CreateMotion(json);
            FinishedCallbackCount() = 0;
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, 0.1f);
            ResetCheckpoint();
            EventSink().clear();
            manager.UpdateMotion(g_context.model, 5.0f);
            std::string fired;
            for (const std::string& value : EventSink())
            {
                fired += value + " ";
            }
            std::printf("[spanning delta] delta=5.0 fired=[%s] finishedCallback=%d %s\n",
                        fired.c_str(), FinishedCallbackCount(), EntrySummary(manager).c_str());
            ACubismMotion::Delete(motion);
        }
    }

    void S7_FadeEasing()
    {
        Section("S7 motion fade weight (model3 override via SetFadeInTime)");
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 2.0,
                                                 g_context.targetValue, 1.0, 1.0, false, {});
        CubismMotionManager manager;
        CubismMotion* motion = CreateMotion(json);
        std::printf("motion3 Meta fadeIn=%.4f fadeOut=%.4f\n",
                    motion->GetFadeInTime(), motion->GetFadeOutTime());
        motion->SetFadeInTime(1.0f);
        motion->SetFadeOutTime(0.5f);
        std::printf("after model3 override fadeIn=%.4f fadeOut=%.4f\n",
                    motion->GetFadeInTime(), motion->GetFadeOutTime());

        ResetCheckpoint();
        g_context.model->SaveParameters();
        manager.StartMotionPriority(motion, false, 1);

        const float delta = 0.25f;
        const float span = g_context.targetValue - g_context.sourceValue;
        for (int step = 1; step <= 10; ++step)
        {
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, delta);
            g_context.model->SaveParameters();
            const float observed = Parameter();
            std::printf("t=%.3f param=%.6f weight=%.6f %s\n", step * delta, observed,
                        span == 0.0f ? 0.0f : (observed - g_context.sourceValue) / span,
                        EntrySummary(manager).c_str());
        }
        ACubismMotion::Delete(motion);
    }

    void S8_Expressions()
    {
        Section("S8 expression contributions and transitions");
        CubismExpressionMotionManager manager;

        const char* blend = std::getenv("KP_PROBE_BLEND");
        const std::string blendName = blend == nullptr ? "Add" : blend;
        const double value = 0.6;
        const std::string json =
            BuildExpressionJson(g_context.parameterId.c_str(), value, blendName.c_str(), 1.0, 1.0);
        CubismExpressionMotion* expression =
            CubismExpressionMotion::Create(reinterpret_cast<const csmByte*>(json.data()),
                                           static_cast<csmSizeInt>(json.size()));
        std::printf("expression blend=%s value=%g fadeIn=%.4f fadeOut=%.4f source=%.4f\n",
                    blendName.c_str(), value, expression->GetFadeInTime(),
                    expression->GetFadeOutTime(), g_context.sourceValue);

        ResetCheckpoint();
        g_context.model->SaveParameters();
        manager.StartMotion(expression, false);

        const float delta = 0.25f;
        for (int step = 1; step <= 8; ++step)
        {
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, delta);
            g_context.model->SaveParameters();
            std::printf("t=%.3f param=%.6f fadeWeight0=%.6f %s\n", step * delta, Parameter(),
                        manager.GetFadeWeight(0), EntrySummary(manager).c_str());
        }

        // Replacement: start the same expression again while it is fully
        // faded in, and watch the previous entry disappear.
        std::printf("-- replacement --\n");
        CubismExpressionMotion* second =
            CubismExpressionMotion::Create(reinterpret_cast<const csmByte*>(json.data()),
                                           static_cast<csmSizeInt>(json.size()));
        manager.StartMotion(second, false);
        for (int step = 9; step <= 14; ++step)
        {
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, delta);
            g_context.model->SaveParameters();
            std::printf("t=%.3f param=%.6f %s\n", step * delta, Parameter(),
                        EntrySummary(manager).c_str());
        }

        // Clear: the official immediate path.
        std::printf("-- clear (StopAllMotions) --\n");
        manager.StopAllMotions();
        std::printf("after clear: %s\n", EntrySummary(manager).c_str());
        ResetCheckpoint();
        manager.UpdateMotion(g_context.model, delta);
        g_context.model->SaveParameters();
        std::printf("param after clear update=%.6f\n", Parameter());

        ACubismMotion::Delete(expression);
        ACubismMotion::Delete(second);
    }

    void S9_ExpressionBlendMath()
    {
        Section("S9 expression blend math at full weight");
        // A non-zero base makes Multiply distinguishable: with an all-zero
        // source, Multiply is indistinguishable from "no contribution".
        const float base = 12.0f;
        std::printf("base checkpoint=%.4f value=0.6 fadeIn=0 (weight 1 immediately)\n", base);

        // Faithful to the official transaction: LoadParameters -> base write ->
        // SaveParameters -> expression. The expression runs *after* the save and
        // is never re-saved, so its relative contribution is recomputed from the
        // persisted base every frame instead of accumulating.
        const auto frame = [&](CubismExpressionMotionManager& manager)
        {
            g_context.model->LoadParameters();
            g_context.model->SetParameterValue(g_context.parameterIndex, base);
            g_context.model->SaveParameters();
            manager.UpdateMotion(g_context.model, 0.25f);
        };

        const char* blends[] = {"Add", "Multiply", "Overwrite"};
        for (const char* blend : blends)
        {
            CubismExpressionMotionManager manager;
            const double value = 0.6;
            const std::string json = BuildExpressionJson(
                g_context.parameterId.c_str(), value, blend, 0.0, 1.0);
            CubismExpressionMotion* expression =
                CubismExpressionMotion::Create(reinterpret_cast<const csmByte*>(json.data()),
                                               static_cast<csmSizeInt>(json.size()));
            g_context.model->LoadParameters();
            g_context.model->SetParameterValue(g_context.parameterIndex, base);
            g_context.model->SaveParameters();
            manager.StartMotion(expression, false);
            frame(manager);
            const float after_one = Parameter();
            frame(manager);
            const float after_two = Parameter();
            std::printf("blend=%-10s base=%.4f value=%.4f frame1=%.6f frame2=%.6f %s\n",
                        blend, base, value, after_one, after_two,
                        after_one == after_two ? "(stable: not accumulating)"
                                               : "(DRIFT: relative layer accumulated)");
            ACubismMotion::Delete(expression);
        }
    }

    void S10_DeltaEdges()
    {
        Section("S10 delta-time edge behavior");
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 1.0,
                                                 g_context.targetValue, 0.5, 0.0, false, {});

        {
            CubismMotionManager manager;
            CubismMotion* motion = CreateMotion(json);
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            for (int step = 1; step <= 3; ++step)
            {
                ResetCheckpoint();
                const bool updated = manager.UpdateMotion(g_context.model, 0.0f);
                std::printf("[zero delta] step=%d updated=%d param=%.6f %s\n", step,
                            updated ? 1 : 0, Parameter(), EntrySummary(manager).c_str());
            }
            ACubismMotion::Delete(motion);
        }

        {
            CubismMotionManager manager;
            CubismMotion* motion = CreateMotion(json);
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            ResetCheckpoint();
            const bool updated = manager.UpdateMotion(g_context.model, 1000.0f);
            std::printf("[large delta] updated=%d param=%.6f %s\n", updated ? 1 : 0,
                        Parameter(), EntrySummary(manager).c_str());
            ACubismMotion::Delete(motion);
        }

        {
            // A large *first* delta is absorbed by SetupMotionQueueEntry, which
            // anchors startTime at that same update. A large *second* delta
            // instead trips the end-time check in ACubismMotion::UpdateParameters,
            // which sets the finished flag without invoking the finish callback.
            CubismMotionManager manager;
            CubismMotion* motion = CreateMotion(json);
            FinishedCallbackCount() = 0;
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, 0.25f);
            std::printf("[large second delta] before: callback=%d %s\n",
                        FinishedCallbackCount(), EntrySummary(manager).c_str());
            ResetCheckpoint();
            const bool updated = manager.UpdateMotion(g_context.model, 1000.0f);
            std::printf("[large second delta] updated=%d callback=%d param=%.6f %s\n",
                        updated ? 1 : 0, FinishedCallbackCount(), Parameter(),
                        EntrySummary(manager).c_str());
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, 0.25f);
            std::printf("[large second delta] next update: param=%.6f %s\n", Parameter(),
                        EntrySummary(manager).c_str());
            ACubismMotion::Delete(motion);
        }

        {
            CubismMotionManager manager;
            CubismMotion* motion = CreateMotion(json);
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, 0.5f);
            std::printf("[negative delta] after +0.5: param=%.6f %s\n", Parameter(),
                        EntrySummary(manager).c_str());
            ResetCheckpoint();
            manager.UpdateMotion(g_context.model, -0.25f);
            std::printf("[negative delta] after -0.25: param=%.6f %s\n", Parameter(),
                        EntrySummary(manager).c_str());
            ACubismMotion::Delete(motion);
        }

        {
            CubismMotionManager manager;
            CubismMotion* motion = CreateMotion(json);
            ResetCheckpoint();
            g_context.model->SaveParameters();
            manager.StartMotionPriority(motion, false, 1);
            ResetCheckpoint();
            const bool updated = manager.UpdateMotion(g_context.model, -1.0f);
            std::printf("[negative first delta] updated=%d param=%.6f %s\n",
                        updated ? 1 : 0, Parameter(), EntrySummary(manager).c_str());
            ACubismMotion::Delete(motion);
        }
    }

    void S11_LoopFlagIgnored()
    {
        Section("S11 CubismMotion::Create ignores authored Loop");
        for (int loop = 0; loop <= 1; ++loop)
        {
            const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 1.5,
                                                     g_context.targetValue, 1.0, 1.0,
                                                     loop != 0, {});
            CubismMotion* motion = CreateMotion(json);
            std::printf("authored Loop=%d -> GetLoop()=%d GetDuration()=%.4f GetLoopDuration()=%.4f\n",
                        loop, motion->GetLoop() ? 1 : 0, motion->GetDuration(),
                        motion->GetLoopDuration());
            ACubismMotion::Delete(motion);
        }
    }

    void S12_ModelOpacityCurve()
    {
        Section("S12 model-target opacity curve");
        std::string json = "{\n\t\"Version\": 3,\n";
        json += "\t\"Meta\": {\n\t\t\"Duration\": 1.0,\n\t\t\"Loop\": false,\n"
                "\t\t\"CurveCount\": 1,\n\t\t\"Fps\": 30.0,\n"
                "\t\t\"TotalSegmentCount\": 1,\n\t\t\"TotalPointCount\": 2,\n"
                "\t\t\"FadeInTime\": 0.0,\n\t\t\"FadeOutTime\": 0.0,\n"
                "\t\t\"UserDataCount\": 0\n\t},\n";
        json += "\t\"Curves\": [\n\t\t{\n\t\t\t\"Target\": \"Model\",\n"
                "\t\t\t\"Id\": \"Opacity\",\n\t\t\t\"Segments\": [\n"
                "\t\t\t\t0.0,\n\t\t\t\t0.25,\n\t\t\t\t0,\n\t\t\t\t1.0,\n"
                "\t\t\t\t0.25\n\t\t\t]\n\t\t}\n\t]\n}\n";

        CubismMotionManager manager;
        CubismMotion* motion = CreateMotion(json);
        std::printf("IsExistModelOpacity=%d opacityIndex=%d before=%.4f\n",
                    motion->IsExistModelOpacity() ? 1 : 0, motion->GetModelOpacityIndex(),
                    g_context.model->GetModelOpacity());
        manager.StartMotionPriority(motion, false, 1);
        manager.UpdateMotion(g_context.model, 0.25f);
        std::printf("after update: modelOpacity=%.4f\n",
                    g_context.model->GetModelOpacity());
        manager.StopAllMotions();
        ACubismMotion::Delete(motion);
    }

    void S13_OverlapCapacity()
    {
        Section("S13 overlapping-fade capacity (no SDK-imposed limit)");
        CubismMotionManager manager;
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 4.0,
                                                 g_context.targetValue, 1.0, 1.0, false, {});

        std::vector<CubismMotion*> motions;
        const int targets[] = {8, 32, 64};
        for (const int target : targets)
        {
            while (static_cast<int>(motions.size()) < target)
            {
                CubismMotion* motion = CreateMotion(json);
                const int priority = 1 + (static_cast<int>(motions.size()) % 5);
                manager.StartMotionPriority(motion, false, priority);
                motions.push_back(motion);
            }
            std::printf("after %d StartMotionPriority calls: entries=%u\n", target,
                        manager.GetCubismMotionQueueEntries()->GetSize());
        }

        ResetCheckpoint();
        g_context.model->SaveParameters();
        manager.UpdateMotion(g_context.model, 0.25f);
        std::printf("after one update with %u overlapping entries: entries=%u param=%.6f\n",
                    static_cast<unsigned>(motions.size()),
                    manager.GetCubismMotionQueueEntries()->GetSize(), Parameter());
        std::printf("no rejection was observed at any count; bounding overlapping fades is\n"
                    "therefore an engine-side policy, not something the SDK enforces\n");

        manager.StopAllMotions();
        for (CubismMotion* motion : motions)
        {
            ACubismMotion::Delete(motion);
        }
    }

    // The body of the null-callback probe. CubismMotionQueueManager::DoUpdateMotion
    // calls `_eventCallback(this, value, _eventCustomData)` for every fired event
    // without a null check, so leaving the callback unset is an immediate
    // dereference of a null function pointer the first time any authored event
    // fires. The engine therefore cannot treat "no callback registered" as
    // "events silently ignored".
    int S6b_NullCallbackBody()
    {
        Section("S6b null event callback hazard");
        const std::vector<EventSpec> events{{0.5, "ev"}};
        const std::string json = BuildMotionJson(g_context.parameterId.c_str(), 2.0,
                                                 g_context.targetValue, 0.0, 0.0, false, events);
        CubismMotionManager manager;
        CubismMotion* motion = CreateMotion(json);
        std::printf("no SetEventCallback; a motion with authored events is started\n");
        ResetCheckpoint();
        g_context.model->SaveParameters();
        manager.StartMotionPriority(motion, false, 1);
        ResetCheckpoint();
        std::printf("first update (window is (0, 0.25], excludes +0.5) ...\n");
        manager.UpdateMotion(g_context.model, 0.25f);
        ResetCheckpoint();
        std::printf("second update crosses +0.5 -> _eventCallback invoked\n");
        manager.UpdateMotion(g_context.model, 0.5f);
        std::printf("survived: the implementation must be null-checking the callback\n");
        manager.StopAllMotions();
        ACubismMotion::Delete(motion);
        return 0;
    }

    // Runs the body above in a child process so that a crash is reported as
    // evidence instead of taking the whole probe's output with it. _spawnv is
    // used rather than std::system because cmd.exe strips the outer pair of
    // quotes from a quoted program path and the launch then fails outright.
    int RunIsolatedChild(const std::string& executablePath, const std::string& mocPath)
    {
#ifdef _WIN32
        const char* arguments[] = {
            executablePath.c_str(), mocPath.c_str(), "--scenario=s6b", nullptr};
        const intptr_t status = _spawnv(_P_WAIT, executablePath.c_str(), arguments);
        return static_cast<int>(status);
#else
        const std::string command =
            Format("\"%s\" \"%s\" --scenario=s6b", executablePath.c_str(), mocPath.c_str());
        return std::system(command.c_str());
#endif
    }

    void S6b_NullCallbackHazard(const std::string& executablePath,
                                const std::string& mocPath)
    {
        Section("S6b null event callback hazard (isolated child process)");
        if (executablePath.empty())
        {
            std::printf("SKIPPED: probe executable path unavailable\n");
            return;
        }
        std::printf("child: %s %s --scenario=s6b\n", executablePath.c_str(), mocPath.c_str());
        std::fflush(stdout);
        const int status = RunIsolatedChild(executablePath, mocPath);
        std::printf("child exit status=%d (0x%08X) (0 = survived, 0xC0000005 = access violation)\n",
                    status, static_cast<unsigned>(status));
        std::printf("conclusion: %s\n",
                    status == 0
                        ? "null callback tolerated -> the implementation would be null-checking"
                        : "null callback dereferences immediately; a callback must always be set");
    }
}

int main(int argc, char** argv)
{
    // Unbuffered so the probe output survives an SDK-level crash.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::string mocPath = KPENGINE_LIVE2D_MOC_PATH;
    std::string scenario;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument.rfind("--scenario=", 0) == 0)
        {
            scenario = argument.substr(std::strlen("--scenario="));
        }
        else
        {
            mocPath = argument;
        }
    }

    const std::vector<std::byte> mocBytes = ReadFile(mocPath);
    if (mocBytes.empty())
    {
        std::printf("FATAL: cannot read .moc3 fixture '%s'\n", mocPath.c_str());
        return 2;
    }
    std::printf("fixture: %s (%zu bytes)\n", mocPath.c_str(), mocBytes.size());

    CubismFramework::Option option{};
    option.LogFunction = &ProbeLog;
    option.LoggingLevel = CubismFramework::Option::LogLevel_Verbose;
    if (!CubismFramework::StartUp(&Allocator(), &option))
    {
        std::printf("FATAL: CubismFramework::StartUp failed\n");
        return 2;
    }
    CubismFramework::Initialize();
    std::printf("core version=0x%08x moc=%u\n",
                static_cast<unsigned>(Core::csmGetVersion()),
                static_cast<unsigned>(Core::csmGetLatestMocVersion()));

    CubismMoc* moc = CubismMoc::Create(
        reinterpret_cast<const csmByte*>(mocBytes.data()),
        static_cast<csmSizeInt>(mocBytes.size()), true);
    if (moc == nullptr)
    {
        std::printf("FATAL: CubismMoc::Create failed\n");
        return 2;
    }
    CubismModel* model = moc->CreateModel();
    std::printf("parameterCount=%d\n", model->GetParameterCount());

    g_context.model = model;
    g_context.parameterIndex = 0;
    if (model->GetParameterCount() > 0)
    {
        g_context.parameterId = model->GetParameterId(0)->GetString().GetRawString();
    }
    const Core::csmModel* coreModel = model->GetModel();
    const csmFloat32* minimums = Core::csmGetParameterMinimumValues(coreModel);
    const csmFloat32* maximums = Core::csmGetParameterMaximumValues(coreModel);
    g_context.parameterMin = minimums[g_context.parameterIndex];
    g_context.parameterMax = maximums[g_context.parameterIndex];
    g_context.sourceValue = model->GetParameterDefaultValue(0);
    g_context.targetValue = g_context.parameterMax;
    if (std::fabs(g_context.targetValue - g_context.sourceValue) < 0.01f)
    {
        g_context.targetValue = g_context.parameterMin;
    }
    std::printf("probe parameter '%s' index=%d range=[%.4f,%.4f] source=%.4f target=%.4f\n",
                g_context.parameterId.c_str(), g_context.parameterIndex,
                g_context.parameterMin, g_context.parameterMax, g_context.sourceValue,
                g_context.targetValue);

    if (scenario == "s6b")
    {
        const int result = S6b_NullCallbackBody();
        moc->DeleteModel(model);
        CubismMoc::Delete(moc);
        CubismFramework::Dispose();
        CubismFramework::CleanUp();
        return result;
    }

    S1_PriorityReservation();
    S2_StartIgnoresPriority();
    S3_ReplacementCrossFade();
    S4_NaturalCompletion();
    S5_Cancellation();
    S6_UserEvents();
    S7_FadeEasing();
    S8_Expressions();
    S9_ExpressionBlendMath();
    S10_DeltaEdges();
    S11_LoopFlagIgnored();
    S12_ModelOpacityCurve();
    S13_OverlapCapacity();
    S6b_NullCallbackHazard(argv[0] == nullptr ? "" : argv[0], mocPath);

    moc->DeleteModel(model);
    CubismMoc::Delete(moc);
    CubismFramework::Dispose();
    CubismFramework::CleanUp();
    std::printf("\nprobe complete\n");
    return 0;
}
