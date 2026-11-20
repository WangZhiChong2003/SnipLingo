// Fake accessibility service for integration-testing the AT-SPI backend without a desktop.
//
// One QDBusVirtualObject serves every path the backend touches, because AT-SPI puts different
// interfaces (org.a11y.Bus, Event.Listener, Registry, Accessible, Text) on the same connection and
// a plain QObject can only declare one interface via Q_CLASSINFO.
//
//   org.a11y.Bus.GetAddress
//   org.a11y.atspi.Event.Listener.RegisterEvent / DeregisterEvent
//   org.a11y.atspi.Registry.GetDesktop
//   org.a11y.atspi.Accessible.{GetName,GetRoleName,GetInterfaces,GetApplication,GetChildCount}
//   org.a11y.atspi.Text.{GetNSelections,GetSelection,GetText,GetCharacterCount,GetRangeExtents}
//   org.freedesktop.DBus.Properties.Get
//   the org.a11y.atspi.Event.Object.TextSelectionChanged signal
#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QObject>
#include <QStringList>

struct FakeAccessibleRef {
    QString bus;
    QString path = QStringLiteral("/");
};
Q_DECLARE_METATYPE(FakeAccessibleRef)
QDBusArgument &operator<<(QDBusArgument &argument, const FakeAccessibleRef &reference);
const QDBusArgument &operator>>(const QDBusArgument &argument, FakeAccessibleRef &reference);

struct FakeTextRange {
    int start = 0;
    int end = 0;
};
Q_DECLARE_METATYPE(FakeTextRange)
QDBusArgument &operator<<(QDBusArgument &argument, const FakeTextRange &range);
const QDBusArgument &operator>>(const QDBusArgument &argument, FakeTextRange &range);

struct FakeRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};
Q_DECLARE_METATYPE(FakeRect)
QDBusArgument &operator<<(QDBusArgument &argument, const FakeRect &rect);
const QDBusArgument &operator>>(const QDBusArgument &argument, FakeRect &rect);

class FakeAtspiService : public QDBusVirtualObject {
    Q_OBJECT
public:
    explicit FakeAtspiService(const QString &busAddress, QObject *parent = nullptr);
    ~FakeAtspiService() override;

    [[nodiscard]] bool ready() const { return m_ready; }

    [[nodiscard]] QString lastError() const { return m_error; }

    void setSelection(
        const QString &text, int start, int end, const QString &roleName = QStringLiteral("text"));
    void clearSelection();
    bool emitSelectionChanged(QString *error);

    [[nodiscard]] int registerEventCalls() const { return m_registerEventCalls; }

    [[nodiscard]] int deregisterEventCalls() const { return m_deregisterEventCalls; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;
    QString introspect(const QString &path) const override;

private:
    QDBusMessage dispatch(const QDBusMessage &message) const;
    [[nodiscard]] QString textSlice(int from, int to) const;

    bool m_ready = false;
    QString m_error;
    QString m_address;
    QDBusConnection m_bus;
    QDBusConnection m_appBus;
    QStringList m_paths;

    QString m_appName = QStringLiteral("fake-editor");
    QString m_roleName = QStringLiteral("text");
    QString m_textPath = QStringLiteral("/org/a11y/atspi/accessible/1");
    QString m_rootPath = QStringLiteral("/org/a11y/atspi/accessible/root");
    int m_objectVersion = 0;
    QString m_text = QStringLiteral("hello world");
    int m_start = 0;
    int m_end = 0;
    bool m_hasSelection = true;
    mutable int m_registerEventCalls = 0;
    mutable int m_deregisterEventCalls = 0;
};
