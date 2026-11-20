// Bridges the coordinator's result view to QML. All wording lives in QML:
// this class exposes keys and plain text, never sentences, and never logs selection content.
#pragma once

#include "core/delay.h"

#include <QObject>
#include <QString>

#include "coordinator/request_coordinator.h"

namespace ui {

class ResultController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QString translatedText READ translatedText NOTIFY changed)
    Q_PROPERTY(QString originalText READ originalText NOTIFY changed)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY changed)
    Q_PROPERTY(QString sourceLanguage READ sourceLanguage NOTIFY changed)
    Q_PROPERTY(QString targetLanguage READ targetLanguage NOTIFY changed)
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY changed)
    Q_PROPERTY(QString errorKey READ errorKey NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    Q_PROPERTY(bool originalVisible READ originalVisible NOTIFY changed)
    Q_PROPERTY(bool originalCollapsed READ originalCollapsed NOTIFY changed)

public:
    explicit ResultController(
        coordinator::RequestCoordinator *coordinator, QObject *parent = nullptr);

    [[nodiscard]] bool visible() const;
    [[nodiscard]] QString translatedText() const;
    [[nodiscard]] QString originalText() const;
    [[nodiscard]] QString providerLabel() const;
    [[nodiscard]] QString sourceLanguage() const;
    [[nodiscard]] QString targetLanguage() const;
    // "hidden" | "loading" | "streaming" | "done" | "error"
    [[nodiscard]] QString stateKey() const;
    // Stable ErrorCode name, so QML can pick a translation without parsing prose.
    [[nodiscard]] QString errorKey() const;
    [[nodiscard]] bool canRetry() const;
    [[nodiscard]] bool originalVisible() const;
    [[nodiscard]] bool originalCollapsed() const;

public slots:
    void copyResult();
    void dismiss();
    void dismissOnFocusLoss();
    void retry();
    void toggleOriginal();

signals:
    void changed();

private:
    void onViewChanged(const coordinator::ResultView &view);
    void scheduleNotify();

    coordinator::RequestCoordinator *m_coordinator = nullptr;
    coordinator::ResultView m_view;
    core::CancellableDelay m_throttle;
    bool m_originalCollapsed = true;
    bool m_keepVisible = false;
};

}  // namespace ui
