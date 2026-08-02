#include "cdp/page.h"
#include "../detail/channel.h"
#include "../detail/base64.h"
#include "../detail/json.hpp"
#include "stealth/encryption.hpp"

#include <fstream>

namespace cdp {

    using json = njson::json;

    Page::Page(std::shared_ptr<detail::Channel> channel,
        std::string target_id,
        std::string session_id)
        : channel_(std::move(channel))
        , target_id_(std::move(target_id))
        , session_id_(std::move(session_id))
    {
        if (channel_ && !session_id_.empty()) {
            auto ch = channel_;
            auto sid = session_id_;
            session_guard_ = std::shared_ptr<void>(nullptr, [ch, sid](void*) {
                try {
                    ch->result_of(CRYPT("Target.detachFromTarget"),
                        json{ {CRYPT("sessionId"), sid} }, "");
                }
                catch (...) {
                }
                });
        }
    }

    bool Page::valid() const noexcept {
        return static_cast<bool>(channel_);
    }

    Result<std::string> Page::evaluate(const std::string& js) {
        if (!channel_) {
            return Error{ Errc::not_connected, CRYPT("Page handle is invalid (tab may have closed)") };
        }

        try {
            json params = {
                {CRYPT("expression"), js},
                {CRYPT("returnByValue"), true},
                {CRYPT("awaitPromise"), true}
            };

            json result = channel_->result_of(CRYPT("Runtime.evaluate"), params, session_id_);

            if (result.contains(CRYPT("exceptionDetails"))) {
                const auto& ex = result[CRYPT("exceptionDetails")];
                std::string errorText = ex.value(CRYPT("text"), CRYPT("Unknown JavaScript error"));

                if (ex.contains(CRYPT("exception")) && ex[CRYPT("exception")].contains(CRYPT("description"))) {
                    errorText = ex[CRYPT("exception")][CRYPT("description")].get<std::string>();
                }

                return Error{ Errc::protocol, CRYPT("JavaScript error: ") + errorText };
            }

            const json& r = result.at(CRYPT("result"));

            if (r.value(CRYPT("type"), "") == CRYPT("string")) {
                return r.value(CRYPT("value"), std::string{});
            }

            if (r.contains(CRYPT("value"))) {
                return r[CRYPT("value")].dump();
            }

            return std::string{};

        }
        catch (const detail::TimeoutError& e) {
            return Error{ Errc::timeout, e.what() };
        }
        catch (const std::exception& e) {
            return Error{ Errc::bad_response,
                CRYPT("Failed to execute JavaScript: ") + std::string(e.what()) };
        }
    }

} // namespace cdp