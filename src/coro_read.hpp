#include <coroutine>

struct IoResult {
    std::error_code ec;
    int result;
};

class RecvFromAwaitable {
public:
    RecvFromAwaitable(IoQueue& io, int socket, void* buf, size_t len,
        ::sockaddr_in* srcAddr)
        : io_(io)
        , socket_(socket)
        , buf_(buf)
        , len_(len)
        , srcAddr_(srcAddr)
    {
    }

    auto operator co_await()
    {
        struct Awaiter {
            RecvFromAwaitable& awaitable;
            IoResult result = {};

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                awaitable.io_.recvfrom(awaitable.socket_, awaitable.buf_,
                    awaitable.len_, 0, awaitable.srcAddr_,
                    [this, handle](std::error_code ec, int receivedBytes) {
                        result = IoResult { ec, receivedBytes };
                        handle.resume();
                    });
            }

            IoResult await_resume() const noexcept { return result; }
        };
        return Awaiter { *this };
    }

private:
    IoQueue& io_;
    int socket_;
    void* buf_;
    size_t len_;
    ::sockaddr_in* srcAddr_;
};