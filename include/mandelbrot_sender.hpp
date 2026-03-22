#pragma once

#include <print>
#include "mandelbrot_fractal_utils.hpp"
#include "types_sfml.hpp"

#include <stdexec/execution.hpp>

using namespace std::chrono_literals;
namespace ex = stdexec;

namespace mandelbrot {

static auto MakeComputeSender(RenderSettings settings, ViewPort viewport) {
    static AvrTimeCounter time_counter;
    return ex::then([settings, viewport](FrameBuffer *fb) {
               time_counter.Start();

               for (std::uint32_t y = 0; y < fb->height; ++y) {
                   for (std::uint32_t x = 0; x < fb->width; ++x) {
                       const auto c = Pixel2DToComplex(x, y, viewport, settings.width, settings.height);
                       const auto iterations =
                           CalculateIterationsForPoint(c, settings.max_iterations, settings.escape_radius);
                       const auto color = IterationsToColor(iterations, settings.max_iterations);

                       const auto idx = static_cast<size_t>((y * fb->width + x) * 4u);
                       fb->rgba[idx] = color.r;
                       fb->rgba[idx + 1] = color.g;
                       fb->rgba[idx + 2] = color.b;
                       fb->rgba[idx + 3] = 255;
                   }
               }

               return fb;
           }) |
           ex::then([](FrameBuffer *fb) {
               time_counter.End();
               if (time_counter.Count() % 10 == 0) {
                   std::println("\nAverage compute time: {} ms over {} frames", time_counter.GetAvr(),
                                time_counter.Count());
               }
               return fb;
           });
}

}  // namespace mandelbrot
