/// @file app/main.cpp
/// @brief Command line, main loop and wiring of core and terminal.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "core/assets.h"
#include "core/utf8.h"
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
  /// Scene to start in (--scene).
  std::string scene;
  /// Headless mode: print the frame after this many steps (--dump).
  int dumpSteps = -1;  // >= 0: headless, print the frame after N steps and exit
  /// Size used by --dump.
  int dumpWidth = 100, dumpHeight = 30;
  /// Frame rate (--fps).
  double fps = 20;
  /// Force 24-bit or 256-color output (--truecolor, --256).
  bool forceTrueColor = false, force256 = false;
  /// Print scenes or sprites and exit (--list-scenes, --gallery).
  bool listScenes = false, gallery = false;
  /// Render style (--style).
  Style style = Style::Full;
};

/// Prints the command line help.
void usage() {
  std::puts(
      "diorascii " DIORASCII_VERSION "\n"
      "usage: diorascii [options]\n"
      "  --scene NAME     start in this scene (see --list-scenes)\n"
      "  --list-scenes    print the available scenes and exit\n"
      "  --seed N         deterministic run\n"
      "  --assets DIR     load extra/override *.sprite and *.scene files from DIR\n"
      "  --fps N          frame rate (default 20)\n"
      "  --style NAME     full (default) | terminal (16 ANSI colors, your background) | mono\n"
      "  --truecolor      force 24-bit color\n"
      "  --256            force 256-color\n"
      "  --dump N         headless: print the scene after N steps (no terminal needed)\n"
      "  --size WxH       size for --dump (default 100x30)\n"
      "  --gallery        print every sprite (for art review) and exit\n"
      "  --version, --help\n"
      "\n"
      "keys: left/right scene | c style | f feed | i info | p pause | r restart | q quit");
}

/// Parses the command line. Returns false on an unknown or malformed option.
bool parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--seed") { const char* v = next(); if (!v) return false; o.seed = std::strtoull(v, nullptr, 10); o.seedSet = true; }
    else if (a == "--assets") { const char* v = next(); if (!v) return false; o.assetsDir = v; }
    else if (a == "--scene") { const char* v = next(); if (!v) return false; o.scene = v; }
    else if (a == "--fps") { const char* v = next(); if (!v) return false; o.fps = std::atof(v); if (o.fps < 1) o.fps = 1; }
    else if (a == "--dump") { const char* v = next(); if (!v) return false; o.dumpSteps = std::atoi(v); }
    else if (a == "--size") { const char* v = next(); if (!v || std::sscanf(v, "%dx%d", &o.dumpWidth, &o.dumpHeight) != 2) return false; }
    else if (a == "--style") {
      const char* v = next();
      if (!v) return false;
      const std::string n = v;
      if (n == "full") o.style = Style::Full;
      else if (n == "terminal") o.style = Style::Terminal;
      else if (n == "mono") o.style = Style::Mono;
      else return false;
    }
    else if (a == "--truecolor") o.forceTrueColor = true;
    else if (a == "--256") o.force256 = true;
    else if (a == "--list-scenes") o.listScenes = true;
    else if (a == "--gallery") o.gallery = true;
    else if (a == "--version") { std::puts("diorascii " DIORASCII_VERSION); std::exit(0); }
    else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
    else return false;
  }
  return true;
}

/// Draws the info line (version, scene, size, entity count, seed, fps) in row 0.
void drawInfo(Frame& f, const World& w, bool paused, double fps, Style style) {
  char line[128];
  std::snprintf(line, sizeof(line), " diorascii %s | %s | %dx%d | %zu entities | seed %llu | %.0f fps%s ",
                DIORASCII_VERSION, w.scene().name.c_str(), w.width(), w.height(), w.entityCount(),
                static_cast<unsigned long long>(w.seed()), fps, paused ? " | PAUSED" : "");
  drawLabel(f, 0, 0, line, style);
}

