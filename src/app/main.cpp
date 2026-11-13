/// @file app/main.cpp
/// @brief Command line and the terminal main loop around a Session.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "app/localclock.h"
#include "app/session.h"
#include "audio/audio.h"
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
  /// Headless mode: summon the scene's rare event at this step (--rare).
  int dumpRare = -1;
  /// With --rare-other: summon the scene's second catastrophe instead (--rare-other).
  bool dumpRareOther = false;
  /// Headless mode: press the action key at this step (--act).
  int dumpAct = -1;    // with --dump: press the action key at this step
  /// Size used by --dump.
  int dumpWidth = 100, dumpHeight = 30;
  /// Start time of day in hours (--hour); negative means the scene's own start time.
  double hour = -1;
  /// Let the time of day follow the host clock (--realtime).
  bool realtime = false;
  /// Frame rate (--fps).
  double fps = 20;
  /// Force 24-bit, 256-color or Linux console output (--truecolor, --256, --console).
  bool forceTrueColor = false, force256 = false, forceConsole = false;
  /// True if --audio was given: a missing sound device is then reported on screen.
  bool audio = false;
  /// Start without ambient sound (--mute).
  bool mute = false;
  /// Print scenes or sprites and exit (--list-scenes, --gallery).
  bool listScenes = false, gallery = false;
  /// Render style (--style).
  Style style = Style::Full;
  /// True if --style was given, which then wins over the style picked from the terminal.
  bool styleSet = false;
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
      "  --style NAME     full (default; terminal on the Linux text console) | terminal (16 ANSI colors, your background) | mono\n"
      "  --truecolor      force 24-bit color\n"
      "  --256            force 256-color\n"
      "  --audio          ambient sound is on by default; with --audio a missing device is reported\n"
      "  --mute           start without ambient sound (a toggles it)\n"
      "  --console        force the Linux text console look (16 colors, dark backdrops)\n"
      "  --dump N         headless: print the scene after N steps (no terminal needed)\n"
      "  --size WxH       size for --dump (default 100x30)\n"
      "  --act N          with --dump: trigger the scene's action at step N\n"
      "  --rare N         with --dump: summon the scene's rare event at step N\n"
      "  --rare-other N   ...or its second, less likely catastrophe\n"
      "  --hour H         start at this time of day, 0 to 24 (scenes with a day and night cycle)\n"
      "  --realtime       let the time of day follow this computer's clock\n"
      "  --gallery        print every sprite (for art review) and exit\n"
      "  --version, --help\n"
      "\n"
      "keys: m/enter/esc menu | left/right scene | c style | t time of day | f action (feed, rain, meteor...) | x rare event (X: the second one) | a sound | i info | p pause | r restart | q quit");
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
    else if (a == "--rare-other") { const char* v = next(); if (!v) return false; o.dumpRare = std::atoi(v); o.dumpRareOther = true; }
    else if (a == "--rare") { const char* v = next(); if (!v) return false; o.dumpRare = std::atoi(v); }
    else if (a == "--act") { const char* v = next(); if (!v) return false; o.dumpAct = std::atoi(v); }
    else if (a == "--realtime") o.realtime = true;
    else if (a == "--hour") { const char* v = next(); if (!v) return false; o.hour = std::atof(v); }
    else if (a == "--size") { const char* v = next(); if (!v || std::sscanf(v, "%dx%d", &o.dumpWidth, &o.dumpHeight) != 2) return false; }
    else if (a == "--style") {
      const char* v = next();
      if (!v) return false;
      const std::string n = v;
      o.styleSet = true;
      if (n == "full") o.style = Style::Full;
      else if (n == "terminal") o.style = Style::Terminal;
      else if (n == "mono") o.style = Style::Mono;
      else return false;
    }
    else if (a == "--truecolor") o.forceTrueColor = true;
    else if (a == "--256") o.force256 = true;
    else if (a == "--console") o.forceConsole = true;
    else if (a == "--audio") o.audio = true;
    else if (a == "--mute") o.mute = true;
    else if (a == "--list-scenes") o.listScenes = true;
    else if (a == "--gallery") o.gallery = true;
    else if (a == "--version") { std::puts("diorascii " DIORASCII_VERSION); std::exit(0); }
    else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
    else return false;
  }
  return true;
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
    if (opt.hour >= 0) world.setHour(opt.hour);
    if (opt.realtime) {
      world.setTimeMode(World::TimeMode::Realtime);
      world.setHour(localClockHour());
    }
    for (int i = 0; i < opt.dumpSteps; ++i) {
      if (i == opt.dumpAct) world.interact();
      if (i == opt.dumpRare) world.triggerRare(opt.dumpRareOther ? RarePick::Other : RarePick::Weighted);
      world.step(1.0 / 20);
    }
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
  if (opt.forceConsole) term.setColorMode(ColorMode::Console);

  int w = 80, h = 24;
  term.size(w, h);
  Session::Config config;
  config.seed = opt.seed;
  config.scene = sceneIdx;
  config.style = opt.styleSet ? opt.style : defaultStyleFor(term.colorMode());  // an explicit --style wins
  config.hour = opt.hour;
  config.realtime = opt.realtime;
  config.sound = !opt.mute;
  Session session(pack, scenes, config, w, h);

  using clock = std::chrono::steady_clock;
  auto next = clock::now();
  double measuredFps = opt.fps;
  auto lastFrame = clock::now();
  Frame cur, prev;
  bool havePrev = false;

  // Ambient sound is on unless muted; a machine without a sound device just stays silent. The
  // missing device is only announced when the user asked for sound (--audio or the a key).
  AudioOutput audio;
  bool startup = true;
  auto syncSound = [&]() {
    if (session.soundWanted() && !audio.running() && !audio.start()) {
      session.soundUnavailable(!startup || opt.audio);
    }
    if (!session.soundWanted() && audio.running()) audio.stop();
    startup = false;
  };
  syncSound();

  while (!term.quitRequested() && !session.quitRequested()) {
    for (Action a : term.poll(5)) session.handle(a);
    syncSound();
    if (session.quitRequested()) break;
    if (term.takeResized()) {
      term.size(w, h);
      session.resize(w, h);
    }

    const auto now = clock::now();
    if (now < next) continue;
    next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(dt));
    if (now > next + std::chrono::milliseconds(250)) next = now;  // don't spiral after a stall

    const double elapsed = std::chrono::duration<double>(now - lastFrame).count();
    lastFrame = now;
    if (elapsed > 0) measuredFps = measuredFps * 0.9 + (1.0 / elapsed) * 0.1;

    session.setMeasuredFps(measuredFps);
    session.setClockHour(localClockHour());
    session.step(dt);
    if (audio.running()) audio.setParams(session.ambient());
    session.render(cur);
    if (session.takeRepaint()) havePrev = false;
    // The terminal style uses the user's 16 ANSI colors so it follows their theme.
    const Style style = session.style();
    // The Linux console keeps its own mode: it cannot show the bright backgrounds of Ansi16.
    const ColorMode mode =
        style == Style::Terminal && term.colorMode() != ColorMode::Console ? ColorMode::Ansi16 : term.colorMode();
    term.write(encodeFrame(havePrev ? &prev : nullptr, cur, mode));
    prev = cur;
    havePrev = true;
  }
  return 0;
}
