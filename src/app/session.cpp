/// @file app/session.cpp
/// @brief The interactive application state shared by every frontend.

#include "app/session.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "core/utf8.h"
#include "version.h"

namespace dio {

namespace {

/// Menu text for a render style.
const char* styleName(Style s) {
  return s == Style::Full ? "Full color" : s == Style::Terminal ? "Terminal colors" : "Monochrome";
}

/// Options of the menu's time-of-day choice. Scenes without a cycle only have one.
std::vector<std::string> timeOptions(bool hasCycle) {
  if (!hasCycle) return {"No cycle"};
  return {"Cycle", "Dawn", "Day", "Dusk", "Night", "Real time"};
}

/// Draws the info line (version, scene, size, entity count, seed, fps) in row 0.
void drawInfo(Frame& f, const World& w, bool paused, double fps, Style style) {
  char line[160];
  const std::string clock = w.hasDayNight() ? " | " + w.clockText() : "";
  std::snprintf(line, sizeof(line), " diorascii %s | %s | %dx%d | %zu entities | seed %llu | %.0f fps%s%s ",
                DIORASCII_VERSION, w.scene().name.c_str(), w.width(), w.height(), w.entityCount(),
                static_cast<unsigned long long>(w.seed()), fps, clock.c_str(), paused ? " | PAUSED" : "");
  drawLabel(f, 0, 0, line, style);
}

/// Draws a short centered message, for example the scene title after a switch.
void drawToast(Frame& f, const std::string& text, Style style) {
  const std::string padded = "  " + text + "  ";
  const int len = static_cast<int>(decodeUtf8(padded).size());
  drawLabel(f, std::max((f.width() - len) / 2, 0), std::max(f.height() / 2 - 1, 0), padded, style);
}

}  // namespace

Session::Session(const AssetPack& pack, std::vector<std::shared_ptr<const SceneDef>> scenes,
                 const Config& config, int width, int height)
    : pack_(pack),
      scenes_(std::move(scenes)),
      sceneIdx_(config.scene % scenes_.size()),
      w_(width),
      h_(height),
      seed_(config.seed),
      style_(config.style),
      timeMode_(config.realtime ? World::TimeMode::Realtime : World::TimeMode::Cycle),
      carriedPhase_(config.hour >= 0 ? config.hour / 24.0 : -1),
      menu_("DIORASCII"),
      info_(config.info) {
  std::vector<std::string> titles;
  for (const auto& sc : scenes_) titles.push_back(sc->title);
  menu_.addChoice("scene", "Scene", titles, sceneIdx_);
  menu_.addChoice("style", "Style",
                  {styleName(Style::Full), styleName(Style::Terminal), styleName(Style::Mono)},
                  static_cast<size_t>(style_));
  menu_.addChoice("time", "Time of day", timeOptions(scenes_[sceneIdx_]->dayNight),
                  scenes_[sceneIdx_]->dayNight ? static_cast<size_t>(timeMode_) : 0);
  menu_.addChoice("info", "Info line", {"off", "on"}, info_ ? 1 : 0);
  menu_.addChoice("audio", "Sound", {"off", "on"}, 0);
  menu_.addButton("action", scenes_[sceneIdx_]->action.label);
  menu_.addButton("rare", "Rare event");
  menu_.addButton("rare2", "Other rare event");
  menu_.addButton("resume", "Resume");
  if (config.quitEntry) menu_.addButton("quit", "Quit");
  menu_.setFooter({"diorascii " DIORASCII_VERSION, "Copyright (c) 2026 Christian Ahrens"});
  rebuild();
  toast(scenes_[sceneIdx_]->title, 2.0);
  if (config.sound) setSound(true);
}

void Session::rebuild() {
  world_ = std::make_unique<World>(pack_, scenes_[sceneIdx_], w_, h_, seed_);
  if (carriedPhase_ >= 0 && world_->hasDayNight()) world_->setPhase(carriedPhase_);
  world_->setTimeMode(timeMode_);
  syncClock();
  rebuildPending_ = false;
  repaint_ = true;
}

void Session::flush() {
  if (!rebuildPending_) return;
  // The time of day survives scene switches, restarts and resizes.
  if (world_->hasDayNight()) carriedPhase_ = world_->phase();
  rebuild();
}

void Session::switchScene(size_t idx) {
  sceneIdx_ = idx % scenes_.size();
  toast(scenes_[sceneIdx_]->title, 2.0);
  menu_.setValue("scene", sceneIdx_);
  menu_.setLabel("action", scenes_[sceneIdx_]->action.label);
  menu_.setOptions("time", timeOptions(scenes_[sceneIdx_]->dayNight), static_cast<size_t>(timeMode_));
  rebuildPending_ = true;
}

void Session::setSound(bool on) {
  sound_ = on;
  menu_.setValue("audio", on ? 1 : 0);
}

void Session::soundUnavailable(bool announce) {
  setSound(false);
  if (announce) toast("No audio device", 1.5);
}

void Session::runAction() {
  if (world_->interact() > 0) toast(scenes_[sceneIdx_]->action.label, 1.2);
}

void Session::toast(const std::string& text, double seconds) {
  toast_ = text;
  toastLeft_ = seconds;
}

bool Session::takeRepaint() {
  const bool r = repaint_;
  repaint_ = false;
  return r;
}

void Session::handle(Action a) {
  if (menu_.handle(a)) {  // the open menu consumes navigation
    applyMenuEvents();
    return;
  }
  switch (a) {
    case Action::Quit: quit_ = true; break;
    case Action::Pause: paused_ = !paused_; break;
    case Action::Info: info_ = !info_; menu_.setValue("info", info_); break;
    case Action::Interact: flush(); runAction(); break;
    case Action::Rare: flush(); world_->triggerRare(); break;
    case Action::RareOther: flush(); world_->triggerRare(RarePick::Other); break;
    case Action::Audio: setSound(!sound_); break;
    case Action::Redraw: rebuildPending_ = true; ++seed_; break;
    case Action::Style:
      style_ = static_cast<Style>((static_cast<int>(style_) + 1) % 3);
      menu_.setValue("style", static_cast<size_t>(style_));
      repaint_ = true;
      break;
    case Action::Time:
      if (scenes_[sceneIdx_]->dayNight) {
        flush();
        timeMode_ = static_cast<World::TimeMode>((static_cast<int>(timeMode_) + 1) % World::kTimeModes);
        world_->setTimeMode(timeMode_);
        syncClock();
        menu_.setValue("time", static_cast<size_t>(timeMode_));
        toast(timeOptions(true)[static_cast<size_t>(timeMode_)], 1.5);
      }
      break;
    case Action::Left: switchScene(sceneIdx_ + scenes_.size() - 1); break;
    case Action::Right: switchScene(sceneIdx_ + 1); break;
    case Action::Menu:
    case Action::Select:
    case Action::Back: menu_.open(); break;
    default: break;
  }
}

void Session::applyMenuEvents() {
  for (const auto& ev : menu_.takeEvents()) {
    if (ev.id == "scene") switchScene(ev.value);
    else if (ev.id == "style") { style_ = static_cast<Style>(ev.value); repaint_ = true; }
    else if (ev.id == "time") {
      flush();
      timeMode_ = static_cast<World::TimeMode>(ev.value);
      world_->setTimeMode(timeMode_);
      syncClock();
    }
    else if (ev.id == "info") info_ = ev.value == 1;
    else if (ev.id == "audio") setSound(ev.value == 1);
    else if (ev.id == "action") { flush(); runAction(); }
    else if (ev.id == "rare") { flush(); world_->triggerRare(); }
    else if (ev.id == "rare2") { flush(); world_->triggerRare(RarePick::Other); }
    else if (ev.id == "resume") menu_.close();
    else if (ev.id == "quit") quit_ = true;
  }
}

void Session::resize(int width, int height) {
  w_ = width;
  h_ = height;
  rebuildPending_ = true;
}

void Session::syncClock() {
  if (timeMode_ == World::TimeMode::Realtime && clockHour_ >= 0 && world_->hasDayNight()) world_->setHour(clockHour_);
}

void Session::step(double dt) {
  flush();
  syncClock();
  if (!paused_) world_->step(dt);
  if (toastLeft_ > 0 && !menu_.isOpen()) toastLeft_ -= dt;
}

void Session::render(Frame& out) {
  flush();
  world_->render(out, style_);
  if (info_) drawInfo(out, *world_, paused_, fps_, style_);
  if (toastLeft_ > 0 && !menu_.isOpen()) drawToast(out, toast_, style_);
  menu_.render(out, style_);
}

AmbientParams Session::ambient() const {
  AmbientParams p = ambientFor(*world_);
  if (paused_) p.volume = 0;
  return p;
}

}  // namespace dio