/// Draws a short centered message, for example the scene title after a switch.
void drawToast(Frame& f, const std::string& text, Style style) {
  const std::string padded = "  " + text + "  ";
  const int len = static_cast<int>(decodeUtf8(padded).size());
  drawLabel(f, std::max((f.width() - len) / 2, 0), std::max(f.height() / 2 - 1, 0), padded, style);
}

/// Prints every sprite (first two frames) for art review.
void printGallery(const AssetPack& pack) {
  for (const auto& [name, sprite] : pack.sprites()) {
    std::printf("== %s  (%dx%d, %zu frame%s)\n", name.c_str(), sprite->width(), sprite->height(),
                sprite->frameCount(), sprite->frameCount() == 1 ? "" : "s");
    const size_t shown = std::min<size_t>(sprite->frameCount(), 2);
    for (size_t i = 0; i < shown; ++i) {
      for (const auto& row : sprite->frame(false, i).art) {
        std::string utf8;
        for (char32_t c : row) appendUtf8(utf8, c);
        std::printf("|%s|\n", utf8.c_str());
      }
    }
  }
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

  const auto scenes = pack.scenes();
  if (scenes.empty()) {
    std::fprintf(stderr, "no scenes available\n");
    return 1;
  }
  if (opt.gallery) { printGallery(pack); return 0; }
  if (opt.listScenes) {
    for (const auto& s : scenes) std::printf("%s\t%s\n", s->name.c_str(), s->title.c_str());
    return 0;
  }

  size_t sceneIdx = 0;
  if (!opt.scene.empty()) {
    bool found = false;
    for (size_t i = 0; i < scenes.size(); ++i) {
      if (scenes[i]->name == opt.scene) { sceneIdx = i; found = true; }
    }
    if (!found) {
      std::fprintf(stderr, "unknown scene '%s' (try --list-scenes)\n", opt.scene.c_str());
      return 2;
    }
  }

  const double dt = 1.0 / opt.fps;

  if (opt.dumpSteps >= 0) {
    World world(pack, scenes[sceneIdx], opt.dumpWidth, opt.dumpHeight, opt.seed);
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
  Style style = opt.style;
  auto world = std::make_unique<World>(pack, scenes[sceneIdx], w, h, seed);
  Frame cur, prev;
  bool havePrev = false, paused = false, info = false;
  std::string toast = scenes[sceneIdx]->title;
  double toastLeft = 2.0;

  using clock = std::chrono::steady_clock;
  auto next = clock::now();
  double measuredFps = opt.fps;
  auto lastFrame = clock::now();

  auto switchScene = [&](size_t idx) {
    sceneIdx = idx % scenes.size();
    toast = scenes[sceneIdx]->title;
    toastLeft = 2.0;
  };

  while (!term.quitRequested()) {
    bool quit = false, rebuild = false;
    for (Action a : term.poll(5)) {
      switch (a) {
        case Action::Quit: quit = true; break;
        case Action::Pause: paused = !paused; break;
        case Action::Info: info = !info; break;
        case Action::Feed: world->feed(); break;
        case Action::Redraw: rebuild = true; ++seed; break;
        case Action::Style:
          style = static_cast<Style>((static_cast<int>(style) + 1) % 3);
          havePrev = false;
          break;
        case Action::Left: switchScene(sceneIdx + scenes.size() - 1); rebuild = true; break;
        case Action::Right: switchScene(sceneIdx + 1); rebuild = true; break;
        default: break;
      }
    }
    if (quit) break;
    if (term.takeResized()) {
      term.size(w, h);
      rebuild = true;
    }
    if (rebuild) {
      world = std::make_unique<World>(pack, scenes[sceneIdx], w, h, seed);
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
    world->render(cur, style);
    if (info) drawInfo(cur, *world, paused, measuredFps, style);
    if (toastLeft > 0) {
      drawToast(cur, toast, style);
      toastLeft -= dt;
    }
    // The terminal style uses the user's 16 ANSI colors so it follows their theme.
    const ColorMode mode = style == Style::Terminal ? ColorMode::Ansi16 : term.colorMode();
    term.write(encodeFrame(havePrev ? &prev : nullptr, cur, mode));
    prev = cur;
    havePrev = true;
  }
  return 0;
}
