#pragma once

#include <memory>
#include <string>
#include <vector>

#include "result.h"

namespace cdp {

    namespace detail { class Channel; } 
    class Page {
    public:
        Page() = default;
        Page(std::shared_ptr<detail::Channel> channel,
            std::string target_id,
            std::string session_id);

        Result<std::string> evaluate(const std::string& js);
        Result<void> navigate(const std::string& url);

        bool valid() const noexcept;

    private:
        std::shared_ptr<detail::Channel> channel_;
        std::string target_id_;
        std::string session_id_;
        std::shared_ptr<void> session_guard_;  // detaches the CDP session when the last Page copy dies
    };

} // namespace cdp
