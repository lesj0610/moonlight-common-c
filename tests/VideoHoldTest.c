// Tests for holding video back while the stream changes size (LiHoldVideo() and LiResumeVideo()).
//
// Frames go in through queueRtpPacket(), where packets from the RTP queue enter the depacketizer,
// and come out every way a decoder can take them: LiWaitForNextVideoFrame(), which the decoder
// thread of push renderers and the decoder thread of pull renderers block in,
// LiPollNextVideoFrame(), LiPeekNextVideoFrame(), and the submit callback of direct submit
// decoders. The depacketizer, the queue, and the platform code are the real ones. Only the parts
// of the control and video streams the depacketizer calls out to are stood in for below.

#include "Limelight-internal.h"
#include "StreamResize.h"

#include <stdarg.h>
#include <stdio.h>

int AppVersionQuad[4];
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
DECODER_RENDERER_CALLBACKS VideoCallbacks;
int NegotiatedVideoFormat;

static int idrRequests;

bool isReferenceFrameInvalidationEnabled(void) { return false; }
bool LiGetCurrentHostDisplayHdrMode(void) { return false; }
void LiRequestIdrFrame(void) { idrRequests++; }
void connectionDetectedFrameLoss(uint32_t startFrame, uint32_t endFrame) {}
void connectionReceivedCompleteFrame(uint32_t frameIndex, bool frameIsLTR) {}
void notifyKeyFrameReceived(void) {}

// Platform.c needs these only to set the platform up, which these tests don't do
int initializePlatformSockets(void) { return 0; }
void cleanupPlatformSockets(void) {}
void enterLowLatencyMode(void) {}
void exitLowLatencyMode(void) {}

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

// No frame to take. Frame numbers use all 32 bits, so this is outside them.
#define NONE (-1)

static char lastResumeLog[256];

static void testLog(const char* format, ...) {
    char message[256];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (strncmp(message, "Video resumed", 13) == 0) {
        memcpy(lastResumeLog, message, sizeof(lastResumeLog));
    }
}

static const uint8_t picture[] = { 0xA1, 0xB2, 0xC3, 0xD4 };

static uint32_t submitted[32];
static int submittedCount;

static int recordSubmit(PDECODE_UNIT decodeUnit) {
    if (submittedCount < (int)(sizeof(submitted) / sizeof(submitted[0]))) {
        submitted[submittedCount++] = (uint32_t)decodeUnit->frameNumber;
    }
    return DR_OK;
}

static uint32_t streamPacketIndex;
static uint64_t receiveTimeUs;

static void startStream(int capabilities) {
    memset(&StreamConfig, 0, sizeof(StreamConfig));
    memset(&VideoCallbacks, 0, sizeof(VideoCallbacks));
    memset(&ListenerCallbacks, 0, sizeof(ListenerCallbacks));

    // Sunshine reports itself as GFE 7.1.431 with a negative last component
    AppVersionQuad[0] = 7;
    AppVersionQuad[1] = 1;
    AppVersionQuad[2] = 431;
    AppVersionQuad[3] = -1;

    // AV1 frames are passed through without parsing NAL units, so a test frame can be any bytes
    NegotiatedVideoFormat = VIDEO_FORMAT_AV1_MAIN8;
    StreamConfig.packetSize = 1024;
    VideoCallbacks.capabilities = capabilities;
    if (capabilities & CAPABILITY_DIRECT_SUBMIT) {
        VideoCallbacks.submitDecodeUnit = recordSubmit;
    }
    ListenerCallbacks.logMessage = testLog;

    streamPacketIndex = 0;
    receiveTimeUs = 1000000;
    idrRequests = 0;
    submittedCount = 0;
    lastResumeLog[0] = 0;

    initializeVideoDepacketizer(StreamConfig.packetSize);
}

static void endStream(void) {
    stopVideoDepacketizer();
    destroyVideoDepacketizer();
}

