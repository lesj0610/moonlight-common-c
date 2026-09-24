#pragma once

// Wire format of the stream resize extension (LI_FF_STREAM_RESIZE).
//
// This is a fork-only extension. Upstream Sunshine and Moonlight don't reserve these packet types,
// so only hosts and clients built from this fork understand them. The host and the client both use
// this header so the format is defined in one place.
//
// Fields are little-endian and unpadded.
//
// Request, client to host, SS_STREAM_RESIZE_REQUEST_PTYPE, 10 bytes:
//   uint32_t requestId, uint16_t width, uint16_t height, uint16_t fps
//
// Result, host to client, SS_STREAM_RESIZE_RESULT_PTYPE, 16 bytes:
//   uint32_t requestId, uint16_t width, uint16_t height, uint16_t fps, uint8_t status,
//   uint8_t flags, uint32_t firstFrame
//
// The host answers every request it can read a request ID from with exactly one result, in request
// order. width, height, and fps in a result are what the host streams after handling the request.
// With SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME in flags, firstFrame is the frame number of the
// first frame the host encoded at that mode for this answer. Other flag bits are not defined.
//
// This is version 2, advertised with LI_FF_STREAM_RESIZE. Version 1 answered in 11 bytes without
// flags and firstFrame, used another feature flag, and was never released.

#include "Limelight.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER) && !defined(__cplusplus)
#define SS_STREAM_RESIZE_INLINE static __inline
#else
#define SS_STREAM_RESIZE_INLINE static inline
#endif

#define SS_STREAM_RESIZE_REQUEST_PTYPE  0x55F0
#define SS_STREAM_RESIZE_REQUEST_LENGTH 10
#define SS_STREAM_RESIZE_RESULT_PTYPE   0x55F1
#define SS_STREAM_RESIZE_RESULT_LENGTH  16

#define SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME 0x01

typedef struct _SS_STREAM_RESIZE_REQUEST {
    uint32_t requestId;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
} SS_STREAM_RESIZE_REQUEST, *PSS_STREAM_RESIZE_REQUEST;

typedef struct _SS_STREAM_RESIZE_RESULT {
    uint32_t requestId;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint8_t status;
    bool hasFirstFrame;
    uint32_t firstFrame;
} SS_STREAM_RESIZE_RESULT, *PSS_STREAM_RESIZE_RESULT;

// What the host does with a received request (see ssStreamResizeReadRequest())
#define SS_STREAM_RESIZE_REQUEST_VALID   0 // Handle the request
#define SS_STREAM_RESIZE_REQUEST_INVALID 1 // Answer LI_STREAM_RESIZE_REJECTED_INVALID for its request ID
#define SS_STREAM_RESIZE_REQUEST_DROP    2 // There is no usable request ID, so ignore it

SS_STREAM_RESIZE_INLINE bool ssStreamResizeValuesValid(int width, int height, int fps) {
    return width >= LI_STREAM_RESIZE_MIN_WIDTH && width <= LI_STREAM_RESIZE_MAX_WIDTH && (width % 2) == 0 &&
           height >= LI_STREAM_RESIZE_MIN_HEIGHT && height <= LI_STREAM_RESIZE_MAX_HEIGHT && (height % 2) == 0 &&
           fps >= LI_STREAM_RESIZE_MIN_FPS && fps <= LI_STREAM_RESIZE_MAX_FPS;
}

SS_STREAM_RESIZE_INLINE bool ssStreamResizeStatusKnown(uint8_t status) {
    return status <= LI_STREAM_RESIZE_FAILED_SESSION_ENDING;
}

