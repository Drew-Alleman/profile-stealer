#include "cdp/result.h"
#include "cdp/page.h"

#include <cassert>
#include <vector>

namespace cdp {

    template <class T>
    Result<T>::Result(T value)
        : ok_(true), value_(std::move(value)) {
    }

    template <class T>
    Result<T>::Result(Error error)
        : ok_(false), error_(std::move(error)) {
    }

    template <class T>
    bool Result<T>::ok() const noexcept { return ok_; }

    template <class T>
    Result<T>::operator bool() const noexcept { return ok_; }

    template <class T>
    const T& Result<T>::value() const& { assert(ok_ && "value() on an error Result"); return value_; }

    template <class T>
    T& Result<T>::value()& { assert(ok_ && "value() on an error Result"); return value_; }

    template <class T>
    T&& Result<T>::value()&& { assert(ok_ && "value() on an error Result"); return std::move(value_); }

    template <class T>
    T Result<T>::value_or(const T& fallback) const { return ok_ ? value_ : fallback; }

    template <class T>
    const Error& Result<T>::error() const noexcept { return error_; }

    template class Result<std::string>;
    template class Result<Page>;
    template class Result<std::vector<std::string>>;

    Result<void>::Result() : ok_(true) {}
    Result<void>::Result(Error error) : ok_(false), error_(std::move(error)) {}

    bool Result<void>::ok() const noexcept { return ok_; }
    Result<void>::operator bool() const noexcept { return ok_; }
    const Error& Result<void>::error() const noexcept { return error_; }

} // namespace cdp
