#pragma once

#include <cstdint>
#include <string_view>


namespace networking {
enum class Result : std::uint8_t {
    Success,
    ErrorInvalidParameter,
    ErrorPipelineFailed,
    ErrorConnectionFailed,
    ErrorAuthFailed,
    ErrorNotInitialized,
};

inline std::string_view resultToString(Result result) {
    switch (result) {
    case Result::Success:
        return "Success";
    case Result::ErrorInvalidParameter:
        return "Invalid parameter";
    case Result::ErrorPipelineFailed:
        return "Pipeline failed";
    case Result::ErrorConnectionFailed:
        return "Connection failed";
    case Result::ErrorAuthFailed:
        return "Authentication failed";
    case Result::ErrorNotInitialized:
        return "Not initialized";
    default:
        return "Unknown error";
    }
}
} // namespace networking