// Sends one frame in one packet, laid out the way the receive thread hands packets on: the queue
// entry for the packet sits in the same allocation, after the packet.
static void sendFrame(uint32_t frameIndex, bool idr) {
    const size_t payloadLength = 8 + sizeof(picture);
    const size_t packetLength = sizeof(RTP_PACKET) + sizeof(NV_VIDEO_PACKET) + payloadLength;
    const size_t entryOffset = (packetLength + 15) & ~(size_t)15;
    char* buffer = (char*)calloc(1, entryOffset + sizeof(RTPV_QUEUE_ENTRY));
    PRTP_PACKET rtp = (PRTP_PACKET)buffer;
    PNV_VIDEO_PACKET video = (PNV_VIDEO_PACKET)(rtp + 1);
    uint8_t* payload = (uint8_t*)(video + 1);
    PRTPV_QUEUE_ENTRY entry = (PRTPV_QUEUE_ENTRY)(buffer + entryOffset);

    if (buffer == NULL) {
        CHECK(buffer != NULL);
        return;
    }

    rtp->header = 0x80;
    video->streamPacketIndex = (streamPacketIndex++ & 0xFFFFFF) << 8;
    video->frameIndex = frameIndex;
    video->flags = FLAG_SOF | FLAG_EOF | FLAG_CONTAINS_PIC_DATA;

    // Sunshine's 8 byte frame header: 0x01, host processing latency, frame type (1 P-frame,
    // 2 IDR frame), and the payload length including this header
    payload[0] = 0x01;
    payload[3] = idr ? 2 : 1;
    payload[4] = (uint8_t)payloadLength;
    payload[5] = (uint8_t)(payloadLength >> 8);
    memcpy(payload + 8, picture, sizeof(picture));

    entry->packet = rtp;
    entry->length = (int)packetLength;
    entry->receiveTimeUs = receiveTimeUs;
    entry->presentationTimeUs = receiveTimeUs;
    entry->rtpTimestamp = frameIndex * 1500;
    entry->isParity = false;
    receiveTimeUs += 16667;

    queueRtpPacket(entry);
}

static bool unitIsTestFrame(PDECODE_UNIT decodeUnit) {
    return decodeUnit->fullLength == (int)sizeof(picture) && decodeUnit->bufferList != NULL &&
           decodeUnit->bufferList->length == (int)sizeof(picture) &&
           memcmp(decodeUnit->bufferList->data, picture, sizeof(picture)) == 0;
}

// Takes the next frame the way a pull renderer polls for it, decodes it, and returns its number
static int64_t pollFrame(void) {
    VIDEO_FRAME_HANDLE handle;
    PDECODE_UNIT decodeUnit;

    if (!LiPollNextVideoFrame(&handle, &decodeUnit)) {
        return NONE;
    }

    int64_t frameNumber = (uint32_t)decodeUnit->frameNumber;
    CHECK(unitIsTestFrame(decodeUnit));
    LiCompleteVideoFrame(handle, DR_OK);
    return frameNumber;
}

// The same through LiWaitForNextVideoFrame(), which blocks until a frame is admitted
static int64_t waitFrame(void) {
    VIDEO_FRAME_HANDLE handle;
    PDECODE_UNIT decodeUnit;

    if (!LiWaitForNextVideoFrame(&handle, &decodeUnit)) {
        return NONE;
    }

    int64_t frameNumber = (uint32_t)decodeUnit->frameNumber;
    CHECK(unitIsTestFrame(decodeUnit));
    LiCompleteVideoFrame(handle, DR_OK);
    return frameNumber;
}

static int64_t peekFrame(void) {
    PDECODE_UNIT decodeUnit;

    if (!LiPeekNextVideoFrame(&decodeUnit)) {
        return NONE;
    }

    CHECK(unitIsTestFrame(decodeUnit));
    return (uint32_t)decodeUnit->frameNumber;
}

static void testFlowsWhenNothingIsHeld(void) {
    startStream(CAPABILITY_PULL_RENDERER);

    CHECK(LiIsVideoFlowing());
    sendFrame(1, true);
    sendFrame(2, false);
    CHECK(waitFrame() == 1);
    CHECK(pollFrame() == 2);
    CHECK(pollFrame() == NONE);

    endStream();
}

