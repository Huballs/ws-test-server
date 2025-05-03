#include "util.hpp"

bool run_with_timeout(std::function<void()> f, std::chrono::milliseconds timeout, std::string_view text) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool done = false;

    auto wrapper = [&done, f]() {
        try {
            f();
        } catch(std::exception const& e) {
            UTL_LOG_DERR("Exception in timeout handler: ", e.what());
        }
        done = true;
    };

    auto t = new std::thread(wrapper);
    t->detach();

    while(!done) {
        if(std::chrono::steady_clock::now() > deadline) {
            UTL_LOG_DWARN("Timeout: ", text);
            break;
        }
    }

    delete t;

    return done;
}