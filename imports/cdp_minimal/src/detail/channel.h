#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "json.hpp"
#include "connection.h"

namespace cdp::detail {

    using json = njson::json;

    class Channel {
    public:
        Channel() = default;
        ~Channel();                              // stops + joins the reader thread

        Channel(const Channel&) = delete;
        Channel& operator=(const Channel&) = delete;

        void connect(const std::string& host,
            const std::string& port,
            const std::string& target);

        bool is_connected() const noexcept;
        void reconnect();
        void close() noexcept;

        // Send command and get result. Still blocks the CALLER until the reply
        // arrives, but no longer reads the socket — it waits on a future the
        // reader thread fulfills, so a slow command can't stall event delivery.
        json result_of(const std::string& method,
            const json& params,
            const std::string& session_id);

        // Get next event (used by watcher). Blocks until an event is available
        // or events are stopped / the connection dies.
        std::optional<json> next_event();

        void stop_events() noexcept;

        void set_on_idle(std::function<void()> cb);

        void set_timeout(std::chrono::milliseconds ms) noexcept { command_timeout_ = ms; }

    private:
        void reader_loop();
        void stop_reader() noexcept;
        void fail_all_pending(const std::string& why);

        Connection conn_;
        std::string host_, port_, target_;

        std::atomic<int>  next_id_{ 1 };
        std::atomic<bool> stop_events_{ false };
        std::atomic<bool> running_{ false };

        std::thread reader_;

        std::mutex mtx_;                         // guards pending_ and event_queue_
        std::mutex send_mtx_;                    // serializes socket writes
        std::condition_variable ev_cv_;          // wakes next_event()

        std::map<int, std::shared_ptr<std::promise<json>>> pending_;  // id -> waiter
        std::deque<json> event_queue_;

        std::function<void()> on_idle_;

        std::chrono::milliseconds command_timeout_{ 30000 };
        std::chrono::milliseconds event_poll_interval_{ 1000 };  // reader read deadline
    };

} // namespace cdp::detail
