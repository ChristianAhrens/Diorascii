/// @file core/scene.cpp
/// @brief Scene file parsing.

#include "core/scene.h"

#include <algorithm>
#include <cstdlib>

#include "core/sprite.h"
#include "core/textutil.h"
#include "core/utf8.h"

namespace dio {

namespace {

/// Prefixes a parse error with its line number.
std::string at(int line, const std::string& msg) { return "line " + std::to_string(line) + ": " + msg; }

/// Parses count hex colors from header key into out. Absent keys leave out untouched.
bool colors(const Props& p, const char* key, Rgb* out, size_t count) {
  if (!p.has(key)) return true;
  const auto toks = p.list(key);
  if (toks.size() != count) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!parseHexColor(toks[i], out[i])) return false;
  }
  return true;
}

}  // namespace

std::shared_ptr<const SceneDef> SceneDef::parse(std::string_view text, std::string& err) {
  auto sc = std::make_shared<SceneDef>();
  Props p;
  enum class Sec { Header, Population, Decor, Events, Rare } sec = Sec::Header;

  size_t pos = 0;
  int lineNo = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    const std::string line = trim(text.substr(pos, nl - pos));
    pos = nl + 1;
    ++lineNo;
    if (line.empty() || line[0] == '#') continue;

    if (line.substr(0, 3) == "---") {
      const std::string kind = lower(trim(line.substr(3)));
      if (kind == "population") sec = Sec::Population;
      else if (kind == "decor") sec = Sec::Decor;
      else if (kind == "events") sec = Sec::Events;
      else if (kind == "rare") sec = Sec::Rare;
      else { err = at(lineNo, "unknown section '" + kind + "'"); return nullptr; }
      continue;
    }

    if (sec == Sec::Header) {
      const size_t colon = line.find(':');
      if (colon == std::string::npos) { err = at(lineNo, "expected 'key: value'"); return nullptr; }
      p.set(lower(trim(line.substr(0, colon))), trim(line.substr(colon + 1)));
      continue;
    }

    const auto t = splitWs(line);
    if (sec == Sec::Population) {
      // <sprite> per-cells N | per-cols N | count N
      if (t.size() != 3) { err = at(lineNo, "expected '<sprite> per-cells|per-cols|count <n>'"); return nullptr; }
      Population pop;
      pop.sprite = t[0];
      pop.n = std::atof(t[2].c_str());
      if (t[1] == "per-cells") pop.kind = Population::Kind::PerCells;
      else if (t[1] == "per-cols") pop.kind = Population::Kind::PerCols;
      else if (t[1] == "count") pop.kind = Population::Kind::Count;
      else { err = at(lineNo, "unknown population mode '" + t[1] + "'"); return nullptr; }
      if (pop.n <= 0) { err = at(lineNo, "population number must be > 0"); return nullptr; }
      sc->population.push_back(pop);
    } else if (sec == Sec::Decor) {
      // <sprite> left|right|center|random <margin> [y <row>]
      if (t.size() != 3 && t.size() != 5) { err = at(lineNo, "expected '<sprite> <align> <margin> [y <row>]'"); return nullptr; }
      Decor d;
      d.sprite = t[0];
      d.align = t[1];
      if (d.align != "left" && d.align != "right" && d.align != "center" && d.align != "random") {
        err = at(lineNo, "unknown alignment '" + d.align + "'");
        return nullptr;
      }
      d.margin = std::atoi(t[2].c_str());
      if (t.size() == 5) {
        if (t[3] != "y") { err = at(lineNo, "expected 'y <row>'"); return nullptr; }
        d.hasY = true;
        d.y = std::atoi(t[4].c_str());
      }
      sc->decor.push_back(d);
    } else {
      // <sprite> <weight>
      if (t.size() != 2) { err = at(lineNo, "expected '<sprite> <weight>'"); return nullptr; }
      (sec == Sec::Rare ? sc->rare : sc->events).push_back({t[0], std::atof(t[1].c_str())});
    }
  }

  sc->name = p.str("name");
  if (sc->name.empty()) { err = "missing 'name:' header"; return nullptr; }
  sc->title = p.str("title", sc->name);
  sc->order = static_cast<int>(p.num("order", 100));
  sc->hasMedium = p.str("medium", "yes") != "no";
  sc->skyFraction = p.num("sky-fraction", sc->skyFraction);
  const auto rows = p.range("sky-rows", {1, 5});
  sc->skyRowsMin = static_cast<int>(rows.first);
  sc->skyRowsMax = static_cast<int>(rows.second);
  sc->groundRows = static_cast<int>(p.num("ground-rows", 2));
  sc->starDensity = static_cast<int>(p.num("stars", 0));
  sc->gravity = p.num("gravity", sc->gravity);
  sc->dustSettle = std::max(p.num("dust-settle", sc->dustSettle), 1.0);
  sc->riverRows = std::max(static_cast<int>(p.num("river-rows", 0)), 0);
  sc->riverFlow = p.num("river-flow", sc->riverFlow);
  sc->weatherDuration = std::max(p.num("weather-duration", sc->weatherDuration), 1.0);
  if (p.has("overcast-tint")) {
    const auto t = p.list("overcast-tint");
    if (t.size() != 2 || !parseHexColor(t[0], sc->overcastTint)) {
      err = "'overcast-tint' needs a '#rrggbb' color and a strength from 0 to 1";
      return nullptr;
    }
    sc->overcastTintAmount = std::clamp(std::atof(t[1].c_str()), 0.0, 1.0);
  }
  sc->dayNight = p.str("day-night", "no") == "yes";
  sc->dayLength = std::max(p.num("day-length", sc->dayLength), 1.0);
  sc->startHour = std::clamp(p.num("start-hour", sc->startHour), 0.0, 24.0);
  sc->sunSprite = p.str("sun-sprite");
  sc->moonSprite = p.str("moon-sprite");
  if (p.has("action")) {
    const auto t = p.list("action");
    if (t.size() == 1 && t[0] == "none") {
      sc->action.kind = Action::Kind::None;
    } else if ((t.size() == 2 && (t[0] == "drop" || t[0] == "impact" || t[0] == "weather")) ||
               (t.size() == 3 && t[0] == "weather")) {
      sc->action.kind = t[0] == "drop"     ? Action::Kind::Drop
                        : t[0] == "impact" ? Action::Kind::Impact
                                           : Action::Kind::Weather;
      sc->action.sprite = t[1];
      if (t.size() == 3) sc->action.extra = t[2];
    } else {
      err = "'action' must be 'none', 'drop <sprite>', 'impact <sprite>' or 'weather <sprite> [<funnel>]'";
      return nullptr;
    }
  }
  sc->action.label = p.str("action-label", sc->action.kind == Action::Kind::None ? "No action" : "Action");
  sc->eventFirst = p.range("event-first", sc->eventFirst);
  sc->eventInterval = p.range("event-interval", sc->eventInterval);
  sc->rareFirst = p.range("rare-first", sc->rareFirst);
  sc->rareInterval = p.range("rare-interval", sc->rareInterval);
  if (p.has("ground-glyphs")) sc->groundGlyphs = decodeUtf8(p.str("ground-glyphs"));
  if (p.has("surface-glyphs")) {
    sc->surfaceGlyphs = decodeUtf8(p.str("surface-glyphs"));
    if (!sc->surfaceGlyphs.empty() && sc->surfaceGlyphs.size() != 3) {
      err = "'surface-glyphs' needs exactly 3 characters (crest, mid, trough) or nothing";
      return nullptr;
    }
  }

  Rgb sky[2] = {sc->skyTop, sc->skyLow}, med[2] = {sc->mediumTop, sc->mediumDeep},
      gnd[2] = {sc->groundA, sc->groundB}, sur[1] = {sc->surfaceFg},
      riv[2] = {sc->riverTop, sc->riverDeep};
  if (!colors(p, "sky", sky, 2) || !colors(p, "medium-colors", med, 2) ||
      !colors(p, "ground-colors", gnd, 2) || !colors(p, "surface-color", sur, 1) ||
      !colors(p, "river-colors", riv, 2)) {
    err = "color headers need '#rrggbb' values (sky, medium-colors, ground-colors, river-colors: two; surface-color: one)";
    return nullptr;
  }
  sc->skyTop = sky[0]; sc->skyLow = sky[1];
  sc->mediumTop = med[0]; sc->mediumDeep = med[1];
  sc->groundA = gnd[0]; sc->groundB = gnd[1];
  sc->surfaceFg = sur[0];
  sc->riverTop = riv[0]; sc->riverDeep = riv[1];
  return sc;
}

}  // namespace dio
