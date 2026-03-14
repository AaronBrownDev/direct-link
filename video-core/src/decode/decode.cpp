#include "../../include/decode/decoder.hpp"
#include "../../include/decode/nvdec_decoder.hpp"
#include "../../include/decode/software_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace videoCore::decode {
std::unique_ptr<Decoder> createDecoder() {
    const AVCodec *codec = avcodec_find_decoder_by_name("h264_cuvid");
    if (codec != nullptr) {
        return std::make_unique<NvdecDecoder>();
    }
    return std::make_unique<SoftwareDecoder>();
}

} // namespace videoCore::decode