// Tests for the stream resize wire format and the rules in StreamResize.h

#include "StreamResize.h"

#include <stdio.h>
#include <string.h>

int streamResizeHeaderCxxRoundTrip(void);

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void writeRequestBytes(uint8_t out[SS_STREAM_RESIZE_REQUEST_LENGTH], uint32_t requestId, uint16_t width, uint16_t height, uint16_t fps) {
    SS_STREAM_RESIZE_REQUEST request = { requestId, width, height, fps };
    ssStreamResizeWriteRequest(&request, out);
}

static void writeResultBytes(uint8_t out[SS_STREAM_RESIZE_RESULT_LENGTH], uint32_t requestId, uint16_t width, uint16_t height, uint16_t fps, uint8_t status) {
    SS_STREAM_RESIZE_RESULT result = { requestId, width, height, fps, status, false, 0 };
    ssStreamResizeWriteResult(&result, out);
}

static int readRequest(uint32_t requestId, uint16_t width, uint16_t height, uint16_t fps, PSS_STREAM_RESIZE_REQUEST request) {
    uint8_t wire[SS_STREAM_RESIZE_REQUEST_LENGTH];
    writeRequestBytes(wire, requestId, width, height, fps);
    return ssStreamResizeReadRequest(wire, sizeof(wire), request);
}

static bool readResult(uint32_t requestId, uint16_t width, uint16_t height, uint16_t fps, uint8_t status) {
    uint8_t wire[SS_STREAM_RESIZE_RESULT_LENGTH];
    SS_STREAM_RESIZE_RESULT result;
    writeResultBytes(wire, requestId, width, height, fps, status);
    return ssStreamResizeReadResult(wire, sizeof(wire), &result);
}

static void testProtocolConstants(void) {
    CHECK(SS_STREAM_RESIZE_REQUEST_PTYPE == 0x55F0);
    CHECK(SS_STREAM_RESIZE_RESULT_PTYPE == 0x55F1);
    CHECK(SS_STREAM_RESIZE_REQUEST_LENGTH == 10);
    CHECK(SS_STREAM_RESIZE_RESULT_LENGTH == 16);
    CHECK(SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME == 0x01);

    // A host with the unreleased first version advertised 0x40000000 and answered without the first
    // frame, so this version must not be mistaken for it
    CHECK(LI_FF_STREAM_RESIZE == 0x20000000);
    CHECK((LI_FF_STREAM_RESIZE & 0x40000000) == 0);
    CHECK((LI_FF_STREAM_RESIZE & (LI_FF_PEN_TOUCH_EVENTS | LI_FF_CONTROLLER_TOUCH_EVENTS)) == 0);

    CHECK(LI_STREAM_RESIZE_OK == 0);
    CHECK(LI_STREAM_RESIZE_SUPERSEDED == 1);
    CHECK(LI_STREAM_RESIZE_REJECTED_INVALID == 2);
    CHECK(LI_STREAM_RESIZE_REJECTED_UNSUPPORTED == 3);
    CHECK(LI_STREAM_RESIZE_FAILED_ROLLED_BACK == 4);
    CHECK(LI_STREAM_RESIZE_FAILED_SESSION_ENDING == 5);

    // Sunshine extension packet types already in use in either direction
    static const int usedTypes[] = { 0x0301, 0x0350, 0x5500, 0x5501, 0x5502, 0x5503, 0x5504 };
    for (size_t i = 0; i < sizeof(usedTypes) / sizeof(usedTypes[0]); i++) {
        CHECK(usedTypes[i] != SS_STREAM_RESIZE_REQUEST_PTYPE);
        CHECK(usedTypes[i] != SS_STREAM_RESIZE_RESULT_PTYPE);
    }
}

static void testRequestWireLayout(void) {
    static const uint8_t expected[SS_STREAM_RESIZE_REQUEST_LENGTH] = {
        0x04, 0x03, 0x02, 0x01, // requestId 0x01020304
        0x80, 0x07,             // width 1920
        0x38, 0x04,             // height 1080
        0x3C, 0x00,             // fps 60
    };
    uint8_t wire[SS_STREAM_RESIZE_REQUEST_LENGTH];
    SS_STREAM_RESIZE_REQUEST request;

    writeRequestBytes(wire, 0x01020304, 1920, 1080, 60);
    CHECK(memcmp(wire, expected, sizeof(expected)) == 0);

    CHECK(ssStreamResizeReadRequest(expected, sizeof(expected), &request) == SS_STREAM_RESIZE_REQUEST_VALID);
    CHECK(request.requestId == 0x01020304);
    CHECK(request.width == 1920);
    CHECK(request.height == 1080);
    CHECK(request.fps == 60);
}

