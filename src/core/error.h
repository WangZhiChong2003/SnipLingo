// Error vocabulary shared by all modules.
//
// `Error::detail` is diagnostic only: it must never contain selected text, recognized text,
// credentials, window titles or captured-image paths.
#pragma once

#include <QString>
#include <QtGlobal>

namespace core {

enum class ErrorCode {
    // Selection acquisition
    SelectionBackendAbsent,
    SelectionBackendDenied,
    SelectionBackendFailed,
    SelectionEmpty,
    SelectionNotText,
    SelectionProtected,
    SelectionTooLarge,
    SelectionTimeout,
    SelectionCancelled,
    // Consent and mode
    AutomaticDisabled,
    AutomaticPaused,
    ConsentRequired,
    ConsentStale,
    // Provider
    ProviderNotConfigured,
    ProviderAuthFailed,
    ProviderRateLimited,
    ProviderUnavailable,
    ProviderNetwork,
    ProviderTimeout,
    ProviderBadResponse,
    ProviderCancelled,
    // Configuration, secrets, storage
    SettingsCorrupt,
    SettingsInvalid,
    SecretStoreUnavailable,
    SecretNotFound,
    // Capture and OCR (declared now so the taxonomy does not churn later)
    CaptureDenied,
    CaptureUnavailable,
    OcrUnavailable,
    OcrEmpty,
    // Generic
    Cancelled,
    Internal
};

struct Error {
    ErrorCode code {ErrorCode::Internal};
    QString detail;
};

// True when the code must not produce a window, a dialog or a notice: it is either a normal
// "nothing to translate" outcome or a superseded request.
constexpr bool isSilent(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::SelectionEmpty:
    case ErrorCode::SelectionNotText:
    case ErrorCode::SelectionProtected:
    case ErrorCode::SelectionCancelled:
    case ErrorCode::ProviderCancelled:
    case ErrorCode::AutomaticDisabled:
    case ErrorCode::AutomaticPaused:
    case ErrorCode::Cancelled:
        return true;
    default:
        return false;
    }
}

// True when offering a manual "Retry" action makes sense.
constexpr bool isRetryable(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::SelectionTimeout:
    case ErrorCode::SelectionBackendFailed:
    case ErrorCode::ProviderTimeout:
    case ErrorCode::ProviderNetwork:
    case ErrorCode::ProviderUnavailable:
    case ErrorCode::ProviderRateLimited:
    case ErrorCode::ProviderBadResponse:
    case ErrorCode::SecretStoreUnavailable:
    case ErrorCode::CaptureUnavailable:
    case ErrorCode::OcrUnavailable:
        return true;
    default:
        return false;
    }
}

// Stable machine-readable name for logs and diagnostics. Never localized, never reworded.
const char *stableName(ErrorCode code) noexcept;

}  // namespace core
