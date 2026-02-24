#include "../../include/encode/encoder_config.hpp"
#include "../../include/encode/encoder.hpp"
#include "../../include/encode/software_encoder.hpp"
#include "../../include/encode/nvenc_encoder.hpp"

namespace videoCore::encode {

std::unique_ptr<Encoder> createEncoder(const EncoderConfig& config) {
    if (config.type == EncoderConfig::Type::Software) {
        return std::make_unique<SoftwareEncoder>(config);
    } 
    else if (config.type == EncoderConfig::Type::Hardware) {
        return std::make_unique<NVENCEncoder>(config);
    }
    return nullptr;
}

}