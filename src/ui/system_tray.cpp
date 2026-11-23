#include "ui/system_tray.h"

#include <QAction>

namespace ui {

SystemTray::SystemTray(const QIcon &icon)
    : m_icon(icon, this) {
    m_openAction = m_menu.addAction(QString());
    m_quitAction = m_menu.addAction(QString());
    retranslate();
    connect(m_openAction, &QAction::triggered, this, &SystemTray::openRequested);
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::quitRequested);
    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    m_icon.setToolTip(QStringLiteral("SnipLingo"));
    m_icon.setContextMenu(&m_menu);
    // Qt can register the icon if the desktop starts its tray host later.
    m_icon.show();
}

void SystemTray::retranslate() {
    m_openAction->setText(tr("Open SnipLingo"));
    m_quitAction->setText(tr("Quit"));
}

}  // namespace ui
