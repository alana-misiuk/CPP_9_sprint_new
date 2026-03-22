#pragma once

#include <SFML/Graphics.hpp>
#include <exception>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "types_core.hpp"

namespace sfml_event_logic {

inline void ToggleAutoZoom(AppState &state) {
    state.auto_zoom_enabled = !state.auto_zoom_enabled;
    state.need_rerender = true;
    state.zoom_clock.restart();
}

inline void ResetViewport(AppState &state) {
    state.viewport = AppState::INITIAL_VIEWPORT;
    state.auto_zoom_enabled = false;
    state.need_rerender = true;
    state.zoom_clock.restart();
}

inline void ZoomToPoint(AppState &state, const RenderSettings &render_settings, int pixel_x, int pixel_y, bool zoom_in,
                        double factor = 0.8) {
    const double current_width = state.viewport.width();
    const double current_height = state.viewport.height();
    const double target_x = state.viewport.x_min + (static_cast<double>(pixel_x) / render_settings.width) * current_width;
    const double target_y =
        state.viewport.y_min + (static_cast<double>(pixel_y) / render_settings.height) * current_height;

    const double zoom_factor = zoom_in ? factor : (1.0 / factor);
    const double new_width = current_width * zoom_factor;
    const double new_height = current_height * zoom_factor;

    const double relative_x = (target_x - state.viewport.x_min) / current_width;
    const double relative_y = (target_y - state.viewport.y_min) / current_height;

    state.viewport.x_min = target_x - relative_x * new_width;
    state.viewport.x_max = state.viewport.x_min + new_width;
    state.viewport.y_min = target_y - relative_y * new_height;
    state.viewport.y_max = state.viewport.y_min + new_height;
    state.need_rerender = true;
    state.zoom_clock.restart();
}

inline auto GetAutoZoomTargetPixel(const AppState &state, const RenderSettings &render_settings) -> std::pair<int, int> {
    const auto pixel_x = static_cast<int>(((AppState::AUTO_ZOOM_TARGET_X - state.viewport.x_min) / state.viewport.width()) *
                                          static_cast<double>(render_settings.width));
    const auto pixel_y = static_cast<int>(((AppState::AUTO_ZOOM_TARGET_Y - state.viewport.y_min) / state.viewport.height()) *
                                          static_cast<double>(render_settings.height));
    return {pixel_x, pixel_y};
}

}  // namespace sfml_event_logic

class SfmlEventHandler {
public:
    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr)>;

    template <typename Receiver>
    struct OperationState {
        Receiver receiver_;
        sf::RenderWindow &window_;
        RenderSettings render_settings_;
        AppState &state_;

        static constexpr float ZOOM_INTERVAL_MS = 100.0f;

        template <typename R>
        explicit OperationState(R &&r, sf::RenderWindow &window, RenderSettings render_settings, AppState &state)
            : receiver_{std::forward<R>(r)}, window_{window}, render_settings_{render_settings}, state_{state} {}

        void start() noexcept {
            try {
                HandleEvents();

                if (state_.should_exit) {
                    stdexec::set_value(std::move(receiver_));
                    return;
                }

                if (state_.auto_zoom_enabled) {
                    HandleAutoZoom();
                }

                stdexec::set_value(std::move(receiver_));
            } catch (...) {
                stdexec::set_error(std::move(receiver_), std::current_exception());
            }
        }

        friend void tag_invoke(stdexec::start_t, OperationState &op) noexcept { op.start(); }

    private:
        void HandleEvents() {
            sf::Event event;
            while (window_.pollEvent(event)) {
                switch (event.type) {
                case sf::Event::Closed:
                    state_.should_exit = true;
                    window_.close();
                    break;

                case sf::Event::KeyPressed:
                    HandleKeyPress(event.key);
                    break;

                case sf::Event::MouseButtonPressed:
                    HandleMousePress(event.mouseButton);
                    break;

                case sf::Event::MouseButtonReleased:
                    HandleMouseRelease(event.mouseButton);
                    break;

                default:
                    break;
                }
            }
        }

        void HandleKeyPress(const sf::Event::KeyEvent &key) {
            if (key.code == sf::Keyboard::X) {
                sfml_event_logic::ToggleAutoZoom(state_);
            } else if (key.code == sf::Keyboard::C) {
                sfml_event_logic::ResetViewport(state_);
            }
        }

        void HandleMousePress(const sf::Event::MouseButtonEvent &mouse) {
            if (state_.zoom_clock.getElapsedTime().asMilliseconds() < ZOOM_INTERVAL_MS) {
                return;
            }

            if (mouse.button == sf::Mouse::Left) {
                state_.left_mouse_pressed = true;
                state_.auto_zoom_enabled = false;
                sfml_event_logic::ZoomToPoint(state_, render_settings_, mouse.x, mouse.y, true);
            } else if (mouse.button == sf::Mouse::Right) {
                state_.right_mouse_pressed = true;
                state_.auto_zoom_enabled = false;
                sfml_event_logic::ZoomToPoint(state_, render_settings_, mouse.x, mouse.y, false);
            }
        }

        void HandleMouseRelease(const sf::Event::MouseButtonEvent &mouse) {
            if (mouse.button == sf::Mouse::Left) {
                state_.left_mouse_pressed = false;
            } else if (mouse.button == sf::Mouse::Right) {
                state_.right_mouse_pressed = false;
            }
        }

        void HandleAutoZoom() {
            if (state_.zoom_clock.getElapsedTime().asMilliseconds() < ZOOM_INTERVAL_MS) {
                return;
            }

            const auto [pixel_x, pixel_y] = sfml_event_logic::GetAutoZoomTargetPixel(state_, render_settings_);
            sfml_event_logic::ZoomToPoint(state_, render_settings_, pixel_x, pixel_y, true, 0.95);
        }
    };

    sf::RenderWindow &window_;
    RenderSettings render_settings_;
    AppState &state_;

    SfmlEventHandler(sf::RenderWindow &window, RenderSettings render_settings, AppState &state)
        : window_{window}, render_settings_{render_settings}, state_{state} {}

    template <typename Receiver>
    auto connect(Receiver &&receiver) && {
        return OperationState<std::decay_t<Receiver>>{std::forward<Receiver>(receiver), window_,
                                                      render_settings_, state_};
    }

    template <typename Receiver>
    auto connect(Receiver &&receiver) const & {
        return OperationState<std::decay_t<Receiver>>{std::forward<Receiver>(receiver), window_, render_settings_,
                                                      state_};
    }

    template <typename Env = stdexec::env<>>
    static auto get_completion_signatures(Env = {}) -> completion_signatures {
        return {};
    }
};
