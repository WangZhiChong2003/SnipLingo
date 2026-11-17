#include "core/error.h"

namespace core {

const char *stableName(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::SelectionBackendAbsent:
        return "SelectionBackendAbsent";
    case ErrorCode::SelectionBackendDenied:
        return "SelectionBackendDenied";
    case ErrorCode::SelectionBackendFailed:
        return "SelectionBackendFailed";
    case ErrorCode::SelectionEmpty:
        return "SelectionEmpty";
    case ErrorCode::SelectionNotText:
        return "SelectionNotText";
    case ErrorCode::SelectionProtected:
        return "SelectionProtected";
    case ErrorCode::SelectionTooLarge:
        return "SelectionTooLarge";
    case ErrorCode::SelectionTimeout:
        return "SelectionTimeout";
    case ErrorCode::SelectionCancelled:
        return "SelectionCancelled";
    case ErrorCode::AutomaticDisabled:
        return "AutomaticDisabled";
    case ErrorCode::AutomaticPaused:
        return "AutomaticPaused";
    case ErrorCode::ConsentRequired:
        return "ConsentRequired";
    case ErrorCode::ConsentStale:
        return "ConsentStale";
    case ErrorCode::ProviderNotConfigured:
        return "ProviderNotConfigured";
    case ErrorCode::ProviderAuthFailed:
        return "ProviderAuthFailed";
    case ErrorCode::ProviderRateLimited:
        return "ProviderRateLimited";
    case ErrorCode::ProviderUnavailable:
        return "ProviderUnavailable";
    case ErrorCode::ProviderNetwork:
        return "ProviderNetwork";
    case ErrorCode::ProviderTimeout:
        return "ProviderTimeout";
    case ErrorCode::ProviderBadResponse:
        return "ProviderBadResponse";
    case ErrorCode::ProviderCancelled:
        return "ProviderCancelled";
    case ErrorCode::SettingsCorrupt:
        return "SettingsCorrupt";
    case ErrorCode::SettingsInvalid:
        return "SettingsInvalid";
    case ErrorCode::SecretStoreUnavailable:
        return "SecretStoreUnavailable";
    case ErrorCode::SecretNotFound:
        return "SecretNotFound";
    case ErrorCode::CaptureDenied:
        return "CaptureDenied";
    case ErrorCode::CaptureUnavailable:
        return "CaptureUnavailable";
    case ErrorCode::OcrUnavailable:
        return "OcrUnavailable";
    case ErrorCode::OcrEmpty:
        return "OcrEmpty";
    case ErrorCode::Cancelled:
        return "Cancelled";
    case ErrorCode::Internal:
        return "Internal";
    }
    return "Unknown";
}

}  // namespace core