// A keyframe the host encoded at the old size can still be on its way when the host answers, and
// the new decoder must not take it for the new stream
static void testDelayedOldKeyframeIsRejected(void) {
    startStream(CAPABILITY_PULL_RENDERER);

    sendFrame(1, true);
    CHECK(waitFrame() == 1);

    // Held from before the request is sent
    LiHoldVideo();
    CHECK(!LiIsVideoFlowing());
    sendFrame(2, false);
    sendFrame(3, true);
    sendFrame(4, false);
    CHECK(pollFrame() == NONE);
    CHECK(LiGetPendingVideoFrames() == 0);

    // The host switched at frame 6, and the decoder was rebuilt for the new size
    LiResumeVideo(true, 6);
    CHECK(!LiIsVideoFlowing());
    sendFrame(5, true);
    CHECK(pollFrame() == NONE);
    sendFrame(6, true);
    sendFrame(7, false);
    CHECK(waitFrame() == 6);
    CHECK(LiIsVideoFlowing());
    CHECK(pollFrame() == 7);
    CHECK(pollFrame() == NONE);
    CHECK(strcmp(lastResumeLog, "Video resumed at frame 6 after dropping 4 frames\n") == 0);

    // Nothing the depacketizer decided on its own was involved
    CHECK(idrRequests == 0);

    endStream();
}

// While the decoder is being replaced nobody takes frames, so they pile up in the queue. The new
// decoder must not start on anything from before the switch.
static void testFramesQueuedDuringDecoderReplacement(void) {
    startStream(CAPABILITY_PULL_RENDERER);

    sendFrame(1, true);
    CHECK(pollFrame() == 1);

    LiHoldVideo();
    sendFrame(2, false);
    sendFrame(3, true);
    LiResumeVideo(true, 5);
    sendFrame(4, true);
    sendFrame(5, true);
    sendFrame(6, false);
    CHECK(LiGetPendingVideoFrames() == 5);

    // A peek already skips what the decoder would never get, and leaves the frame it shows queued
    CHECK(peekFrame() == 5);
    CHECK(peekFrame() == 5);
    CHECK(LiGetPendingVideoFrames() == 2);
    CHECK(!LiIsVideoFlowing());
    CHECK(pollFrame() == 5);
    CHECK(LiIsVideoFlowing());
    CHECK(pollFrame() == 6);
    CHECK(pollFrame() == NONE);

    endStream();
}

static void testDirectSubmit(void) {
    startStream(CAPABILITY_DIRECT_SUBMIT);

    sendFrame(1, true);
    LiHoldVideo();
    sendFrame(2, false);
    sendFrame(3, true);
    LiResumeVideo(true, 5);
    sendFrame(4, true);
    sendFrame(5, true);
    sendFrame(6, false);

    CHECK(submittedCount == 3);
    CHECK(submitted[0] == 1);
    CHECK(submitted[1] == 5);
    CHECK(submitted[2] == 6);
    CHECK(LiIsVideoFlowing());
    CHECK(strcmp(lastResumeLog, "Video resumed at frame 5 after dropping 3 frames\n") == 0);

    endStream();
}

// Without a first frame, the host didn't restart the stream, so any keyframe is good
static void testResumeWithoutFirstFrame(void) {
    startStream(CAPABILITY_PULL_RENDERER);

    sendFrame(1, true);
    CHECK(pollFrame() == 1);

    LiHoldVideo();
    sendFrame(2, false);
    LiResumeVideo(false, 0);
    sendFrame(3, false);
    sendFrame(4, true);
    sendFrame(5, false);
    CHECK(pollFrame() == 4);
    CHECK(pollFrame() == 5);

    endStream();
}

// A new resize starting before the keyframe of the last one arrived holds video again, and only
// the newest first frame counts
static void testHoldAgainBeforeKeyframe(void) {
    startStream(CAPABILITY_PULL_RENDERER);

    sendFrame(1, true);
    CHECK(pollFrame() == 1);

    LiHoldVideo();
    LiResumeVideo(true, 3);
    LiHoldVideo();
    sendFrame(2, false);
    sendFrame(3, true);
    CHECK(pollFrame() == NONE);
    LiResumeVideo(true, 5);
    sendFrame(4, true);
    sendFrame(5, true);
    CHECK(pollFrame() == 5);

    endStream();
}

// Moves the stream to a frame number far ahead with a keyframe, the way it continues after
// frames were lost
static void jumpTo(uint32_t frameIndex) {
    sendFrame(frameIndex, true);
    CHECK(pollFrame() == frameIndex);
}

static void walkToWrap(void) {
    sendFrame(1, true);
    CHECK(pollFrame() == 1);
    jumpTo(0x70000000);
    jumpTo(0xE0000000);
    jumpTo(0xFFFFFFF8);
}

