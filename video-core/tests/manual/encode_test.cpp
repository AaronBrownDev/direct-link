#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

int main() {
    const int width = 640;
    const int height = 480;
    const char* filename = "output.h264";

    // Find H.264 encoder
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "H.264 encoder not found\n";
        return 1;
    }

    // Allocate codec context
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        std::cerr << "Could not allocate codec context\n";
        return 1;
    }

    // Set encoding parameters
    ctx->bit_rate = 400000;
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = {1, 25}; // 25 fps
    ctx->framerate = {25, 1};
    ctx->gop_size = 10; // Group of pictures
    ctx->max_b_frames = 1;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    // Set H.264 preset for better compression
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
    }

    // Open codec
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec\n";
        avcodec_free_context(&ctx);
        return 1;
    }

    // Allocate frame
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Could not allocate frame\n";
        avcodec_free_context(&ctx);
        return 1;
    }

    frame->format = ctx->pix_fmt;
    frame->width = ctx->width;
    frame->height = ctx->height;

    if (av_frame_get_buffer(frame, 32) < 0) {
        std::cerr << "Could not allocate frame buffer\n";
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return 1;
    }

    // Fill frame with a solid color (Y=128, U=64, V=192)
    if (av_frame_make_writable(frame) < 0) {
        std::cerr << "Frame not writable\n";
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return 1;
    }

    for (int y = 0; y < height; y++) {
        std::memset(frame->data[0] + y * frame->linesize[0], 128, width); // Y
    }
    for (int y = 0; y < height / 2; y++) {
        std::memset(frame->data[1] + y * frame->linesize[1], 64, width / 2);  // U
        std::memset(frame->data[2] + y * frame->linesize[2], 192, width / 2); // V
    }

    // Prepare output file
    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile) {
        std::cerr << "Could not open output file\n";
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return 1;
    }

    // Encode the frame
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Could not allocate packet\n";
        outfile.close();
        av_frame_free(&frame);
        avcodec_free_context(&ctx);
        return 1;
    }

    frame->pts = 0; // Presentation timestamp
    if (avcodec_send_frame(ctx, frame) < 0) {
        std::cerr << "Error sending frame to encoder\n";
    }

    while (avcodec_receive_packet(ctx, pkt) == 0) {
        outfile.write(reinterpret_cast<char*>(pkt->data), pkt->size);
        av_packet_unref(pkt);
    }

    // Flush encoder
    avcodec_send_frame(ctx, nullptr);
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        outfile.write(reinterpret_cast<char*>(pkt->data), pkt->size);
        av_packet_unref(pkt);
    }

    // Cleanup
    outfile.close();
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);

    std::cout << "Encoded test frame saved to " << filename << "\n";
    return 0;
}