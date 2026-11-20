// Every ErrorCode must have a stable name and deliberate policy membership; adding a code without
// updating the policy tables fails here.
#include "core/error.h"

#include <QSet>
#include <QtTest>

#include <array>

using core::ErrorCode;

namespace {

constexpr std::array<ErrorCode, 30> kAllCodes {ErrorCode::SelectionBackendAbsent,
    ErrorCode::SelectionBackendDenied, ErrorCode::SelectionBackendFailed, ErrorCode::SelectionEmpty,
    ErrorCode::SelectionNotText, ErrorCode::SelectionProtected, ErrorCode::SelectionTooLarge,
    ErrorCode::SelectionTimeout, ErrorCode::SelectionCancelled, ErrorCode::AutomaticDisabled,
    ErrorCode::AutomaticPaused, ErrorCode::ConsentRequired, ErrorCode::ConsentStale,
    ErrorCode::ProviderNotConfigured, ErrorCode::ProviderAuthFailed, ErrorCode::ProviderRateLimited,
    ErrorCode::ProviderUnavailable, ErrorCode::ProviderNetwork, ErrorCode::ProviderTimeout,
    ErrorCode::ProviderBadResponse, ErrorCode::ProviderCancelled, ErrorCode::SettingsCorrupt,
    ErrorCode::SettingsInvalid, ErrorCode::SecretStoreUnavailable, ErrorCode::SecretNotFound,
    ErrorCode::CaptureDenied, ErrorCode::CaptureUnavailable, ErrorCode::OcrUnavailable,
    ErrorCode::OcrEmpty, ErrorCode::Internal};

}  // namespace

class ErrorPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void everyCodeHasStableName();
    void namesAreUnique();
    void silentCodesAreTheExpectedOnes();
    void retryableCodesAreTheExpectedOnes();
    void silentAndRetryableAreDisjoint();
    void consentAndConfigurationFailuresAreNotSilent();
};

void ErrorPolicyTest::everyCodeHasStableName() {
    for (const ErrorCode code : kAllCodes) {
        const char *name = core::stableName(code);
        QVERIFY2(name != nullptr, "null name");
        QVERIFY2(qstrlen(name) > 0, "empty name");
    }
}

void ErrorPolicyTest::namesAreUnique() {
    QSet<QString> names;
    for (const ErrorCode code : kAllCodes)
        names.insert(QString::fromLatin1(core::stableName(code)));
    QCOMPARE(names.size(), int(kAllCodes.size()));
}

void ErrorPolicyTest::silentCodesAreTheExpectedOnes() {
    // "Silent" means: no window, no toast, no dialog — normal outcomes and superseded work.
    const QSet<ErrorCode> expected {ErrorCode::SelectionEmpty, ErrorCode::SelectionNotText,
        ErrorCode::SelectionProtected, ErrorCode::SelectionCancelled, ErrorCode::ProviderCancelled,
        ErrorCode::AutomaticDisabled, ErrorCode::AutomaticPaused, ErrorCode::Cancelled};
    for (const ErrorCode code : kAllCodes)
        QCOMPARE(core::isSilent(code), expected.contains(code));
}

void ErrorPolicyTest::retryableCodesAreTheExpectedOnes() {
    const QSet<ErrorCode> expected {ErrorCode::SelectionTimeout, ErrorCode::SelectionBackendFailed,
        ErrorCode::ProviderTimeout, ErrorCode::ProviderNetwork, ErrorCode::ProviderUnavailable,
        ErrorCode::ProviderRateLimited, ErrorCode::ProviderBadResponse,
        ErrorCode::SecretStoreUnavailable, ErrorCode::CaptureUnavailable,
        ErrorCode::OcrUnavailable};
    for (const ErrorCode code : kAllCodes)
        QCOMPARE(core::isRetryable(code), expected.contains(code));
}

void ErrorPolicyTest::silentAndRetryableAreDisjoint() {
    for (const ErrorCode code : kAllCodes)
        QVERIFY2(!(core::isSilent(code) && core::isRetryable(code)),
            qPrintable(QStringLiteral("code %1 is both silent and retryable")
                           .arg(QString::fromLatin1(core::stableName(code)))));
}

void ErrorPolicyTest::consentAndConfigurationFailuresAreNotSilent() {
    // These must reach the user: silence here would hide the fact that nothing will be translated.
    const ErrorCode actionable[] = {ErrorCode::ConsentRequired, ErrorCode::ConsentStale,
        ErrorCode::ProviderNotConfigured, ErrorCode::SelectionBackendAbsent,
        ErrorCode::SelectionBackendDenied, ErrorCode::SecretNotFound, ErrorCode::SelectionTooLarge};
    for (const ErrorCode code : actionable) {
        QVERIFY2(!core::isSilent(code), core::stableName(code));
    }
}

QTEST_GUILESS_MAIN(ErrorPolicyTest)
#include "core_error_policy_test.moc"
