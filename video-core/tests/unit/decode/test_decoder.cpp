#include "../../../include/decode/decoder.hpp"
#include "../../../include/decode/nvdec_decoder.hpp"
#include "../../../include/decode/software_decoder.hpp"
#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

using namespace videoCore;
using namespace videoCore::decode;

// ---------------------------------------------------------------------------
// Helpers — encode a few blank frames into H.264 packets for test input
// ---------------------------------------------------------------------------

struct EncodedTestData {
    std::vector<std::pair<std::vector<uint8_t>, int64_t>>
        packets; // {data, pts}
    int width = 320;
    int height = 240;
    int framerate = 30;
};

static EncodedTestData generateTestPackets(int frameCount = 5) {
    EncodedTestData result;

    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    EXPECT_NE(encoder, nullptr);

    AVCodecContext *ctx = avcodec_alloc_context3(encoder);
    EXPECT_NE(ctx, nullptr);

    ctx->width = result.width;
    ctx->height = result.height;
    ctx->time_base = {.num = 1, .den = result.framerate};
    ctx->framerate = {.num = result.framerate, .den = 1};
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->gop_size = 1; // every frame is a keyframe — simplifies decoder test

    // Use ultrafast + zerolatency so encode doesn't take forever in tests
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);

    EXPECT_EQ(avcodec_open2(ctx, encoder, &opts), 0);
    av_dict_free(&opts);

    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = result.width;
    frame->height = result.height;
    av_frame_get_buffer(frame, 0);

    AVPacket *pkt = av_packet_alloc();

    for (int i = 0; i < frameCount; i++) {
        av_frame_make_writable(frame);

        // Fill YUV420P frame with blank data (gray)
        memset(frame->data[0], 0,
               static_cast<long>(frame->linesize[0]) * result.height);
        memset(frame->data[1], 128, frame->linesize[1] * result.height / 2);
        memset(frame->data[2], 128, frame->linesize[2] * result.height / 2);
        frame->pts = i;

        if (avcodec_send_frame(ctx, frame) == 0) {
            while (avcodec_receive_packet(ctx, pkt) == 0) {
                std::vector<uint8_t> data(pkt->data, pkt->data + pkt->size);
                result.packets.emplace_back(
                    std::vector<uint8_t>(pkt->data, pkt->data + pkt->size),
                    pkt->pts);
                av_packet_unref(pkt);
            }
        }
    }

    // Flush
    avcodec_send_frame(ctx, nullptr);
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        std::vector<uint8_t> data(pkt->data, pkt->data + pkt->size);
        result.packets.emplace_back(
            std::vector<uint8_t>(pkt->data, pkt->data + pkt->size), pkt->pts);
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);

    return result;
}

// ---------------------------------------------------------------------------
// SoftwareDecoder tests
// ---------------------------------------------------------------------------

TEST(SoftwareDecoderTest, InitializeSucceeds) {
    SoftwareDecoder decoder;
    auto result = decoder.initialize([](std::unique_ptr<Frame>) {});
    EXPECT_EQ(result, Result::Success);
}

TEST(SoftwareDecoderTest, InitializeTwiceIsIdempotent) {
    SoftwareDecoder decoder;
    EXPECT_EQ(decoder.initialize([](std::unique_ptr<Frame>) {}),
              Result::Success);
    // Second initialize — should not crash or leak
    EXPECT_EQ(decoder.initialize([](std::unique_ptr<Frame>) {}),
              Result::Success);
}

TEST(SoftwareDecoderTest, DecodePacketProducesFrames) {
    SoftwareDecoder decoder;

    std::vector<std::unique_ptr<Frame>> received_frames;
    ASSERT_EQ(decoder.initialize([&](std::unique_ptr<Frame> frame) {
        received_frames.push_back(std::move(frame));
    }),
              Result::Success);

    auto test_data = generateTestPackets(5);
    ASSERT_FALSE(test_data.packets.empty());

    for (auto &packet_data : test_data.packets) {
        auto pkt = std::make_unique<Packet>();
        pkt->packet.reset(av_packet_alloc());
        av_new_packet(pkt->packet.get(),
                      static_cast<int>(packet_data.first.size()));
        memcpy(pkt->packet->data, packet_data.first.data(),
               packet_data.first.size());
        decoder.decodePacket(std::move(pkt));
    }

    EXPECT_GT(received_frames.size(), 0u);
}

TEST(SoftwareDecoderTest, DecodedFrameHasCorrectDimensions) {
    SoftwareDecoder decoder;

    std::unique_ptr<Frame> first_frame;
    ASSERT_EQ(decoder.initialize([&](std::unique_ptr<Frame> frame) {
        if (first_frame == nullptr) {
            first_frame = std::move(frame);
        }
    }),
              Result::Success);

    auto test_data = generateTestPackets(3);
    for (auto &packet_data : test_data.packets) {
        auto pkt = std::make_unique<Packet>();
        pkt->packet.reset(av_packet_alloc());
        av_new_packet(pkt->packet.get(),
                      static_cast<int>(packet_data.first.size()));
        memcpy(pkt->packet->data, packet_data.first.data(),
               packet_data.first.size());
        decoder.decodePacket(std::move(pkt));
    }

    ASSERT_NE(first_frame, nullptr);
    EXPECT_EQ(first_frame->width, 320);
    EXPECT_EQ(first_frame->height, 240);
}

