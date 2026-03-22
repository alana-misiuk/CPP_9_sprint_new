#include <gtest/gtest.h>

#include <stdexec/execution.hpp>

#include <exception>

#include "mandelbrot_sender.hpp"
#include "sfml_events_handler.hpp"
#include "types_sfml.hpp"

namespace ex = stdexec;

namespace {

class ComputeReceiver {
public:
    using receiver_concept = stdexec::receiver_t;

    struct Result {
        FrameBuffer *expected{};
        FrameBuffer *received{};
        bool value_called{false};
        bool error_called{false};
        bool stopped_called{false};
        std::exception_ptr error{};
    };

    explicit ComputeReceiver(Result &result) : result_{&result} {}

    void set_value(FrameBuffer *fb) noexcept {
        result_->value_called = true;
        result_->received = fb;
    }

    void set_error(std::exception_ptr error) noexcept {
        result_->error_called = true;
        result_->error = std::move(error);
    }

    void set_stopped() noexcept { result_->stopped_called = true; }

    auto get_env() const noexcept { return stdexec::env<>(); }

private:
    Result *result_{};
};

TEST(MandelbrotComputeSender, FillsFramebufferAndCompletesWithSamePointer) {
    RenderSettings settings{.width = 2, .height = 2, .max_iterations = 20, .escape_radius = 2.0};
    ViewPort viewport{2.0, 3.0, 2.0, 3.0};
    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);
    ComputeReceiver::Result result{.expected = &fb};

    auto snd = ex::just(&fb) | mandelbrot::MakeComputeSender(settings, viewport);
    ComputeReceiver receiver{result};
    auto op = ex::connect(std::move(snd), std::move(receiver));
    ex::start(op);

    EXPECT_TRUE(result.value_called);
    EXPECT_FALSE(result.error_called);
    EXPECT_FALSE(result.stopped_called);
    EXPECT_EQ(result.received, result.expected);

    EXPECT_EQ(fb.rgba.size(), 16u);
    for (size_t i = 3; i < fb.rgba.size(); i += 4) {
        EXPECT_EQ(fb.rgba[i], 255);
    }

    bool has_non_black_channel = false;
    for (size_t i = 0; i < fb.rgba.size(); i += 4) {
        has_non_black_channel = has_non_black_channel || fb.rgba[i] != 0 || fb.rgba[i + 1] != 0 || fb.rgba[i + 2] != 0;
    }
    EXPECT_TRUE(has_non_black_channel);
}

TEST(SfmlEventLogic, ZoomInShrinksViewportAndMarksFrameDirty) {
    AppState state;
    RenderSettings settings{.width = 800, .height = 600, .max_iterations = 100, .escape_radius = 2.0};

    const double old_width = state.viewport.width();
    const double old_height = state.viewport.height();

    sfml_event_logic::ZoomToPoint(state, settings, 400, 300, true);

    EXPECT_TRUE(state.need_rerender);
    EXPECT_DOUBLE_EQ(state.viewport.width(), old_width * 0.8);
    EXPECT_DOUBLE_EQ(state.viewport.height(), old_height * 0.8);
}

TEST(SfmlEventLogic, ZoomOutExpandsViewport) {
    AppState state;
    RenderSettings settings{.width = 800, .height = 600, .max_iterations = 100, .escape_radius = 2.0};

    const double old_width = state.viewport.width();
    const double old_height = state.viewport.height();

    sfml_event_logic::ZoomToPoint(state, settings, 400, 300, false);

    EXPECT_GT(state.viewport.width(), old_width);
    EXPECT_GT(state.viewport.height(), old_height);
    EXPECT_TRUE(state.need_rerender);
}

TEST(SfmlEventLogic, ToggleAutoZoomFlipsFlagAndRequestsRerender) {
    AppState state;
    state.need_rerender = false;

    sfml_event_logic::ToggleAutoZoom(state);
    EXPECT_TRUE(state.auto_zoom_enabled);
    EXPECT_TRUE(state.need_rerender);

    state.need_rerender = false;
    sfml_event_logic::ToggleAutoZoom(state);
    EXPECT_FALSE(state.auto_zoom_enabled);
    EXPECT_TRUE(state.need_rerender);
}

TEST(SfmlEventLogic, ResetViewportRestoresInitialStateAndDisablesAutoZoom) {
    AppState state;
    RenderSettings settings{.width = 800, .height = 600, .max_iterations = 100, .escape_radius = 2.0};
    sfml_event_logic::ZoomToPoint(state, settings, 100, 100, true);
    state.auto_zoom_enabled = true;
    state.need_rerender = false;

    sfml_event_logic::ResetViewport(state);

    EXPECT_EQ(state.viewport.x_min, AppState::INITIAL_VIEWPORT.x_min);
    EXPECT_EQ(state.viewport.x_max, AppState::INITIAL_VIEWPORT.x_max);
    EXPECT_EQ(state.viewport.y_min, AppState::INITIAL_VIEWPORT.y_min);
    EXPECT_EQ(state.viewport.y_max, AppState::INITIAL_VIEWPORT.y_max);
    EXPECT_FALSE(state.auto_zoom_enabled);
    EXPECT_TRUE(state.need_rerender);
}

TEST(SfmlEventLogic, AutoZoomTargetPixelFallsInsideWindowForInitialViewport) {
    AppState state;
    RenderSettings settings{.width = 800, .height = 600, .max_iterations = 100, .escape_radius = 2.0};

    const auto [pixel_x, pixel_y] = sfml_event_logic::GetAutoZoomTargetPixel(state, settings);

    EXPECT_GE(pixel_x, 0);
    EXPECT_LT(pixel_x, static_cast<int>(settings.width));
    EXPECT_GE(pixel_y, 0);
    EXPECT_LT(pixel_y, static_cast<int>(settings.height));
}

}  // namespace
