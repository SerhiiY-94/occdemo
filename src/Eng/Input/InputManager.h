#pragma once

#include <functional>
#include <memory>

#include "Keycode.h"

struct InputManagerImp;

enum class RawInputEv {
    None = -1,
    P1Down, P1Up, P1Move,
    P2Down, P2Up, P2Move,
    KeyDown, KeyUp,
    Resize,
    MouseWheel,
    Count
};

class InputManager {
    std::unique_ptr<InputManagerImp> imp_;
public:
    struct Event {
        RawInputEv type = RawInputEv::None;
        uint32_t key_code;
        struct {
            float x, y;
        } point;
        struct {
            float dx, dy;
        } move;
        uint64_t time_stamp;
    };

    InputManager();
    ~InputManager() = default;
    InputManager(const InputManager &) = delete;
    InputManager &operator=(const InputManager &) = delete;

    void SetConverter(RawInputEv evt_type, const std::function<void(Event &)> &conv);
    void AddRawInputEvent(Event &evt);
    bool PollEvent(uint64_t time_us, Event &evt);
    void ClearBuffer();

    static char CharFromKeycode(uint32_t key_code);
};

