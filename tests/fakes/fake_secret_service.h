// Fake Secret Service for testing the keyring client against the real wire protocol.
//
// Implements the subset SnipLingo uses (OpenSession, SearchItems, Unlock, CreateItem,
// Item.GetSecret, Item.Delete, Session.Close, Item properties) and records the *declared D-Bus
// signatures* so a client that marshals the wrong type is caught instead of silently accepted.
#pragma once

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QHash>
#include <QMap>
#include <QStringList>

#include "secrets/secret_service_store.h"

class FakeSecretService : public QDBusVirtualObject {
    Q_OBJECT
public:
    explicit FakeSecretService(const QString &busAddress, QObject *parent = nullptr);
    ~FakeSecretService() override;

    [[nodiscard]] bool ready() const { return m_ready; }

    [[nodiscard]] QString lastError() const { return m_error; }

    // Behaviour knobs modelling what a real keyring can do.
    void setRequirePromptForWrites(bool require) { m_requirePromptForWrites = require; }

    void setLocked(bool locked) { m_locked = locked; }

    void seed(const QString &key, const QString &value);

    [[nodiscard]] int itemCount() const { return m_items.size(); }

    [[nodiscard]] QString storedValue(const QString &key) const;

    // Signatures observed per member, e.g. "CreateItem" -> "a{sv}(oayays)b".
    [[nodiscard]] QString signatureOf(const QString &member) const {
        return m_signatures.value(member);
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;
    QString introspect(const QString &path) const override;

private:
    struct Item {
        QMap<QString, QString> attributes;
        QByteArray value;
        QString label;
    };

    QDBusMessage dispatch(const QDBusMessage &message);
    QString findItemByKey(const QString &key) const;

    bool m_ready = false;
    QString m_error;
    QDBusConnection m_bus;
    QHash<QString, Item> m_items;
    QHash<QString, QString> m_signatures;
    int m_nextItemId = 1;
    bool m_requirePromptForWrites = false;
    bool m_locked = false;
};
