/// @file app/session.h
/// @brief The interactive application state shared by every frontend.

#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/ambience.h"
#include "core/assets.h"
#include "core/frame.h"
#include "core/input.h"
#include "core/menu.h"
#include "core/world.h"

namespace dio {

/// Everything the interactive program does between input and output: the current scene and
/// world, the ASCII menu, style, time of day, pause, info line, toasts and the sound switch.
/// It performs no I/O of its own (no terminal, no sound device, no clock), so the terminal
/// frontend and other frontends (such as tvOS) drive it the same way: feed it Actions and
/// elapsed time, draw the Frame it renders, and play the sound it asks for.
class Session {
 public:
  /// Settings a session starts with.
  struct Config {
    /// Seed of the first world (a restart uses the next seed).
    uint64_t seed = 1;
    /// Index into the scene list to start in.
    size_t scene = 0;
    /// Initial render style.
    Style style = Style::Full;
    /// Start time of day in hours; negative means each scene's own start time.
    double hour = -1;
    /// Start in the real-time mode (the time of day follows the host clock, see setClockHour()).
    bool realtime = false;
    /// Show the info line from the start.
    bool info = false;
    /// Ask for ambient sound from the start.
    bool sound = false;
    /// Offer "Quit" in the menu. Frontends that are left another way (tvOS: the Menu button) turn it off.
    bool quitEntry = true;
  };

  /// @param scenes the scenes to switch between; must not be empty
  /// @param width,height size of the screen in cells
  Session(const AssetPack& pack, std::vector<std::shared_ptr<const SceneDef>> scenes,
          const Config& config, int width, int height);

  /// Applies one input action (menu navigation, scene change, pause, ...).
  void handle(Action a);
  /// Tells the session the screen changed size; the world is rebuilt for the new size.
  void resize(int width, int height);
  /// Advances the world by `dt` seconds (not while paused) and ages the toast message.
  void step(double dt);
  /// Draws the scene, info line, toast and menu into `out`.
  void render(Frame& out);

  /// True once the user asked to leave.
  bool quitRequested() const { return quit_; }
  /// True after a style change or a rebuild: the frontend must repaint everything.
  /// Reading it clears it.
  bool takeRepaint();
  /// Render style currently selected.
  Style style() const { return style_; }
  /// True while the simulation is paused.
  bool paused() const { return paused_; }
  /// Index of the current scene in the scene list.
  size_t sceneIndex() const { return sceneIdx_; }
  /// The world being simulated.
  const World& world() const { return *world_; }
  /// The menu (read access, for frontends that draw their own chrome).
  const Menu& menu() const { return menu_; }
  /// Shows a message in the middle of the screen for a moment.
  void toast(const std::string& text, double seconds);
  /// Tells the session the host's local time of day in hours (0 to 24, fractional). The frontend
  /// calls this every frame; it only matters while the time mode is World::TimeMode::Realtime.
  void setClockHour(double hour) { clockHour_ = hour; }
  /// Frame rate shown on the info line; the frontend measures it.
  void setMeasuredFps(double fps) { fps_ = fps; }

  /// True while the user wants ambient sound. The frontend opens or closes its sound device
  /// to match and, if it cannot open one, calls soundUnavailable().
  bool soundWanted() const { return sound_; }
  /// Called by the frontend when ambient sound was requested but no device could be opened:
  /// switches the request off and, if `announce`, says so on screen.
  void soundUnavailable(bool announce = true);
  /// What the ambient sound should reflect now (muted while paused).
  AmbientParams ambient() const;

 private:
  /// Builds a fresh world for the current scene, size and carried-over time of day.
  void rebuild();
  /// Rebuilds the world if an action asked for it.
  void flush();
  /// Switches to scene `idx` (wrapping) and shows its title; the world is rebuilt lazily.
  void switchScene(size_t idx);
  /// In the real-time mode, sets the world's time of day from the host clock.
  void syncClock();
  /// Turns the sound request on or off and keeps the menu in step.
  void setSound(bool on);
  /// Runs the scene's action and confirms it on screen.
  void runAction();
  /// Applies what the user changed in the open menu.
  void applyMenuEvents();

  /// Assets the worlds are built from.
  const AssetPack& pack_;
  /// All scenes, in menu order.
  std::vector<std::shared_ptr<const SceneDef>> scenes_;
  /// Index of the current scene.
  size_t sceneIdx_;
  /// Screen size in cells.
  int w_, h_;
  /// Seed of the current world.
  uint64_t seed_;
  /// Render style.
  Style style_;
  /// How the clock runs (cycle, dawn, day, dusk, night, real time); survives scene switches and restarts.
  World::TimeMode timeMode_ = World::TimeMode::Cycle;
  /// Host time of day in hours as last reported by the frontend; negative until it reports.
  double clockHour_ = -1;
  /// Phase (0..1) of the day to start the next world at; negative means the scene's own.
  double carriedPhase_;
  /// The simulation.
  std::unique_ptr<World> world_;
  /// The ASCII menu.
  Menu menu_;
  /// Simulation paused.
  bool paused_ = false;
  /// Info line visible.
  bool info_;
  /// Sound requested.
  bool sound_ = false;
  /// Quit requested.
  bool quit_ = false;
  /// A scene switch, restart or resize wants a new world.
  bool rebuildPending_ = false;
  /// The frontend must repaint everything.
  bool repaint_ = true;
  /// Message in the middle of the screen, and how long it stays.
  std::string toast_;
  double toastLeft_ = 0;
  /// Frame rate for the info line.
  double fps_ = 20;
};

}  // namespace dio
