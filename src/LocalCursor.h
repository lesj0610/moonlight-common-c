#pragma once

// Wire format of the local cursor extension (LI_FF_LOCAL_CURSOR).
//
// This is a fork-only extension, like the stream resize one in StreamResize.h. The client draws the
// mouse pointer itself at its own mouse position, so the pointer moves without waiting for video.
// The host stops drawing the pointer into the video and tells the client how it looks instead.
//
// Fields are little-endian and unpadded.
//
// Request, client to host, SS_LOCAL_CURSOR_REQUEST_PTYPE, 1 byte:
//   uint8_t enabled (0 or 1)
//
// Update, host to client, SS_LOCAL_CURSOR_UPDATE_PTYPE, 10 bytes and then the image, if any:
//   uint8_t flags, uint8_t scale, uint16_t width, uint16_t height, uint16_t hotX, uint16_t hotY,
//   width * height pixels of 32-bit BGRA with straight alpha, row by row from the top
//
// SS_LOCAL_CURSOR_VISIBLE in flags shows the pointer. Without SS_LOCAL_CURSOR_HAS_IMAGE an update
// only changes whether it is shown, and scale through hotY are zero. The host shrinks an image with
// more than SS_LOCAL_CURSOR_MAX_PIXELS pixels by a whole factor, which scale gives (1 for an image
// sent as it is), so the client can draw it as large as the host would. Other flag bits are not
// defined.
//
// After a request to enable it, the host sends the current pointer with its image, and then an
// update whenever the pointer changes, until a request to disable it.

#include "Limelight.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER) && !defined(__cplusplus)
#define SS_LOCAL_CURSOR_INLINE static __inline
#else
#define SS_LOCAL_CURSOR_INLINE static inline
#endif

#define SS_LOCAL_CURSOR_REQUEST_PTYPE  0x55F2
#define SS_LOCAL_CURSOR_REQUEST_LENGTH 1
#define SS_LOCAL_CURSOR_UPDATE_PTYPE   0x55F3
#define SS_LOCAL_CURSOR_HEADER_LENGTH  10

// Keeps an update well inside the 16-bit lengths that control messages use
#define SS_LOCAL_CURSOR_MAX_PIXELS 8100

#define SS_LOCAL_CURSOR_VISIBLE   0x01
#define SS_LOCAL_CURSOR_HAS_IMAGE 0x02

SS_LOCAL_CURSOR_INLINE void ssLocalCursorPut16(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

SS_LOCAL_CURSOR_INLINE uint16_t ssLocalCursorGet16(const uint8_t* in) {
    return (uint16_t)(in[0] | (in[1] << 8));
}

// Returns false for a malformed request, which must be ignored.
SS_LOCAL_CURSOR_INLINE bool ssLocalCursorReadRequest(const void* data, size_t length, bool* enabled) {
    const uint8_t* in = (const uint8_t*)data;

    if (length != SS_LOCAL_CURSOR_REQUEST_LENGTH || in[0] > 1) {
        return false;
    }

    *enabled = in[0] != 0;
    return true;
}

// Writes the 10 byte header of an update. The image, if the update has one, follows it.
SS_LOCAL_CURSOR_INLINE void ssLocalCursorWriteUpdateHeader(const LOCAL_CURSOR_UPDATE* update, uint8_t out[SS_LOCAL_CURSOR_HEADER_LENGTH]) {
    bool hasImage = update->image != NULL;

    out[0] = (uint8_t)((update->visible ? SS_LOCAL_CURSOR_VISIBLE : 0) | (hasImage ? SS_LOCAL_CURSOR_HAS_IMAGE : 0));
    out[1] = hasImage ? (uint8_t)update->scale : 0;
    ssLocalCursorPut16(out + 2, hasImage ? (uint16_t)update->width : 0);
    ssLocalCursorPut16(out + 4, hasImage ? (uint16_t)update->height : 0);
    ssLocalCursorPut16(out + 6, hasImage ? (uint16_t)update->hotX : 0);
    ssLocalCursorPut16(out + 8, hasImage ? (uint16_t)update->hotY : 0);
}

// Returns false for a malformed update, which must be dropped: a length that doesn't match, undefined
// flag bits, an image that is empty, too large, scaled by 0 or has its hot spot outside it, or
// nonzero image fields without an image. image points into data.
SS_LOCAL_CURSOR_INLINE bool ssLocalCursorReadUpdate(const void* data, size_t length, LOCAL_CURSOR_UPDATE* update) {
    const uint8_t* in = (const uint8_t*)data;

    if (length < SS_LOCAL_CURSOR_HEADER_LENGTH) {
        return false;
    }

    uint8_t flags = in[0];
    update->visible = (flags & SS_LOCAL_CURSOR_VISIBLE) != 0;
    update->scale = in[1];
    update->width = ssLocalCursorGet16(in + 2);
    update->height = ssLocalCursorGet16(in + 4);
    update->hotX = ssLocalCursorGet16(in + 6);
    update->hotY = ssLocalCursorGet16(in + 8);
    update->image = NULL;

    if ((flags & ~(SS_LOCAL_CURSOR_VISIBLE | SS_LOCAL_CURSOR_HAS_IMAGE)) != 0) {
        return false;
    }

    if (!(flags & SS_LOCAL_CURSOR_HAS_IMAGE)) {
        return length == SS_LOCAL_CURSOR_HEADER_LENGTH &&
               update->scale == 0 && update->width == 0 && update->height == 0 && update->hotX == 0 && update->hotY == 0;
    }

    if (update->width == 0 || update->height == 0 || update->scale == 0 ||
            (size_t)update->width * update->height > SS_LOCAL_CURSOR_MAX_PIXELS ||
            update->hotX >= update->width || update->hotY >= update->height ||
            length != SS_LOCAL_CURSOR_HEADER_LENGTH + (size_t)update->width * update->height * 4) {
        return false;
    }

    update->image = in + SS_LOCAL_CURSOR_HEADER_LENGTH;
    return true;
}

#ifdef __cplusplus
}
#endif