static void testResultWireLayout(void) {
    static const uint8_t expected[SS_STREAM_RESIZE_RESULT_LENGTH] = {
        0xDD, 0xCC, 0xBB, 0xAA, // requestId 0xAABBCCDD
        0x00, 0x0A,             // width 2560
        0xA0, 0x05,             // height 1440
        0xE8, 0x03,             // fps 1000
        0x04,                   // status FAILED_ROLLED_BACK
        0x01,                   // flags HAS_FIRST_FRAME
        0x44, 0x33, 0x22, 0x11, // firstFrame 0x11223344
    };
    static const uint8_t expectedWithout[SS_STREAM_RESIZE_RESULT_LENGTH] = {
        0x07, 0x00, 0x00, 0x00, // requestId 7
        0x80, 0x07,             // width 1920
        0x38, 0x04,             // height 1080
        0x3C, 0x00,             // fps 60
        0x03,                   // status REJECTED_UNSUPPORTED
        0x00,                   // no flags
        0x00, 0x00, 0x00, 0x00, // no firstFrame
    };
    SS_STREAM_RESIZE_RESULT result = { 0xAABBCCDD, 2560, 1440, 1000, LI_STREAM_RESIZE_FAILED_ROLLED_BACK, true, 0x11223344 };
    uint8_t wire[SS_STREAM_RESIZE_RESULT_LENGTH];

    ssStreamResizeWriteResult(&result, wire);
    CHECK(memcmp(wire, expected, sizeof(expected)) == 0);

    memset(&result, 0, sizeof(result));
    CHECK(ssStreamResizeReadResult(expected, sizeof(expected), &result));
    CHECK(result.requestId == 0xAABBCCDD);
    CHECK(result.width == 2560);
    CHECK(result.height == 1440);
    CHECK(result.fps == 1000);
    CHECK(result.status == LI_STREAM_RESIZE_FAILED_ROLLED_BACK);
    CHECK(result.hasFirstFrame);
    CHECK(result.firstFrame == 0x11223344);

    // Without the flag, whatever firstFrame holds is not written
    result = (SS_STREAM_RESIZE_RESULT){ 7, 1920, 1080, 60, LI_STREAM_RESIZE_REJECTED_UNSUPPORTED, false, 99 };
    ssStreamResizeWriteResult(&result, wire);
    CHECK(memcmp(wire, expectedWithout, sizeof(expectedWithout)) == 0);
    CHECK(ssStreamResizeReadResult(wire, sizeof(wire), &result));
    CHECK(!result.hasFirstFrame);
    CHECK(result.firstFrame == 0);

    // Frame 0 is a real frame number once frame numbers wrap
    result = (SS_STREAM_RESIZE_RESULT){ 8, 1920, 1080, 60, LI_STREAM_RESIZE_OK, true, 0 };
    ssStreamResizeWriteResult(&result, wire);
    memset(&result, 0, sizeof(result));
    CHECK(ssStreamResizeReadResult(wire, sizeof(wire), &result));
    CHECK(result.hasFirstFrame);
    CHECK(result.firstFrame == 0);
}

static void testValueLimits(void) {
    CHECK(ssStreamResizeValuesValid(640, 360, 1));
    CHECK(ssStreamResizeValuesValid(7680, 4320, 1000));
    CHECK(ssStreamResizeValuesValid(2560, 1440, 120));

    CHECK(!ssStreamResizeValuesValid(638, 360, 60));
    CHECK(!ssStreamResizeValuesValid(7682, 1080, 60));
    CHECK(!ssStreamResizeValuesValid(1920, 358, 60));
    CHECK(!ssStreamResizeValuesValid(1920, 4322, 60));
    CHECK(!ssStreamResizeValuesValid(1921, 1080, 60));
    CHECK(!ssStreamResizeValuesValid(1920, 1081, 60));
    CHECK(!ssStreamResizeValuesValid(1920, 1080, 0));
    CHECK(!ssStreamResizeValuesValid(1920, 1080, 1001));
    CHECK(!ssStreamResizeValuesValid(-1920, 1080, 60));
    CHECK(!ssStreamResizeValuesValid(1920, -1080, 60));
    CHECK(!ssStreamResizeValuesValid(1920, 1080, -60));
    CHECK(!ssStreamResizeValuesValid(65536 + 1920, 1080, 60));
}