// Frame numbers wrap around. Keyframes from just before the wrap are older than a first frame
// after it, even though their numbers are bigger.
static void testFirstFrameAfterWrap(void) {
    startStream(CAPABILITY_PULL_RENDERER);
    walkToWrap();

    LiHoldVideo();
    sendFrame(0xFFFFFFF9, false);
    LiResumeVideo(true, 2);
    for (uint32_t frame = 0xFFFFFFFA; frame != 2; frame++) {
        sendFrame(frame, frame == 0xFFFFFFFA || frame == 0xFFFFFFFF || frame == 1);
    }
    CHECK(pollFrame() == NONE);
    sendFrame(2, true);
    sendFrame(3, false);
    CHECK(pollFrame() == 2);
    CHECK(pollFrame() == 3);

    endStream();
}

// And a keyframe after the wrap is newer than a first frame just before it, even though its
// number is smaller
static void testKeyframeAfterWrap(void) {
    startStream(CAPABILITY_PULL_RENDERER);
    walkToWrap();

    LiHoldVideo();
    sendFrame(0xFFFFFFF9, true);
    sendFrame(0xFFFFFFFA, false);
    sendFrame(0xFFFFFFFB, false);
    sendFrame(0xFFFFFFFC, false);
    sendFrame(0xFFFFFFFD, false);
    // The keyframe the host switched with arrives before the decoder was rebuilt, so it is lost
    sendFrame(0xFFFFFFFE, true);
    CHECK(pollFrame() == NONE);
    LiResumeVideo(true, 0xFFFFFFFE);
    sendFrame(0xFFFFFFFF, false);
    sendFrame(0, false);
    CHECK(pollFrame() == NONE);
    sendFrame(1, true);
    CHECK(pollFrame() == 1);

    endStream();
}

static PLT_THREAD decoderThread;
static int64_t decoderThreadFrame;

static void decoderThreadProc(void* context) {
    decoderThreadFrame = waitFrame();
}

// The decoder thread of a pull renderer is blocked in LiWaitForNextVideoFrame() the whole time.
// Frames held back wake it up, but it must keep waiting instead of returning them.
static void testBlockedDecoderThread(void) {
    startStream(CAPABILITY_PULL_RENDERER);

    sendFrame(1, true);
    CHECK(pollFrame() == 1);

    LiHoldVideo();
    decoderThreadFrame = 0;
    CHECK(PltCreateThread("TestDecoder", decoderThreadProc, NULL, &decoderThread) == 0);
    sendFrame(2, false);
    sendFrame(3, true);
    PltSleepMs(50);
    LiResumeVideo(true, 5);
    sendFrame(4, true);
    PltSleepMs(50);
    sendFrame(5, true);
    PltJoinThread(&decoderThread);
    CHECK(decoderThreadFrame == 5);
    CHECK(LiIsVideoFlowing());

    endStream();
}

// Before the stream starts and after it ends there is no video to hold
static void testOutsideStream(void) {
    LiHoldVideo();
    LiResumeVideo(true, 5);
    CHECK(LiIsVideoFlowing());
}

static void testFrameComparison(void) {
    CHECK(ssStreamResizeFrameAtOrAfter(5, 5));
    CHECK(ssStreamResizeFrameAtOrAfter(6, 5));
    CHECK(!ssStreamResizeFrameAtOrAfter(4, 5));
    CHECK(ssStreamResizeFrameAtOrAfter(1, 0xFFFFFFFE));
    CHECK(!ssStreamResizeFrameAtOrAfter(0xFFFFFFFE, 1));
    CHECK(ssStreamResizeFrameAtOrAfter(0, 0xFFFFFFFF));
    CHECK(!ssStreamResizeFrameAtOrAfter(0xFFFFFFFF, 0));
}

int main(void) {
    testOutsideStream();
    testFrameComparison();
    testFlowsWhenNothingIsHeld();
    testDelayedOldKeyframeIsRejected();
    testFramesQueuedDuringDecoderReplacement();
    testDirectSubmit();
    testResumeWithoutFirstFrame();
    testHoldAgainBeforeKeyframe();
    testFirstFrameAfterWrap();
    testKeyframeAfterWrap();
    testBlockedDecoderThread();
    testOutsideStream();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("video hold tests passed\n");
    return 0;
}
