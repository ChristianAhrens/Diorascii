/// @file bench_audio.cpp
/// @brief Measures the CPU cost of the ambient synth per scene and speaker layout.
///
/// Not a test and not part of `ctest`: timings depend on the machine. Run it through
/// `scripts/bench-audio.sh`. Every scene renders with all its sounds active (the same parameters as the
/// golden test), after a short warm-up, and the table shows the share of one core that one second of
/// audio costs at 48 kHz. Record the numbers in the commit message of the change they belong to.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/ambience.h"
#include "core/layout.h"

using namespace dio;

namespace {
/// Scene names in AmbientKind order, starting at the forest.
const char* const kScenes[] = {"forest", "aquarium", "desert", "moon", "mountains", "city"};
/// Frames per render call.
constexpr int kBlock = 4800;

/// Everything active in scene `kind`, as in the golden test (no catastrophe).
AmbientParams activeParams(AmbientKind kind) {
  AmbientParams p;
  p.kind = kind;
  p.night = 0.5f; p.rain = 0.6f; p.storm = 0.8f; p.whale = 0.6f; p.tornado = 0.6f; p.avalanche = 0.6f;
  p.worm = 0.6f; p.siren = 0.6f; p.wail = 0.6f; p.train = 0.6f; p.plane = 0.6f; p.fire = 0.6f;
  p.flames = 0.5f; p.winged = 0.5f;
  return p;
}

/// Share of one core (in percent) that one second of audio costs for a scene in a layout.
double cost(AmbientKind kind, const Layout& layout, int seconds) {
  Ambience a(48000, 5, layout);
  AmbientParams p = activeParams(kind);
  std::vector<float> buf(static_cast<size_t>(layout.channels()) * kBlock);
  for (int i = 0; i < 20; ++i) a.render(p, buf.data(), kBlock);  // warm-up: crossfades done, caches hot
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < seconds * 10; ++i) a.render(p, buf.data(), kBlock);
  const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
  return 100.0 * dt.count() / seconds;
}
}  // namespace

/// Prints the table. Optional argument: seconds of audio per measurement (default 5).
int main(int argc, char** argv) {
  const int seconds = argc > 1 ? std::max(1, std::atoi(argv[1])) : 5;
  std::printf("CPU, percent of one core per second of audio at 48 kHz (%d s per cell)\n%-10s", seconds, "");
  for (int i = 0; i < kLayoutCount; ++i) std::printf("%8s", Layout::nameOf(static_cast<LayoutId>(i)));
  std::printf("\n");
  for (int k = 0; k < 6; ++k) {
    std::printf("%-10s", kScenes[k]);
    for (int i = 0; i < kLayoutCount; ++i)
      std::printf("%8.2f", cost(static_cast<AmbientKind>(1 + k), Layout::named(static_cast<LayoutId>(i)), seconds));
    std::printf("\n");
  }
  return 0;
}
