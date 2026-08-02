#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "result.h"
#include "page.h"

namespace cdp {

    namespace detail { class Channel; }

    class Browser {
    public:
        Browser(std::string host = "127.0.0.1", uint16_t port = 9222);
        ~Browser();

        Browser(Browser&&) noexcept;
        Browser& operator=(Browser&&) noexcept;
        Browser(const Browser&) = delete;
        Browser& operator=(const Browser&) = delete;
        bool connect();
        bool isConnected() const noexcept;
        Result<Page> anyPage();
        Result<Page> currentPage();
        Result<std::string> readFileViaFileURI(const std::string& localPath);
     

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace cdp
