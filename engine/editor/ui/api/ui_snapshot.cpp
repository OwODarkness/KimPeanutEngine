#include "editor_ui/ui_snapshot.h"

#include <utility>

namespace kpengine::editor::ui
{
    void UiSnapshotStore::Publish(UiSnapshotCapture capture)
    {
        if (capture.session == 0 || capture.frame == 0) return;
        auto published = std::make_shared<const UiSnapshotCapture>(std::move(capture));
        std::scoped_lock lock(mutex_);
        latest_ = std::move(published);
    }

    std::shared_ptr<const UiSnapshotCapture> UiSnapshotStore::Latest(const UiSessionId session) const
    {
        std::scoped_lock lock(mutex_);
        return latest_ && latest_->session == session ? latest_ : nullptr;
    }

    void UiSnapshotStore::Clear(const UiSessionId session) noexcept
    {
        std::scoped_lock lock(mutex_);
        if (latest_ && latest_->session == session)
            latest_.reset();
    }
}
