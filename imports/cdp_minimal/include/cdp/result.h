#pragma once

#include <utility>
#include "error.h"

namespace cdp {

    template <class T>
    class Result {
    public:
        Result(T value);
        Result(Error error);

        bool ok() const noexcept;
        explicit operator bool() const noexcept;

        const T& value() const&;
        T& value()&;
        T&& value()&&;

        T value_or(const T& fallback) const;
        const Error& error() const noexcept;

    private:
        bool  ok_;
        T     value_{};
        Error error_{};
    };

    template <>
    class Result<void> {
    public:
        Result();
        Result(Error error);

        bool ok() const noexcept;
        explicit operator bool() const noexcept;
        const Error& error() const noexcept;

    private:
        bool  ok_;
        Error error_{};
    };

} // namespace cdp