SS_STREAM_RESIZE_INLINE void ssStreamResizePut16(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

SS_STREAM_RESIZE_INLINE void ssStreamResizePut32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

SS_STREAM_RESIZE_INLINE uint16_t ssStreamResizeGet16(const uint8_t* in) {
    return (uint16_t)(in[0] | (in[1] << 8));
}

SS_STREAM_RESIZE_INLINE uint32_t ssStreamResizeGet32(const uint8_t* in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

SS_STREAM_RESIZE_INLINE void ssStreamResizeWriteRequest(const SS_STREAM_RESIZE_REQUEST* request, uint8_t out[SS_STREAM_RESIZE_REQUEST_LENGTH]) {
    ssStreamResizePut32(out, request->requestId);
    ssStreamResizePut16(out + 4, request->width);
    ssStreamResizePut16(out + 6, request->height);
    ssStreamResizePut16(out + 8, request->fps);
}

// Returns one of the SS_STREAM_RESIZE_REQUEST_* values. For SS_STREAM_RESIZE_REQUEST_INVALID, only
// requestId is filled in and the other fields are zero.
SS_STREAM_RESIZE_INLINE int ssStreamResizeReadRequest(const void* data, size_t length, PSS_STREAM_RESIZE_REQUEST request) {
    const uint8_t* in = (const uint8_t*)data;

    request->requestId = 0;
    request->width = 0;
    request->height = 0;
    request->fps = 0;

    if (length < sizeof(request->requestId)) {
        return SS_STREAM_RESIZE_REQUEST_DROP;
    }

    request->requestId = ssStreamResizeGet32(in);
    if (request->requestId == 0) {
        return SS_STREAM_RESIZE_REQUEST_DROP;
    }

    if (length != SS_STREAM_RESIZE_REQUEST_LENGTH) {
        return SS_STREAM_RESIZE_REQUEST_INVALID;
    }

    uint16_t width = ssStreamResizeGet16(in + 4);
    uint16_t height = ssStreamResizeGet16(in + 6);
    uint16_t fps = ssStreamResizeGet16(in + 8);
    if (!ssStreamResizeValuesValid(width, height, fps)) {
        return SS_STREAM_RESIZE_REQUEST_INVALID;
    }

    request->width = width;
    request->height = height;
    request->fps = fps;
    return SS_STREAM_RESIZE_REQUEST_VALID;
}

SS_STREAM_RESIZE_INLINE void ssStreamResizeWriteResult(const SS_STREAM_RESIZE_RESULT* result, uint8_t out[SS_STREAM_RESIZE_RESULT_LENGTH]) {
    ssStreamResizePut32(out, result->requestId);
    ssStreamResizePut16(out + 4, result->width);
    ssStreamResizePut16(out + 6, result->height);
    ssStreamResizePut16(out + 8, result->fps);
    out[10] = result->status;
    out[11] = result->hasFirstFrame ? SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME : 0;
    ssStreamResizePut32(out + 12, result->hasFirstFrame ? result->firstFrame : 0);
}

// Returns false for a malformed result, which must be dropped: wrong length, request ID 0, an unknown
// status, undefined flag bits, a first frame without its flag, an OK result outside the
// LI_STREAM_RESIZE_* limits, or any other result with a zero value. A failed result reports the size
// that stayed in effect, which may be the size the stream started with and need not fit the resize
// limits.
SS_STREAM_RESIZE_INLINE bool ssStreamResizeReadResult(const void* data, size_t length, PSS_STREAM_RESIZE_RESULT result) {
    const uint8_t* in = (const uint8_t*)data;

    if (length != SS_STREAM_RESIZE_RESULT_LENGTH) {
        return false;
    }

    result->requestId = ssStreamResizeGet32(in);
    result->width = ssStreamResizeGet16(in + 4);
    result->height = ssStreamResizeGet16(in + 6);
    result->fps = ssStreamResizeGet16(in + 8);
    result->status = in[10];
    result->hasFirstFrame = (in[11] & SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME) != 0;
    result->firstFrame = ssStreamResizeGet32(in + 12);

    if (result->requestId == 0 || !ssStreamResizeStatusKnown(result->status)) {
        return false;
    }

    if ((in[11] & ~SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME) != 0 || (!result->hasFirstFrame && result->firstFrame != 0)) {
        return false;
    }

    if (result->status == LI_STREAM_RESIZE_OK) {
        return ssStreamResizeValuesValid(result->width, result->height, result->fps);
    }

    return result->width != 0 && result->height != 0 && result->fps != 0;
}

// Whether frame number frame is at or after frame number first, comparing them the way sequence
// numbers are compared, so the answer holds across the frame number wrapping.
SS_STREAM_RESIZE_INLINE bool ssStreamResizeFrameAtOrAfter(uint32_t frame, uint32_t first) {
    return (int32_t)(frame - first) >= 0;
}

// Client-side ordering rule for well-formed results. lastSentId is the newest request ID sent on
// this connection and lastDeliveredId the newest one already delivered (0 for none). A result is
// delivered only if its ID was sent and is newer than the last delivered one; anything else is
// stale or a duplicate. A result for an ID older than lastSentId is still delivered: the host
// answers in request order, so it describes a switch the host really made.
SS_STREAM_RESIZE_INLINE bool ssStreamResizeAcceptResult(uint32_t requestId, uint32_t lastSentId, uint32_t* lastDeliveredId) {
    if (requestId == 0 || requestId > lastSentId || requestId <= *lastDeliveredId) {
        return false;
    }

    *lastDeliveredId = requestId;
    return true;
}

#ifdef __cplusplus
}
#endif
