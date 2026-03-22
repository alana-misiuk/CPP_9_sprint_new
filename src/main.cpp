#include <chrono>
#include <memory>
#include <print>
#include <thread>
#include <utility>

#include <SFML/Graphics.hpp>

#include <exec/any_sender_of.hpp>
#include <exec/repeat_effect_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "mandelbrot_sender.hpp"
#include "sfml_display_sender.hpp"
#include "sfml_events_handler.hpp"
#include "types_sfml.hpp"

using namespace std::chrono_literals;
namespace ex = stdexec;

template <class... Ts>
using AnySenderOf = exec::any_receiver_ref<stdexec::completion_signatures<Ts...>>::template any_sender<>;

using VoidPipelineSender =
    AnySenderOf<stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr), stdexec::set_stopped_t()>;

class WaitForFPS {
public:
    static constexpr float TARGET_FPS = 60.0f;

    explicit WaitForFPS(FrameClock &frame_clock, unsigned int target_fps)
        : frame_clock_(frame_clock),
          frame_time_(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>{1.0 / static_cast<double>(target_fps)})) {}

    void operator()() {
        auto cur_frame_duration = frame_clock_.GetFrameTime();

        if (cur_frame_duration < frame_time_) {
            std::this_thread::sleep_for(frame_time_ - cur_frame_duration);
        }
        frame_clock_.Reset();
    }

private:
    FrameClock &frame_clock_;
    const std::chrono::steady_clock::duration frame_time_{1ms};
};

class MandelbrotApp {
public:
    MandelbrotApp() : compute_pool_{std::max(1u, std::thread::hardware_concurrency())}, sfml_thread_{1} {
        std::println("hardware_concurrency: {}\n", std::thread::hardware_concurrency());
    }

    void Run() {
        auto compute_sched = compute_pool_.get_scheduler();
        auto sfml_sched = sfml_thread_.get_scheduler();

        auto initialize =
            ex::starts_on(sfml_sched,
                          ex::just() | ex::then([this]() {
                              state_ = std::make_unique<SfmlState>(
                                  RenderSettings{.width = 800, .height = 600, .max_iterations = 100, .escape_radius = 2.0});
                          }));
        ex::sync_wait(std::move(initialize));

        auto process_frame =
            ex::starts_on(sfml_sched, SfmlEventHandler{state_->window, state_->render_settings, state_->app_state}) |
            ex::let_value([this, compute_sched, sfml_sched]() -> VoidPipelineSender {
                if (!state_->app_state.need_rerender) {
                    return VoidPipelineSender{ex::just() | ex::then(WaitForFPS{
                                                  state_->frame_clock,
                                                  static_cast<unsigned int>(WaitForFPS::TARGET_FPS)})};
                }

                state_->app_state.need_rerender = false;

                return VoidPipelineSender{
                    ex::starts_on(compute_sched,
                                  ex::just(&state_->fb) |
                                      mandelbrot::MakeComputeSender(state_->render_settings,
                                                                    state_->app_state.viewport)) |
                    ex::let_value([this, sfml_sched](FrameBuffer *fb) -> VoidPipelineSender {
                        return VoidPipelineSender{
                            ex::starts_on(sfml_sched,
                                          ex::just(fb) | render::MakeSfmlDisplaySender(*state_)) |
                            ex::then(WaitForFPS{state_->frame_clock,
                                                static_cast<unsigned int>(WaitForFPS::TARGET_FPS)})};
                    })};
            });

        #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        #elif defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        #endif
        auto repeated_pipeline =
            std::move(process_frame) | ex::then([this] { return state_->app_state.should_exit; }) |
            exec::repeat_effect_until();
        #if defined(__clang__)
        #pragma clang diagnostic pop
        #elif defined(__GNUC__)
        #pragma GCC diagnostic pop
        #endif
        ex::sync_wait(std::move(repeated_pipeline));
    }

private:
    std::unique_ptr<SfmlState> state_;

    exec::static_thread_pool compute_pool_;
    exec::static_thread_pool sfml_thread_;
};

int main() {
    std::println("=== Mandelbrot Fractal Renderer ===\n");
    std::println("Controls:");
    std::println("  Left Mouse Button  - Zoom In");
    std::println("  Right Mouse Button - Zoom Out");
    std::println("  X                  - Toggle Auto Zoom (infinite zoom to 'Seahorse Valley' point)");
    std::println("  C                  - Reset to Initial View");
    std::println("  Close Window       - Exit\n");

    try {
        MandelbrotApp app;
        app.Run();
    } catch (const std::exception &e) {
        std::println("Error: {}", e.what());
        return 1;
    }
    return 0;
}
