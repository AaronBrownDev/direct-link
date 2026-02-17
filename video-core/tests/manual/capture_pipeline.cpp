// capture_pipeline.cpp
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <csignal>
#include <atomic>
#include <string>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

// Global flag for graceful shutdown
std::atomic<bool> shouldExit(false);

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down...\n";
    shouldExit = true;
}

class CaptureEncodeTransmit {
private:
    // Capture
    AVFormatContext* captureCtx = nullptr;
    int videoStreamIdx = -1;
    
    // Encoder
    AVCodecContext* encoderCtx = nullptr;
    
    // Frame queue
    std::queue<AVFrame*> frameQueue;
    std::mutex queueMutex;
    
    // Threads
    std::thread captureThread;
    std::thread encodeThread;
    bool running = false;
    
public:
    bool initialize(const std::string& device, bool useGPU = false) {
        avdevice_register_all();
        
        // Setup capture
        const AVInputFormat* inputFmt = av_find_input_format("v4l2");
        if (!inputFmt) {
            std::cerr << "Could not find v4l2 input format\n";
            return false;
        }
        
        captureCtx = avformat_alloc_context();
        
        std::cout << "Opening device: " << device << "\n";
        if (avformat_open_input(&captureCtx, device.c_str(), 
                                inputFmt, nullptr) < 0) {
            std::cerr << "Could not open device: " << device << "\n";
            return false;
        }
        
        if (avformat_find_stream_info(captureCtx, nullptr) < 0) {
            std::cerr << "Could not find stream info\n";
            return false;
        }
        
        // Find video stream
        for (unsigned i = 0; i < captureCtx->nb_streams; i++) {
            if (captureCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIdx = i;
                break;
            }
        }
        
        if (videoStreamIdx == -1) {
            std::cerr << "No video stream found\n";
            return false;
        }
        
        auto* codecpar = captureCtx->streams[videoStreamIdx]->codecpar;
        std::cout << "Found video stream: " 
                  << codecpar->width << "x" << codecpar->height << "\n";
        
        // Setup encoder based on flag
        const AVCodec* encoder = nullptr;
        
        if (useGPU) {
            std::cout << "Attempting to use GPU encoder (NVENC)...\n";
            encoder = avcodec_find_encoder_by_name("h264_nvenc");
            if (!encoder) {
                std::cerr << "NVENC not available, falling back to CPU\n";
                encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
            }
        } 
        else {
            std::cout << "Using CPU encoder (libx264)...\n";
            encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
        
        if (!encoder) {
            std::cerr << "No H.264 encoder found\n";
            return false;
        }
        
        std::cout << "Selected encoder: " << encoder->name 
                  << " (" << encoder->long_name << ")\n";
        
        encoderCtx = avcodec_alloc_context3(encoder);
        
        encoderCtx->width = codecpar->width;
        encoderCtx->height = codecpar->height;
        encoderCtx->time_base = {1, 30};  // 30 fps for CPU
        encoderCtx->framerate = {30, 1};
        encoderCtx->bit_rate = 2000000;   // 2 Mbps
        encoderCtx->gop_size = 30;
        encoderCtx->max_b_frames = 0;     // No B-frames for lower latency
        
        // Set encoder-specific settings
        if (std::string(encoder->name).find("nvenc") != std::string::npos) {
            // GPU (NVENC) settings
            encoderCtx->pix_fmt = AV_PIX_FMT_NV12;
            av_opt_set(encoderCtx->priv_data, "preset", "p1", 0);
            av_opt_set(encoderCtx->priv_data, "tune", "ull", 0);
            av_opt_set(encoderCtx->priv_data, "zerolatency", "1", 0);
            std::cout << "Configured for GPU encoding (NV12, p1 preset)\n";
        } 
        else {
            // CPU (libx264) settings
            encoderCtx->pix_fmt = AV_PIX_FMT_YUV420P;
            av_opt_set(encoderCtx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(encoderCtx->priv_data, "tune", "zerolatency", 0);
            std::cout << "Configured for CPU encoding (YUV420P, ultrafast preset)\n";
        }
        
        if (avcodec_open2(encoderCtx, encoder, nullptr) < 0) {
            std::cerr << "Could not open encoder\n";
            return false;
        }
        
        std::cout << "Encoder initialized successfully\n";
        return true;
    }
    
    void start() {
        running = true;
        captureThread = std::thread(&CaptureEncodeTransmit::captureLoop, this);
        encodeThread = std::thread(&CaptureEncodeTransmit::encodeLoop, this);
        std::cout << "Started capture and encode threads\n";
    }
    
    void stop() {
        std::cout << "Stopping threads...\n";
        running = false;
        if (captureThread.joinable()) captureThread.join();
        if (encodeThread.joinable()) encodeThread.join();
        std::cout << "Threads stopped\n";
    }
    
    ~CaptureEncodeTransmit() {
        stop();
        if (encoderCtx) avcodec_free_context(&encoderCtx);
        if (captureCtx) avformat_close_input(&captureCtx);
    }
    
private:
    void captureLoop() {
        AVPacket* packet = av_packet_alloc();
        int frameCount = 0;
        
        while (running && !shouldExit) {
            if (av_read_frame(captureCtx, packet) >= 0) {
                if (packet->stream_index == videoStreamIdx) {
                    AVFrame* frame = av_frame_alloc();
                    // TODO: Decode if needed, or if raw just convert packet to frame
                    
                    std::lock_guard<std::mutex> lock(queueMutex);
                    frameQueue.push(frame);
                    frameCount++;
                    
                    if (frameCount % 60 == 0) {
                        std::cout << "Captured " << frameCount << " frames\n";
                    }
                }
                av_packet_unref(packet);
            }
        }
        
        av_packet_free(&packet);
        std::cout << "Capture thread exiting (captured " << frameCount << " frames)\n";
    }
    
    void encodeLoop() {
        AVPacket* packet = av_packet_alloc();
        int packetCount = 0;
        
        while (running && !shouldExit) {
            AVFrame* frame = nullptr;
            
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (!frameQueue.empty()) {
                    frame = frameQueue.front();
                    frameQueue.pop();
                }
            }
            
            if (frame) {
                // Encode frame
                avcodec_send_frame(encoderCtx, frame);
                
                while (avcodec_receive_packet(encoderCtx, packet) == 0) {
                    // Send packet over network (WebRTC, custom protocol, etc.)
                    transmitPacket(packet);
                    packetCount++;
                    av_packet_unref(packet);
                }
                
                av_frame_free(&frame);
            } 
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        av_packet_free(&packet);
        std::cout << "Encode thread exiting (encoded " << packetCount << " packets)\n";
    }
    
    void transmitPacket(AVPacket* packet) {
        // Send over network to SFU/client
        // For now just print stats
        static int count = 0;
        if (++count % 60 == 0) {
            std::cout << "Transmitted " << count << " packets (latest: " 
                      << packet->size << " bytes)\n";
        }
    }
};

int main(int argc, char* argv[]) {
    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Parse arguments
    std::string device = "/dev/video0";
    bool useGPU = false;  // Default to CPU
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--gpu" || arg == "-g") {
            useGPU = true;
        } 
        else if (arg == "--cpu" || arg == "-c") {
            useGPU = false;
        } 
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options] [device]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --cpu, -c     Use CPU encoding (libx264) [default]\n";
            std::cout << "  --gpu, -g     Use GPU encoding (NVENC)\n";
            std::cout << "  --help, -h    Show this help\n";
            std::cout << "\nExamples:\n";
            std::cout << "  " << argv[0] << "                    # CPU, /dev/video0\n";
            std::cout << "  " << argv[0] << " --cpu /dev/video1  # CPU, /dev/video1\n";
            std::cout << "  " << argv[0] << " --gpu /dev/video0  # GPU, /dev/video0\n";
            return 0;
        } 
        else if (arg[0] != '-') {
            device = arg;
        }
    }
    
    std::cout << "=== DirectLink Camera Capture Pipeline ===\n";
    std::cout << "Device: " << device << "\n";
    std::cout << "Encoder: " << (useGPU ? "GPU (NVENC)" : "CPU (libx264)") << "\n";
    std::cout << "Press Ctrl+C to stop\n\n";
    
    // Create pipeline
    CaptureEncodeTransmit pipeline;
    
    // Initialize with encoder choice
    if (!pipeline.initialize(device, useGPU)) {
        std::cerr << "Failed to initialize pipeline\n";
        return 1;
    }
    
    // Start capture and encoding
    pipeline.start();
    
    // Run until signal received
    while (!shouldExit) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Stop gracefully
    pipeline.stop();
    
    std::cout << "\nPipeline stopped successfully\n";
    return 0;
}