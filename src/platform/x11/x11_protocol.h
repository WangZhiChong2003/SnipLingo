// Protocol helpers for the X11 PRIMARY backend, kept free of xcb so they can be unit-tested.
#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace platform::x11 {

// X11 delivers extension events as (first_event + subtype). Comparing the raw response type against
// the bare subtype constant (0 for XFixes SelectionNotify) misses the event.
[[nodiscard]] bool isXfixesSelectionNotify(uint8_t firstEvent, uint8_t responseType);

struct ConversionAttempt {
    bool useCurrentTime = false;
    int deadlineMs = 0;
    const char *label = "";
};

struct ConversionPlan {
    ConversionAttempt attempts[2];
    int count = 0;
};

// The first attempt uses the event timestamp with a short deadline; a stalled owner then costs
// hundreds of milliseconds instead of a full timeout, and the CurrentTime retry is what actually
// answered on the measured desktop (M0 Run 8).
[[nodiscard]] ConversionPlan conversionPlan(bool haveEventTimestamp);

// Selection sizes are bounded before any conversion, and INCR transfers are assembled with the same
// cap so a hostile or careless owner cannot exhaust memory.
class IncrAssembler {
public:
    explicit IncrAssembler(int maxBytes);

    // Returns false when appending would exceed the cap; the caller must abandon the transfer.
    bool append(const QByteArray &chunk);

    [[nodiscard]] bool finished() const { return m_finished; }

    void markFinished() { m_finished = true; }

    [[nodiscard]] const QByteArray &data() const { return m_data; }

    [[nodiscard]] bool overCap() const { return m_overCap; }

private:
    QByteArray m_data;
    int m_maxBytes;
    bool m_finished = false;
    bool m_overCap = false;
};

// Best-effort source identification from _NET_WM_PID, read through /proc. Returns an empty string
// when the pid is unknown; never throws and never touches user content.
[[nodiscard]] QString processNameForPid(uint32_t pid);

}  // namespace platform::x11
