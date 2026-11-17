#include "provider/sse_parser.h"

namespace provider {

SseParser::SseParser(DataHandler onData)
    : m_onData(std::move(onData)) { }

void SseParser::feed(const QByteArray &chunk) {
    m_buffer.append(chunk);

    int newline = m_buffer.indexOf('\n');
    while (newline >= 0) {
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        processLine(line);
        newline = m_buffer.indexOf('\n');
    }
}

void SseParser::finish() {
    // A partial line is discarded on purpose: without a terminating newline the event is
    // incomplete.
    m_buffer.clear();
    m_hasPendingData = false;
    m_data.clear();
}

void SseParser::reset() {
    m_buffer.clear();
    m_data.clear();
    m_hasPendingData = false;
    m_dispatched = 0;
}

void SseParser::processLine(const QByteArray &line) {
    if (line.isEmpty()) {
        // Blank line terminates an event.
        if (m_hasPendingData && m_onData) {
            m_onData(m_data);
            ++m_dispatched;
        }
        m_data.clear();
        m_hasPendingData = false;
        return;
    }

    if (line.startsWith(':'))
        return;  // comment / keep-alive

    const int colon = line.indexOf(':');
    const QByteArray field = colon < 0 ? line : line.left(colon);
    if (field != "data")
        return;  // event:, id:, retry: carry no translation payload

    QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
    if (value.startsWith(' '))
        value.remove(0, 1);

    if (m_hasPendingData)
        m_data.append('\n');
    m_data.append(value);
    m_hasPendingData = true;
}

}  // namespace provider
