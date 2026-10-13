// Tests for the local cursor wire format in LocalCursor.h

#include "LocalCursor.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

// The largest update: a header and SS_LOCAL_CURSOR_MAX_PIXELS pixels
static uint8_t wire[SS_LOCAL_CURSOR_HEADER_LENGTH + SS_LOCAL_CURSOR_MAX_PIXELS * 4];

static size_t writeUpdate(bool visible, int width, int height, int hotX, int hotY, int scale) {
    LOCAL_CURSOR_UPDATE update = { visible, width != 0 ? wire + SS_LOCAL_CURSOR_HEADER_LENGTH : NULL, width, height, hotX, hotY, scale };
    ssLocalCursorWriteUpdateHeader(&update, wire);
    return SS_LOCAL_CURSOR_HEADER_LENGTH + (size_t)width * height * 4;
}

static void testProtocolConstants(void) {
    CHECK(SS_LOCAL_CURSOR_REQUEST_PTYPE == 0x55F2);
    CHECK(SS_LOCAL_CURSOR_UPDATE_PTYPE == 0x55F3);
    CHECK(LI_FF_LOCAL_CURSOR == 0x10000000);
    CHECK((LI_FF_LOCAL_CURSOR & (LI_FF_STREAM_RESIZE | 0x40000000 | LI_FF_PEN_TOUCH_EVENTS | LI_FF_CONTROLLER_TOUCH_EVENTS)) == 0);

    // The largest update and its control header fit a signed 16-bit length
    CHECK(sizeof(wire) + 4 <= 32767);
}

static void testRequests(void) {
    uint8_t request[2] = { 1, 0 };
    bool enabled = false;

    CHECK(ssLocalCursorReadRequest(request, 1, &enabled) && enabled);
    request[0] = 0;
    CHECK(ssLocalCursorReadRequest(request, 1, &enabled) && !enabled);

    request[0] = 2;
    CHECK(!ssLocalCursorReadRequest(request, 1, &enabled));
    request[0] = 1;
    CHECK(!ssLocalCursorReadRequest(request, 0, &enabled));
    CHECK(!ssLocalCursorReadRequest(request, 2, &enabled));
}

static void testUpdates(void) {
    LOCAL_CURSOR_UPDATE update;
    size_t length;

    // An image with its hot spot, as the host writes it
    length = writeUpdate(true, 32, 48, 31, 0, 1);
    CHECK(wire[0] == (SS_LOCAL_CURSOR_VISIBLE | SS_LOCAL_CURSOR_HAS_IMAGE) && wire[1] == 1);
    CHECK(wire[2] == 32 && wire[3] == 0 && wire[4] == 48 && wire[6] == 31 && wire[8] == 0);
    CHECK(ssLocalCursorReadUpdate(wire, length, &update));
    CHECK(update.visible && update.width == 32 && update.height == 48 && update.hotX == 31 && update.hotY == 0 && update.scale == 1);
    CHECK(update.image == wire + SS_LOCAL_CURSOR_HEADER_LENGTH);

    // A shrunk image of the largest size
    length = writeUpdate(false, 90, 90, 45, 89, 2);
    CHECK(ssLocalCursorReadUpdate(wire, length, &update) && !update.visible && update.scale == 2);

    // Visibility alone carries no image and no image fields
    length = writeUpdate(false, 0, 0, 0, 0, 0);
    CHECK(length == SS_LOCAL_CURSOR_HEADER_LENGTH && wire[0] == 0);
    CHECK(ssLocalCursorReadUpdate(wire, length, &update) && !update.visible && update.image == NULL);
    length = writeUpdate(true, 0, 0, 0, 0, 0);
    CHECK(ssLocalCursorReadUpdate(wire, length, &update) && update.visible && update.image == NULL);
}

static void testMalformedUpdates(void) {
    LOCAL_CURSOR_UPDATE update;
    size_t length;

    length = writeUpdate(true, 16, 16, 0, 0, 1);
    CHECK(!ssLocalCursorReadUpdate(wire, length - 1, &update));
    CHECK(!ssLocalCursorReadUpdate(wire, length + 1, &update));
    CHECK(!ssLocalCursorReadUpdate(wire, SS_LOCAL_CURSOR_HEADER_LENGTH - 1, &update));
    wire[0] |= 0x04;
    CHECK(!ssLocalCursorReadUpdate(wire, length, &update));

    CHECK(!ssLocalCursorReadUpdate(wire, writeUpdate(true, 16, 16, 16, 0, 1), &update));
    CHECK(!ssLocalCursorReadUpdate(wire, writeUpdate(true, 16, 16, 0, 16, 1), &update));
    CHECK(!ssLocalCursorReadUpdate(wire, writeUpdate(true, 16, 16, 0, 0, 0), &update));
    CHECK(!ssLocalCursorReadUpdate(wire, writeUpdate(true, 1, 0, 0, 0, 1), &update));

    // One pixel over the limit. The length is checked before any pixel is read.
    writeUpdate(true, 1, 1, 0, 0, 1);
    ssLocalCursorPut16(wire + 2, SS_LOCAL_CURSOR_MAX_PIXELS + 1);
    CHECK(!ssLocalCursorReadUpdate(wire, SS_LOCAL_CURSOR_HEADER_LENGTH + (SS_LOCAL_CURSOR_MAX_PIXELS + 1) * 4, &update));

    // Image fields without an image
    length = writeUpdate(true, 0, 0, 0, 0, 0);
    wire[1] = 1;
    CHECK(!ssLocalCursorReadUpdate(wire, length, &update));
}

int main(void) {
    testProtocolConstants();
    testRequests();
    testUpdates();
    testMalformedUpdates();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("local cursor tests passed\n");
    return 0;
}