TEST(SoftwareDecoderTest, DecodedFrameHasNonNegativePts) {
    SoftwareDecoder decoder;

    std::vector<int64_t> pts_values;
    ASSERT_EQ(decoder.initialize([&](std::unique_ptr<Frame> frame) {
        pts_values.push_back(frame->pts);
    }),
              Result::Success);

    auto test_data = generateTestPackets(5);
    for (auto &packet_data : test_data.packets) {
        auto pkt = std::make_unique<Packet>();
        pkt->packet.reset(av_packet_alloc());
        av_new_packet(pkt->packet.get(),
                      static_cast<int>(packet_data.first.size()));
        memcpy(pkt->packet->data, packet_data.first.data(),
               packet_data.first.size());
        pkt->packet->pts = packet_data.second;
        pkt->packet->dts = packet_data.second;
        decoder.decodePacket(std::move(pkt));
    }

    ASSERT_GE(pts_values.size(), 1u);
    for (int64_t pts : pts_values) {
        EXPECT_GE(pts, 0) << "PTS should never be negative";
    }
}

TEST(SoftwareDecoderTest, DecodeBeforeInitializeDoesNotCrash) {
    SoftwareDecoder decoder;
    auto pkt = std::make_unique<Packet>();
    pkt->packet.reset(av_packet_alloc());
    av_new_packet(pkt->packet.get(), 64);
    memset(pkt->packet->data, 0, 64);
    // Should return early cleanly, not crash
    EXPECT_NO_THROW(decoder.decodePacket(std::move(pkt)));
}

TEST(SoftwareDecoderTest, StopThenDecodeDoesNotCrash) {
    SoftwareDecoder decoder;
    ASSERT_EQ(decoder.initialize([](std::unique_ptr<Frame>) {}),
              Result::Success);
    decoder.stop();

    auto pkt = std::make_unique<Packet>();
    pkt->packet.reset(av_packet_alloc());
    av_new_packet(pkt->packet.get(), 64);
    memset(pkt->packet->data, 0, 64);
    EXPECT_NO_THROW(decoder.decodePacket(std::move(pkt)));
}

// ---------------------------------------------------------------------------
// NvdecDecoder tests
// ---------------------------------------------------------------------------

TEST(NvdecDecoderTest, InitializeEitherSucceedsOrReturnsDeviceNotFound) {
    NvdecDecoder decoder;
    auto result = decoder.initialize([](std::unique_ptr<Frame>) {});
    EXPECT_TRUE(result == Result::Success ||
                result == Result::ErrorDeviceNotFound);
}

TEST(NvdecDecoderTest, InitializeFailsGracefullyWithNoGpu) {
    NvdecDecoder decoder;
    auto result = decoder.initialize([](std::unique_ptr<Frame>) {});
    if (result == Result::ErrorDeviceNotFound) {
        // No GPU — verify stop and destructor don't crash
        EXPECT_NO_THROW(decoder.stop());
    }
}

// ---------------------------------------------------------------------------
// createDecoder factory tests
// ---------------------------------------------------------------------------

TEST(CreateDecoderTest, ReturnsNonNull) {
    auto decoder = createDecoder();
    EXPECT_NE(decoder, nullptr);
}

TEST(CreateDecoderTest, ReturnedDecoderInitializesSuccessfully) {
    auto decoder = createDecoder();
    ASSERT_NE(decoder, nullptr);
    auto result = decoder->initialize([](std::unique_ptr<Frame>) {});
    EXPECT_EQ(result, Result::Success);
}

TEST(CreateDecoderTest, ReturnedDecoderCanDecodePackets) {
    auto decoder = createDecoder();
    ASSERT_NE(decoder, nullptr);

    std::vector<std::unique_ptr<Frame>> received_frames;
    decoder->initialize([&](std::unique_ptr<Frame> frame) {
        received_frames.push_back(std::move(frame));
    });

    auto test_data = generateTestPackets(5);
    for (auto &packet_data : test_data.packets) {
        auto pkt = std::make_unique<Packet>();
        pkt->packet.reset(av_packet_alloc());
        av_new_packet(pkt->packet.get(),
                      static_cast<int>(packet_data.first.size()));
        memcpy(pkt->packet->data, packet_data.first.data(),
               packet_data.first.size());
        decoder->decodePacket(std::move(pkt));
    }

    EXPECT_GT(received_frames.size(), 0u);
}