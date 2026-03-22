#pragma once

#include "types_sfml.hpp"

#include <print>
#include <stdexec/execution.hpp>

namespace ex = stdexec;

namespace render {

static auto MakeSfmlDisplaySender(SfmlState &st) {
    static AvrTimeCounter time_counter;
    return ex::then([&](FrameBuffer *fb) {
        time_counter.Start();
        st.image.create(fb->width, fb->height, fb->rgba.data());
        st.texture.update(st.image);

        st.window.clear();
        st.window.draw(st.sprite);
        st.window.display();
        time_counter.End();

        if (time_counter.Count() % 10 == 0) {
            std::println("Average display time: {} ms over {} frames", time_counter.GetAvr(), time_counter.Count());
        }
    });
}
}  // namespace render
