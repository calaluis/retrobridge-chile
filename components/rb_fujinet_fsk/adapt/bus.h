// RetroBridge adapter (written for RetroBridge, not taken from FujiNet): SYSTEM_BUS.flushOutput(), the
// one bus call of the owner's FSK code. It waits until the UART has sent what it holds, before the
// RMT takes DATA IN. The composition root installs the function; without one it does nothing.
#pragma once

struct RbFujinetBus {
    void (*flush)() = nullptr;
    void flushOutput() const
    {
        if (flush != nullptr) {
            flush();
        }
    }
};

extern RbFujinetBus SYSTEM_BUS;
