#include "platform/x11/x11_protocol.h"

#include <QFile>

namespace platform::x11 {
namespace {

constexpr uint8_t kXfixesSelectionNotifySubtype = 0;
constexpr int kShortDeadlineMs = 250;
constexpr int kFullDeadlineMs = 1500;

}  // namespace

bool isXfixesSelectionNotify(uint8_t firstEvent, uint8_t responseType) {
    return firstEvent != 0 && responseType == uint8_t(firstEvent + kXfixesSelectionNotifySubtype);
}

ConversionPlan conversionPlan(bool haveEventTimestamp) {
    ConversionPlan plan;
    if (!haveEventTimestamp) {
        plan.attempts[0] = {true, kFullDeadlineMs, "CurrentTime"};
        plan.count = 1;
        return plan;
    }
    plan.attempts[0] = {false, kShortDeadlineMs, "event-time"};
    plan.attempts[1] = {true, kFullDeadlineMs, "CurrentTime"};
    plan.count = 2;
    return plan;
}

IncrAssembler::IncrAssembler(int maxBytes)
    : m_maxBytes(maxBytes) { }

bool IncrAssembler::append(const QByteArray &chunk) {
    if (m_overCap)
        return false;
    if (chunk.isEmpty()) {
        m_finished = true;
        return true;
    }
    if (m_data.size() + chunk.size() > m_maxBytes) {
        m_overCap = true;
        return false;
    }
    m_data.append(chunk);
    return true;
}

QString processNameForPid(uint32_t pid) {
    if (pid == 0)
        return {};
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(comm.readAll()).trimmed();
}

}  // namespace platform::x11
