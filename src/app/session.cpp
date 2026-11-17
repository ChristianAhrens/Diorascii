/// @file app/session.cpp
/// @brief The interactive application state shared by every frontend.

#include "app/session.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <utility>

#include "core/utf8.h"
#include "version.h"

namespace dio {

namespace {

/// True if the command line's layout is plain stereo, which starts the sound in the stereo mode.
bool isStereo(const LayoutChoice& c) { return !c.automatic && c.id == LayoutId::Stereo; }

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
      rng_(config.seed ^ 0x5eed5eedULL),
      menu_("DIORASCII"),
      info_(config.info),
      immersive_(isStereo(config.audioLayout) ? LayoutChoice{} : config.audioLayout) {
  std::vector<std::string> titles;
  titles.reserve(scenes_.size());
  std::transform(scenes_.begin(), scenes_.end(), std::back_inserter(titles), [](const auto& sc) { return sc->title; });
  menu_.addChoice("scene", "Scene", titles, sceneIdx_);
  menu_.addChoice("style", "Style",
                  {styleName(Style::Full), styleName(Style::Terminal), styleName(Style::Mono)},
                  static_cast<size_t>(style_));
  menu_.addChoice("mode", "Mode", {"User", "Cycle"}, 0);
  menu_.addChoice("time", "Time of day", timeOptions(scenes_[sceneIdx_]->dayNight),
                  scenes_[sceneIdx_]->dayNight ? static_cast<size_t>(timeMode_) : 0);
  menu_.addChoice("info", "Info line", {"off", "on"}, info_ ? 1 : 0);
  menu_.addChoice("audio", "Sound", {"off", "stereo", "immersive"}, 0);
  menu_.addButton("action", scenes_[sceneIdx_]->action.label);
  menu_.addButton("rare", "Rare event");
  menu_.addButton("rare2", "Other rare event");
  menu_.addButton("resume", "Resume");
  if (config.quitEntry) menu_.addButton("quit", "Quit");
  updateFooter();
  rebuild();
  toast(scenes_[sceneIdx_]->title, 2.0);
  if (config.sound) setSound(isStereo(config.audioLayout) ? SoundMode::Stereo : SoundMode::Immersive);
  if (config.cycle) setCycle(true);
}

void Session::rebuild() {
  world_ = std::make_unique<World>(pack_, scenes_[sceneIdx_], w_, h_, seed_);
  if (carriedPhase_ >= 0 && world_->hasDayNight()) world_->setPhase(carriedPhase_);
  world_->setTimeMode(timeMode_);
  syncClock();
  rebuildPending_ = false;
  repaint_ = true;
  markProgress();
}

void Session::flush() {
  if (!rebuildPending_) return;
  // The time of day survives scene switches, restarts and resizes.
  if (world_->hasDayNight()) carriedPhase_ = world_->phase();
  endFade();
  if (fadePending_) {  // the old world stays on screen and in the ears while the new one comes in
    outgoing_ = std::move(world_);
    fadeLeft_ = kFadeSeconds;
  }
  fadePending_ = false;
  rebuild();
}

void Session::endFade() {
  outgoing_.reset();
  fadeLeft_ = 0;
}

void Session::switchScene(size_t idx) {
  sceneIdx_ = idx % scenes_.size();
  toast(scenes_[sceneIdx_]->title, 2.0);
  menu_.setValue("scene", sceneIdx_);
  menu_.setLabel("action", scenes_[sceneIdx_]->action.label);
  menu_.setOptions("time", timeOptions(scenes_[sceneIdx_]->dayNight), static_cast<size_t>(timeMode_));
  rebuildPending_ = true;
}

void Session::setSound(SoundMode mode) {
  if (mode != mode_ && mode != SoundMode::Off && mode_ != SoundMode::Off) layoutChanged_ = true;  // reopened for the other layout
  mode_ = mode;
  menu_.setValue("audio", static_cast<size_t>(mode));
  if (mode == SoundMode::Off && !outputLine_.empty()) {  // with the sound off nothing has been found
    outputLine_.clear();
    updateFooter();
  }
}

LayoutChoice Session::layoutChoice() const {
  if (mode_ != SoundMode::Stereo) return immersive_;
  LayoutChoice stereo;
  stereo.automatic = false;
  stereo.id = LayoutId::Stereo;
  return stereo;
}

void Session::updateFooter() {
  std::vector<std::string> lines;
  lines.push_back(outputLine_.empty() ? "Audio output: sound off" : outputLine_);
  lines.push_back("diorascii " DIORASCII_VERSION);
  lines.push_back("Copyright (c) 2026 Christian Ahrens");
  menu_.setFooter(std::move(lines));
}

bool Session::takeLayoutChange() {
  const bool changed = layoutChanged_;
  layoutChanged_ = false;
  return changed;
}

void Session::soundLayout(const char* name, int deviceChannels, bool downmixed, bool spatialized) {
  // The layout in use, whatever was asked for: for example "Audio output: 7.1 on 8 channels", "Audio output: 7.1.4 on 2 channels (mixed down)"
  // or "Audio output: 7.1.4 on 2 channels (spatialized)". A spatializing route is not a plain mix-down, so it says only that.
  outputLine_ = "Audio output: " + std::string(std::string(name) == "custom" ? "custom map" : name) + " on " + std::to_string(deviceChannels) +
                (deviceChannels == 1 ? " channel" : " channels") + (spatialized ? " (spatialized)" : downmixed ? " (mixed down)" : "");
  updateFooter();
}

void Session::soundUnavailable(bool announce) {
  setSound(SoundMode::Off);
  if (announce) toast("No audio device", 1.5);
}

bool Session::runAction() {
  if (world_->interact() <= 0) return false;
  ++actions_;
  toast(scenes_[sceneIdx_]->action.label, 1.2);
  return true;
}

void Session::markProgress() {
  baseDays_ = world_->daysElapsed();
  baseRares_ = world_->raresStarted();
  actions_ = 0;
  actionTimer_ = rng_.range(10.0, 90.0);
}

void Session::setCycle(bool on) {
  if (on == cycle_) return;
  cycle_ = on;
  flush();
  if (on) {  // the clock runs by itself; what the user chose is kept for later
    userTimeMode_ = timeMode_;
    timeMode_ = World::TimeMode::Cycle;
  } else {
    timeMode_ = userTimeMode_;
  }
  world_->setTimeMode(timeMode_);
  syncClock();
  menu_.setValue("mode", on ? 1 : 0);
  menu_.setValue("time", static_cast<size_t>(timeMode_));
  markProgress();
  toast(on ? "Cycle mode" : "User mode", 1.5);
}

bool Session::sceneDone() const {
  const SceneDef& sc = *scenes_[sceneIdx_];
  const bool day = !sc.dayNight || world_->daysElapsed() - baseDays_ >= 1.0;
  const bool rare = sc.rare.empty() || (world_->raresStarted() > baseRares_ && !world_->rareActive());
  const bool act = sc.action.kind == SceneDef::Action::Kind::None || actions_ > 0;
  return day && rare && act;
}

void Session::stepCycle(double dt) {
  actionTimer_ -= dt;
  if (actionTimer_ <= 0) {  // the F key, pressed by nobody
    actionTimer_ = rng_.range(10.0, 90.0);
    runAction();
  }
  // A rare event that has not come yet is waited for: the scene stays on, day after day.
  if (sceneDone()) {
    ++seed_;  // a scene that comes round again looks different
    fadePending_ = true;
    switchScene(sceneIdx_ + 1);
  }
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
    case Action::Interact: setCycle(false); flush(); runAction(); break;
    case Action::Rare: setCycle(false); flush(); world_->triggerRare(); break;
    case Action::RareOther: setCycle(false); flush(); world_->triggerRare(RarePick::Other); break;
    case Action::Mode: setCycle(!cycle_); break;
    case Action::Audio: {  // off, stereo, immersive, and round
      static const char* const names[] = {"off", "stereo", "immersive"};
      const int next = (static_cast<int>(mode_) + 1) % 3;
      setSound(static_cast<SoundMode>(next));
      toast(std::string("Sound: ") + names[next], 1.2);
      break;
    }
    case Action::Redraw: rebuildPending_ = true; ++seed_; break;
    case Action::Style:
      style_ = static_cast<Style>((static_cast<int>(style_) + 1) % 3);
      menu_.setValue("style", static_cast<size_t>(style_));
      repaint_ = true;
      break;
    case Action::Time:
      if (scenes_[sceneIdx_]->dayNight) {
        if (cycle_) {  // choosing the time by hand ends the cycle mode; the clock steps on from "Cycle"
          setCycle(false);
          timeMode_ = World::TimeMode::Cycle;
        }
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
    else if (ev.id == "mode") setCycle(ev.value == 1);
    else if (ev.id == "time") {
      setCycle(false);  // the chosen time of day replaces the automatic one
      flush();
      timeMode_ = static_cast<World::TimeMode>(ev.value);
      world_->setTimeMode(timeMode_);
      syncClock();
      menu_.setValue("time", ev.value);  // leaving the cycle mode reset the entry
    }
    else if (ev.id == "info") info_ = ev.value == 1;
    else if (ev.id == "audio") setSound(static_cast<SoundMode>(ev.value));
    else if (ev.id == "action") { setCycle(false); flush(); runAction(); }
    else if (ev.id == "rare") { setCycle(false); flush(); world_->triggerRare(); }
    else if (ev.id == "rare2") { setCycle(false); flush(); world_->triggerRare(RarePick::Other); }
    else if (ev.id == "resume") menu_.close();
    else if (ev.id == "quit") quit_ = true;
  }
}

void Session::resize(int width, int height) {
  endFade();  // the old world has the old size
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
  if (!paused_) {
    world_->step(dt);
    if (outgoing_) {
      outgoing_->step(dt);
      fadeLeft_ -= dt;
      if (fadeLeft_ <= 0) endFade();
    }
    if (cycle_) stepCycle(dt);
  }
  if (toastLeft_ > 0 && !menu_.isOpen()) toastLeft_ -= dt;
}

void Session::render(Frame& out) {
  flush();
  world_->render(out, style_);
  if (outgoing_) {
    outgoing_->render(fadeFrame_, style_);
    crossfade(fadeFrame_, out, 1.0 - fadeLeft_ / kFadeSeconds);
  }
  if (info_) drawInfo(out, *world_, paused_, fps_, style_);
  if (toastLeft_ > 0 && !menu_.isOpen()) drawToast(out, toast_, style_);
  menu_.render(out, style_);
}

void Session::takeSoundEvents(std::vector<SoundEvent>& out) { world_->takeEvents(out); }

AmbientParams Session::ambient() const {
  AmbientParams p = ambientFor(*world_);
  if (outgoing_) {  // out with the old scene's sound, then in with the new one's
    const double progress = 1.0 - fadeLeft_ / kFadeSeconds;
    if (progress < 0.5) {
      p = ambientFor(*outgoing_);
      p.volume = static_cast<float>(1.0 - 2.0 * progress);
    } else {
      p.volume = static_cast<float>(2.0 * progress - 1.0);
    }
  }
  if (paused_) p.volume = 0;
  return p;
}

}  // namespace dio
