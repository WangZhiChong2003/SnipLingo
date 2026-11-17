// Fallible result type used across module boundaries (see docs/design/01-core.md).
#pragma once

#include "core/error.h"

#include <optional>
#include <utility>

namespace core {

template <class T>
class Result {
public:
    Result(T value)  // NOLINT(google-explicit-constructor) — success values read naturally
        : m_value(std::move(value))
        , m_hasValue(true) { }

    Result(Error error)  // NOLINT(google-explicit-constructor)
        : m_error(std::move(error))
        , m_hasValue(false) { }

    [[nodiscard]] bool ok() const noexcept { return m_hasValue; }

    explicit operator bool() const noexcept { return m_hasValue; }

    T &value() & {
        Q_ASSERT(m_hasValue);
        return m_value;
    }

    const T &value() const & {
        Q_ASSERT(m_hasValue);
        return m_value;
    }

    T take() {
        Q_ASSERT(m_hasValue);
        return std::move(m_value);
    }

    [[nodiscard]] const Error &error() const {
        Q_ASSERT(!m_hasValue);
        return m_error;
    }

    [[nodiscard]] T valueOr(T fallback) const { return m_hasValue ? m_value : fallback; }

private:
    T m_value {};
    Error m_error;
    bool m_hasValue = false;
};

// Every module returns Result<void> for "failed with an error, no payload".
template <>
class Result<void> {
public:
    Result() = default;

    Result(Error error)  // NOLINT(google-explicit-constructor)
        : m_error(std::move(error)) { }

    [[nodiscard]] bool ok() const noexcept { return !m_error.has_value(); }

    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Error &error() const {
        Q_ASSERT(m_error.has_value());
        return *m_error;
    }

private:
    std::optional<Error> m_error;
};

}  // namespace core
