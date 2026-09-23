// StreamResize.h is also compiled as C++ by the host and the Qt client

#include "StreamResize.h"

extern "C" int streamResizeHeaderCxxRoundTrip(void) {
    SS_STREAM_RESIZE_RESULT result = { 5, 1920, 1080, 60, LI_STREAM_RESIZE_OK };
    SS_STREAM_RESIZE_RESULT parsed = {};
    uint8_t wire[SS_STREAM_RESIZE_RESULT_LENGTH];
    uint32_t lastDelivered = 0;

    ssStreamResizeWriteResult(&result, wire);
    if (!ssStreamResizeReadResult(wire, sizeof(wire), &parsed) || parsed.requestId != 5 || parsed.width != 1920 ||
            parsed.height != 1080 || parsed.fps != 60 || parsed.status != LI_STREAM_RESIZE_OK) {
        return 1;
    }

    return ssStreamResizeAcceptResult(parsed.requestId, 5, &lastDelivered) && lastDelivered == 5 ? 0 : 1;
}