static void testMalformedRequests(void) {
    uint8_t wire[SS_STREAM_RESIZE_REQUEST_LENGTH + 1] = { 0 };
    SS_STREAM_RESIZE_REQUEST request;

    // Too short to hold a request ID
    writeRequestBytes(wire, 9, 1920, 1080, 60);
    for (size_t length = 0; length < 4; length++) {
        CHECK(ssStreamResizeReadRequest(wire, length, &request) == SS_STREAM_RESIZE_REQUEST_DROP);
        CHECK(request.requestId == 0);
    }

    // Request ID 0 is never valid, so there is nothing to answer
    CHECK(readRequest(0, 1920, 1080, 60, &request) == SS_STREAM_RESIZE_REQUEST_DROP);

    // Wrong length with a readable request ID is answered as invalid
    for (size_t length = 4; length <= sizeof(wire); length++) {
        if (length == SS_STREAM_RESIZE_REQUEST_LENGTH) {
            continue;
        }
        CHECK(ssStreamResizeReadRequest(wire, length, &request) == SS_STREAM_RESIZE_REQUEST_INVALID);
        CHECK(request.requestId == 9);
        CHECK(request.width == 0 && request.height == 0 && request.fps == 0);
    }

    // Out of range values are answered as invalid and never passed on
    CHECK(readRequest(10, 1921, 1080, 60, &request) == SS_STREAM_RESIZE_REQUEST_INVALID);
    CHECK(request.requestId == 10);
    CHECK(request.width == 0 && request.height == 0 && request.fps == 0);
    CHECK(readRequest(11, 1920, 1080, 0, &request) == SS_STREAM_RESIZE_REQUEST_INVALID);
    CHECK(readRequest(12, 8192, 4320, 60, &request) == SS_STREAM_RESIZE_REQUEST_INVALID);
    CHECK(readRequest(13, 320, 240, 60, &request) == SS_STREAM_RESIZE_REQUEST_INVALID);
    CHECK(readRequest(14, 640, 360, 1, &request) == SS_STREAM_RESIZE_REQUEST_VALID);
}

