#pragma once

#include <Ren/MMat.h>

#include "Common.h"

struct VegState {
    // streaming data
    float movement_scale = 1.0f;
    float tree_mode = 1.0f;
    float bend_scale = 1.0f;
    float stretch = 0.0f;

    // temporary data
    //Ren::Vec2f wind_scroll;

    static constexpr float WindNoiseLfScale = 128.0f;

    static void Read(const JsObjectP &js_in, VegState &vs);
    static void Write(const VegState &vs, JsObjectP &js_out);

    static const char *name() { return "veg_state"; }
};
