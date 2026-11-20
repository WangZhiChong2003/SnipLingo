#include "ui/system_tray.h"

#include <QAction>

namespace ui {

SystemTray::SystemTray(const QIcon &icon)
    : m_icon(icon, this) {
    connect(m_menu.addAction(tr("Open SnipLingo")), &QAction::triggered, this,
        &SystemTray::openRequested);
    connect(m_menu.addAction(tr("Quit")), &QAction::triggered, this, &SystemTray::quitRequested);
    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    m_icon.setToolTip(QStringLiteral("SnipLingo"));
    m_icon.setContextMenu(&m_menu);
    // Qt can register the icon if the desktop starts its tray host later.
    m_icon.show();
}

}  // namespace ui