static void testMalformedResults(void) {
    uint8_t wire[SS_STREAM_RESIZE_RESULT_LENGTH + 1] = { 0 };
    SS_STREAM_RESIZE_RESULT result;

    writeResultBytes(wire, 3, 1920, 1080, 60, LI_STREAM_RESIZE_OK);
    CHECK(ssStreamResizeReadResult(wire, SS_STREAM_RESIZE_RESULT_LENGTH, &result));
    for (size_t length = 0; length <= sizeof(wire); length++) {
        if (length != SS_STREAM_RESIZE_RESULT_LENGTH) {
            CHECK(!ssStreamResizeReadResult(wire, length, &result));
        }
    }

    CHECK(!readResult(0, 1920, 1080, 60, LI_STREAM_RESIZE_OK));
    CHECK(!readResult(3, 1920, 1080, 60, 6));
    CHECK(!readResult(3, 1920, 1080, 60, 0xFF));

    // The unreleased first version answered in 11 bytes
    CHECK(!ssStreamResizeReadResult(wire, 11, &result));

    // Flags other than HAS_FIRST_FRAME are not defined, and a first frame needs its flag
    for (int bit = 1; bit < 8; bit++) {
        writeResultBytes(wire, 3, 1920, 1080, 60, LI_STREAM_RESIZE_OK);
        wire[11] = (uint8_t)(1 << bit);
        CHECK(!ssStreamResizeReadResult(wire, SS_STREAM_RESIZE_RESULT_LENGTH, &result));
        wire[11] |= SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME;
        CHECK(!ssStreamResizeReadResult(wire, SS_STREAM_RESIZE_RESULT_LENGTH, &result));
    }
    writeResultBytes(wire, 3, 1920, 1080, 60, LI_STREAM_RESIZE_OK);
    wire[12] = 1;
    CHECK(!ssStreamResizeReadResult(wire, SS_STREAM_RESIZE_RESULT_LENGTH, &result));
    wire[11] = SS_STREAM_RESIZE_RESULT_HAS_FIRST_FRAME;
    CHECK(ssStreamResizeReadResult(wire, SS_STREAM_RESIZE_RESULT_LENGTH, &result));
    CHECK(result.hasFirstFrame && result.firstFrame == 1);

    for (uint8_t status = LI_STREAM_RESIZE_OK; status <= LI_STREAM_RESIZE_FAILED_SESSION_ENDING; status++) {
        CHECK(readResult(3, 1920, 1080, 60, status));
    }

    // An OK result must be a size the host was allowed to switch to
    CHECK(!readResult(3, 1921, 1080, 60, LI_STREAM_RESIZE_OK));
    CHECK(!readResult(3, 7682, 4320, 60, LI_STREAM_RESIZE_OK));
    CHECK(!readResult(3, 1920, 1080, 0, LI_STREAM_RESIZE_OK));

    // Other results report the size that stayed in effect, which may be the starting size
    CHECK(readResult(3, 1366, 767, 60, LI_STREAM_RESIZE_FAILED_ROLLED_BACK));
    CHECK(readResult(3, 8192, 4320, 30, LI_STREAM_RESIZE_REJECTED_UNSUPPORTED));
    CHECK(!readResult(3, 0, 1080, 60, LI_STREAM_RESIZE_REJECTED_INVALID));
    CHECK(!readResult(3, 1920, 0, 60, LI_STREAM_RESIZE_SUPERSEDED));
    CHECK(!readResult(3, 1920, 1080, 0, LI_STREAM_RESIZE_FAILED_SESSION_ENDING));
}

static void testResultOrdering(void) {
    uint32_t lastDelivered = 0;

    // Nothing sent yet
    CHECK(!ssStreamResizeAcceptResult(1, 0, &lastDelivered));
    CHECK(lastDelivered == 0);

    // One request in flight
    CHECK(!ssStreamResizeAcceptResult(0, 1, &lastDelivered));
    CHECK(!ssStreamResizeAcceptResult(2, 1, &lastDelivered));
    CHECK(ssStreamResizeAcceptResult(1, 1, &lastDelivered));
    CHECK(lastDelivered == 1);

    // Duplicate
    CHECK(!ssStreamResizeAcceptResult(1, 1, &lastDelivered));
    CHECK(lastDelivered == 1);

    // Requests 2, 3, and 4 sent back to back. The host started 2, superseded 3 with 4, and answers
    // in that order. Every answer describes what the host really streams, so all three arrive.
    CHECK(ssStreamResizeAcceptResult(2, 4, &lastDelivered));
    CHECK(ssStreamResizeAcceptResult(3, 4, &lastDelivered));
    CHECK(ssStreamResizeAcceptResult(4, 4, &lastDelivered));
    CHECK(lastDelivered == 4);

    // Anything at or below the last delivered ID is stale or a duplicate
    CHECK(!ssStreamResizeAcceptResult(2, 4, &lastDelivered));
    CHECK(!ssStreamResizeAcceptResult(4, 4, &lastDelivered));

    // Skipped IDs are fine as long as the order holds
    CHECK(ssStreamResizeAcceptResult(7, 9, &lastDelivered));
    CHECK(!ssStreamResizeAcceptResult(6, 9, &lastDelivered));
    CHECK(lastDelivered == 7);

    CHECK(!ssStreamResizeAcceptResult(UINT32_MAX, 9, &lastDelivered));
    CHECK(lastDelivered == 7);
}

int main(void) {
    testProtocolConstants();
    testRequestWireLayout();
    testResultWireLayout();
    testValueLimits();
    testMalformedRequests();
    testMalformedResults();
    testResultOrdering();
    CHECK(streamResizeHeaderCxxRoundTrip() == 0);

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("stream resize tests passed\n");
    return 0;
}
