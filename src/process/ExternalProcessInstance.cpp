// Implementation of ExternalProcessInstance declared in
// ExternalProcessInstance.h.
#include "process/ExternalProcessInstance.h"

#include <signal.h>

#include "core/Log.h"
#include "process/ChildSupervisor.h"

namespace canvas {

ExternalProcessInstance::ExternalProcessInstance(ChildSupervisor& supervisor,
                                                 std::string tag,
                                                 std::string outputName,
                                                 std::string wallpaperId,
                                                 bool muted,
                                                 ArgvBuilder builder)
    : supervisor_(supervisor),
      tag_(std::move(tag)),
      output_name_(std::move(outputName)),
      wallpaper_id_(std::move(wallpaperId)),
      muted_(muted),
      builder_(std::move(builder)) {}

ExternalProcessInstance::~ExternalProcessInstance() { stop(); }

void ExternalProcessInstance::start() {
    supervisor_.start(tag_, builder_(muted_));
    started_ = true;
}

void ExternalProcessInstance::stop() {
    if (!started_) return;
    supervisor_.stop(tag_);
    started_ = false;
}

void ExternalProcessInstance::pause() {
    if (started_) supervisor_.signalChild(tag_, SIGSTOP);
}

void ExternalProcessInstance::resume() {
    if (started_) supervisor_.signalChild(tag_, SIGCONT);
}

void ExternalProcessInstance::setMuted(bool muted) {
    if (muted_ == muted) return;
    muted_ = muted;
    if (started_) {
        log::info() << tag_ << ": mute change requires restart";
        start();  // supervisor start() replaces the running child
    }
}

}  // namespace canvas
