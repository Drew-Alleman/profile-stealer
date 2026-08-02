#include "channel.h"
#include "stealth/encryption.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

#include "json.hpp"

namespace cdp::detail {

    using json = njson::json;

    void Channel::connect(const std::string& host,
        const std::string& port,
        const std::string& target)
    {
        host_ = host;
        port_ = port;
        target_ = target;
        stop_events_.store(false);

        conn_.connect(host, port, target);

        running_.store(true);
        reader_ = std::thread([this] { reader_loop(); });
    }

    bool Channel::is_connected() const noexcept {
        return conn_.is_connected();
    }

    void Channel::stop_reader() noexcept {
        running_.store(false);
        conn_.close();
        if (reader_.joinable()) reader_.join();
    }

    void Channel::reconnect() {
        stop_reader();
        fail_all_pending(CRYPT("reconnecting"));
        {
            std::lock_guard<std::mutex> lk(mtx_);
            event_queue_.clear();
        }
        stop_events_.store(false);

        conn_.connect(host_, port_, target_);

        running_.store(true);
        reader_ = std::thread([this] { reader_loop(); });
    }

    void Channel::close() noexcept {
        stop_events_.store(true);
        ev_cv_.notify_all();
        stop_reader();
        fail_all_pending(CRYPT("connection closed"));
        ev_cv_.notify_all();
    }

    Channel::~Channel() {
        stop_events_.store(true);
        stop_reader();
    }

    void Channel::fail_all_pending(const std::string& why) {
        std::vector<std::shared_ptr<std::promise<json>>> doomed;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto& kv : pending_) doomed.push_back(kv.second);
            pending_.clear();
        }
        for (auto& p : doomed) {
            try { p->set_exception(std::make_exception_ptr(std::runtime_error(why))); }
            catch (...) {}
        }
    }

    void Channel::reader_loop() {
        while (running_.load()) {
            std::string msg;
            try {
                msg = conn_.read();
            }
            catch (const std::exception& e) {
                if (running_.load())
                    std::cerr << CRYPT("[READER] unexpected error: ") << e.what() << "\n";
                break;
            }
            if (msg.empty()) break;

            json j;
            try { j = json::parse(msg); }
            catch (...) { continue; }

            if (j.contains(CRYPT("id"))) {
                const int id = j.value(CRYPT("id"), -1);
                std::shared_ptr<std::promise<json>> prom;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    auto it = pending_.find(id);
                    if (it != pending_.end()) {
                        prom = it->second;
                        pending_.erase(it);
                    }
                }
                if (prom) {
                    try { prom->set_value(std::move(j)); }
                    catch (...) {}
                }
            }
            else if (j.contains(CRYPT("method"))) {
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    if (event_queue_.size() > 1024) event_queue_.pop_front();
                    event_queue_.push_back(std::move(j));
                }
                ev_cv_.notify_one();
            }
        }

        running_.store(false);
        fail_all_pending(CRYPT("connection closed"));
        ev_cv_.notify_all();
    }

    json Channel::result_of(const std::string& method,
        const json& params,
        const std::string& session_id)
    {
        if (!running_.load() || !conn_.is_connected())
            throw std::runtime_error(method + CRYPT(": not connected"));

        const int id = next_id_.fetch_add(1);

        json req = {
            {CRYPT("id"), id},
            {CRYPT("method"), method},
            {CRYPT("params"), params}
        };
        if (!session_id.empty())
            req[CRYPT("sessionId")] = session_id;

        auto prom = std::make_shared<std::promise<json>>();
        auto fut = prom->get_future();

        {
            std::lock_guard<std::mutex> lk(mtx_);
            pending_.emplace(id, prom);
        }

        {
            std::lock_guard<std::mutex> lk(send_mtx_);
            try {
                conn_.send(req.dump());
            }
            catch (...) {
                std::lock_guard<std::mutex> lk2(mtx_);
                pending_.erase(id);
                throw;
            }
        }

        if (fut.wait_for(command_timeout_) != std::future_status::ready) {
            std::lock_guard<std::mutex> lk(mtx_);
            pending_.erase(id);
            throw TimeoutError(method + CRYPT(": timed out waiting for reply"));
        }

        json j = fut.get();

        if (j.contains(CRYPT("error"))) {
            throw std::runtime_error(method + CRYPT(": ") +
                j[CRYPT("error")].value(CRYPT("message"), CRYPT("unknown error")));
        }
        return j.value(CRYPT("result"), json::object());
    }

    std::optional<json> Channel::next_event() {
        std::unique_lock<std::mutex> lk(mtx_);
        for (;;) {
            if (stop_events_.load()) return std::nullopt;
            if (!event_queue_.empty()) {
                json ev = std::move(event_queue_.front());
                event_queue_.pop_front();
                return ev;
            }
            if (!running_.load()) return std::nullopt;
            ev_cv_.wait_for(lk, event_poll_interval_);
        }
    }

    void Channel::stop_events() noexcept {
        stop_events_.store(true);
        ev_cv_.notify_all();
    }

    void Channel::set_on_idle(std::function<void()> cb) {
        on_idle_ = std::move(cb);
    }

} // namespace cdp::detail