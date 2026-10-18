#pragma once

// ViewerStream on Windows: a decode thread per stream running the Media Foundation decoder, the
// D3D11 video processor (NV12 → BGRA, aspect fit, BT.709) and a flip-model swap chain on a
// borderless window that covers the target monitor (FLIP_DISCARD, frame latency waitable object,
// maximum latency 1), presenting each frame as soon as it is decoded (DEVPLAN).

#include "present/presentation_thread.hpp"
#include "viewer/viewer_stream.hpp"

#include <memory>

namespace osplay::engine {

class WindowsViewerStreamFactory final : public ViewerStreamFactory {
public:
    /// `presentation` must outlive every stream this factory starts.
    explicit WindowsViewerStreamFactory(PresentationThread& presentation)
        : presentation_(&presentation) {}

    win::Expected<std::unique_ptr<ViewerStream>, PipelineError>
    start(const osplay_stream_config& stream, const ViewerMonitor& monitor, bool visible,
          ViewerStreamSink& sink) override;

private:
    PresentationThread* presentation_; // never null
};

} // namespace osplay::engine
