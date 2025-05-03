#pragma once

#include <coroutine>
#include "util.hpp"
#include "device_payloads.hpp"

enum class ReadResult {
    error, closed, success
};

void read_handler(boost::beast::error_code ec
    , std::size_t bytes_transferred
    , ws_state_t& ws_state
    , bool send_bad_payloads
    , std::coroutine_handle<> handle
    , ReadResult& res

) {

    if(!ec) {
        UTL_LOG_DINFO("Read handler: count: ",  bytes_transferred, " Message: ", boost::beast::make_printable(ws_state.buffer.data()));

        // auto read_h = [&ws_state](beast::error_code ec, std::size_t bytes_transferred) {
        //     read_handler(ec, bytes_transferred, ws_state);
        // };
    
        // ws_state.ws.async_read(ws_state.buffer, beast::bind_front_handler(read_h));
        LOCK_GUARD(*ws_state.ws_state_mutex);
        ws_state.extra_payload = ws_get_payload(
            boost::beast::buffers_to_string(ws_state.buffer.data())
            , send_bad_payloads
        );

        res = ReadResult::success;
    }
    else {
        UTL_LOG_DERR("Read handler error: ", ec.message());
        res = ReadResult::error;
    }

    ws_state.buffer.clear();

    handle.resume();
}

class ReadFromAwaitable {
public:
    ReadFromAwaitable(ws_state_t& ws_state, bool send_bad_payloads)
        : m_ws_state(ws_state), m_send_bad_payloads(send_bad_payloads)
    {
    }

    auto operator co_await()
    {
        struct Awaiter {
            ReadFromAwaitable& awaitable;
            ws_state_t& m_ws_state;
            bool m_send_bad_payloads;

            ReadResult m_result = ReadResult::success;
            
            bool await_ready() const noexcept { 
                // if(result != ReadResult::success)
                //     return true;
                return false; 
            }


            void await_suspend(std::coroutine_handle<> handle) noexcept
            {

                auto read_h = [&](boost::beast::error_code ec, std::size_t bytes_transferred) {
                    read_handler(ec, bytes_transferred, m_ws_state, m_send_bad_payloads, handle, m_result);
 
                };
            
                m_ws_state.ws.async_read(m_ws_state.buffer, boost::beast::bind_front_handler(read_h));
            }

            ReadResult await_resume() const noexcept { return m_result; }
        };
        return Awaiter { *this,  m_ws_state, m_send_bad_payloads };
    }
    
private:

    ws_state_t& m_ws_state;
    bool m_send_bad_payloads;
};

class BasicCoroutine {
public:
    struct Promise {
        BasicCoroutine get_return_object() { return BasicCoroutine {}; }

        void unhandled_exception() noexcept { }

        void return_void() noexcept { }

        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
    };
    using promise_type = Promise;
};
    
