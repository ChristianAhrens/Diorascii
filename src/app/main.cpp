/// @file app/main.cpp
/// @brief Command line, main loop and wiring of core and terminal.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "core/assets.h"
#include "core/world.h"
#include "term/terminal.h"
#include "version.h"

using namespace dio;

namespace {

/// Parsed command line.
struct Options {
  /// Seed for deterministic runs, valid if seedSet.
  uint64_t seed = 0;
  /// True if --seed was given.
  bool seedSet = false;
  /// Extra asset directory (--assets).
  std::string assetsDir;
  /// Headless mode: print the frame after this many steps (--dump).
  int dumpSteps = -1;  // >= 0: headless, print the frame after N steps and exit
  /// Size used by --dump.
  int dumpWidth = 100, dumpHeight = 30;
  /// Frame rate (--fps).
  double fps = 20;
  /// Force 24-bit or 256-color output (--truecolor, --256).
  bool forceTrueColor = false, force256 = false;
};

/// Prints the command line help.
void usage() {
  std::puts(
      "diorascii " DIORASCII_VERSION "\n"
      "usage: diorascii [options]\n"
      "  --seed N         deterministic run\n"
      "  --assets DIR     load extra/override *.sprite files from DIR\n"
      "  --fps N          frame rate (default 20)\n"
      "  --truecolor      force 24-bit color\n"
      "  --256            force 256-color\n"
      "  --dump N         headless: print the scene after N steps (no terminal needed)\n"
      "  --size WxH       size for --dump (default 100x30)\n"
      "  --version, --help\n"
      "\n"
      "keys: q quit | p/space pause | r restart | i info | f feed (soon)");
}

/// Parses the command line. Returns false on an unknown or malformed option.
bool parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--seed") { const char* v = next(); if (!v) return false; o.seed = std::strtoull(v, nullptr, 10); o.seedSet = true; }
    else if (a == "--assets") { const char* v = next(); if (!v) return false; o.assetsDir = v; }
    else if (a == "--fps") { const char* v = next(); if (!v) return false; o.fps = std::atof(v); if (o.fps < 1) o.fps = 1; }
    else if (a == "--dump") { const char* v = next(); if (!v) return false; o.dumpSteps = std::atoi(v); }
    else if (a == "--size") { const char* v = next(); if (!v || std::sscanf(v, "%dx%d", &o.dumpWidth, &o.dumpHeight) != 2) return false; }
    else if (a == "--truecolor") o.forceTrueColor = true;
    else if (a == "--256") o.force256 = true;
    else if (a == "--version") { std::puts("diorascii " DIORASCII_VERSION); std::exit(0); }
    else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
    else return false;
  }
  return true;
}

/// Draws the info line (version, scene, size, entity count, seed, fps) in row 0.
void drawInfo(Frame& f, const World& w, bool paused, double fps) {
  const Rgb fg{235, 235, 235}, bg{20, 20, 30};
  char line[96];
  std::snprintf(line, sizeof(line), " diorascii %s | %dx%d | %zu entities | seed %llu | %.0f fps%s ",
                DIORASCII_VERSION, w.width(), w.height(), w.entityCount(),
                static_cast<unsigned long long>(w.seed()), fps, paused ? " | PAUSED" : "");
  drawText(f, 0, 0, line, fg, bg);
}

}  // namespace

/// Program entry: loads assets, picks a scene and runs the interactive loop,
/// or renders headless output for --dump, --gallery and --list-scenes.
int main(int argc, char** argv) {
  Options opt;
  if (!parseArgs(argc, argv, opt)) {
    usage();
    return 2;
  }
  if (!opt.seedSet) {
    opt.seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  }

  AssetPack pack = AssetPack::embedded();
  if (!opt.assetsDir.empty()) pack.addDirectory(opt.assetsDir);
  for (const auto& e : pack.errors()) std::fprintf(stderr, "asset error: %s\n", e.c_str());

  const double dt = 1.0 / opt.fps;

  if (opt.dumpSteps >= 0) {
    World world(pack, opt.dumpWidth, opt.dumpHeight, opt.seed);
    for (int i = 0; i < opt.dumpSteps; ++i) world.step(1.0 / 20);
    Frame frame;
    world.render(frame);
    std::puts(frame.toText().c_str());
    return 0;
  }

  Terminal term;
  if (!term.init()) {
    std::fprintf(stderr, "diorascii needs an interactive terminal (try --dump N for headless output)\n");
    return 1;
  }
  if (opt.forceTrueColor) term.setColorMode(ColorMode::TrueColor);
  if (opt.force256) term.setColorMode(ColorMode::Ansi256);

  int w = 80, h = 24;
  term.size(w, h);
  uint64_t seed = opt.seed;
  auto world = std::make_unique<World>(pack, w, h, seed);
  Frame cur, prev;
  bool havePrev = false, paused = false, info = false;

  using clock = std::chrono::steady_clock;
  auto next = clock::now();
  double measuredFps = opt.fps;
  auto lastFrame = clock::now();

  while (!term.quitRequested()) {
    bool quit = false, rebuild = false;
    for (Action a : term.poll(5)) {
      switch (a) {
        case Action::Quit: quit = true; break;
        case Action::Pause: paused = !paused; break;
        case Action::Info: info = !info; break;
        case Action::Redraw: rebuild = true; ++seed; break;
        default: break;
      }
    }
    if (quit) break;
    if (term.takeResized()) {
      term.size(w, h);
      rebuild = true;
    }
    if (rebuild) {
      world = std::make_unique<World>(pack, w, h, seed);
      havePrev = false;
    }

    const auto now = clock::now();
    if (now < next) continue;
    next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(dt));
    if (now > next + std::chrono::milliseconds(250)) next = now;  // don't spiral after a stall

    const double elapsed = std::chrono::duration<double>(now - lastFrame).count();
    lastFrame = now;
    if (elapsed > 0) measuredFps = measuredFps * 0.9 + (1.0 / elapsed) * 0.1;

    if (!paused) world->step(dt);
    world->render(cur);
    if (info) drawInfo(cur, *world, paused, measuredFps);
    term.write(encodeFrame(havePrev ? &prev : nullptr, cur, term.colorMode()));
    prev = cur;
    havePrev = true;
  }
  return 0;
}
