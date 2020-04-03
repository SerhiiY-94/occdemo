#pragma once

#include <Sys/Signal_.h>

#include "BaseElement.h"

namespace Gui {
class ButtonBase : public BaseElement {
protected:
    enum class eState { Normal, Focused, Pressed };
    eState state_;
public:
    ButtonBase(const Vec2f &pos, const Vec2f &size, const BaseElement *parent);

    void Focus(const Vec2i &p) override;
    void Focus(const Vec2f &p) override;

    void Press(const Vec2i &p, bool push) override;
    void Press(const Vec2f &p, bool push) override;

    Sys::Signal<void()> pressed_signal;
};
}

