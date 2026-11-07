/// @file core/scene.h
/// @brief Scene definitions: the data that describes a whole world.

#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/color.h"

namespace dio {

/// A scene is pure data (see assets/scenes/*.scene): the look of the backdrop,
/// which sprites populate it, which decorations stand in it and which rare
/// "events" (ship, shark, meteor...) pass through from time to time.
struct SceneDef {
  /// How many of a sprite a scene starts with.
  struct Population {
    /// How the amount is interpreted.
    enum class Kind { PerCells, PerCols, Count };
    std::string sprite;  ///< sprite name, or "@seaweed" for the procedural plant
    /// Whether n counts per cell area, per column, or is an absolute count.
    Kind kind = Kind::Count;
    /// Amount; see kind.
    double n = 1;
  };
  /// A decoration placed once when the scene starts.
  struct Decor {
    /// Sprite name.
    std::string sprite;
    std::string align = "left";  ///< left | right | center | random
    /// Distance in cells from the aligned edge.
    int margin = 0;
    /// True if y is set explicitly. Otherwise the sprite habitat decides the row.
    bool hasY = false;
    int y = 0;  ///< row; negative counts from the bottom
  };
  /// A rare visitor (ship, shark, meteor...) that the scene sends one at a time.
  struct Event {
    /// Sprite name.
    std::string sprite;
    /// Relative chance of being picked.
    double weight = 1;
  };
  /// What the F key does in this scene.
  ///   drop   <sprite>  items fall from the surface (food for the fish)
  ///   impact <sprite>  a meteorite strikes the ground and digs a crater
  ///   weather <sprite> it rains for a while (the sprite is the raindrop)
  struct Action {
    /// Which action the scene offers.
    enum class Kind { None, Drop, Impact, Weather };
    /// What the action key does.
    Kind kind = Kind::None;
    /// Sprite that is dropped or hurled, depending on kind.
    std::string sprite;
    std::string label = "No action";  ///< shown on the menu button
  };

  /// Unique name, and the title shown in the menu and toast.
  std::string name, title;
  /// Sort key in the scene list. Lower comes first.
  int order = 100;

  /// Vertical layout: sky | medium (water) | ground.
  bool hasMedium = true;
  double skyFraction = 0.2;  ///< >= 1: no medium, the whole area above the ground is sky
  /// Limits for the number of sky rows above the surface.
  int skyRowsMin = 1, skyRowsMax = 5;
  /// Rows of ground at the bottom.
  int groundRows = 2;

  /// Sky gradient from the top row down to the horizon.
  Rgb skyTop{6, 8, 28}, skyLow{34, 52, 98};
  /// Medium (water) gradient from the surface down to the deep.
  Rgb mediumTop{30, 120, 150}, mediumDeep{5, 22, 66};
  /// Ground colors: the top ground row, and the rows below it.
  Rgb groundA{150, 124, 82}, groundB{118, 96, 60};
  /// Color of the surface line glyphs.
  Rgb surfaceFg{175, 232, 245};
  /// Characters scattered over the ground.
  std::u32string groundGlyphs = U".,'`. : ";
  std::u32string surfaceGlyphs = U"~-_";  ///< crest, mid, trough; empty = no surface line
  int starDensity = 0;                   ///< one star per N sky cells, 0 = none

  /// What the action key does in this scene.
  Action action;
  double gravity = 6;      ///< cells/s^2 for falling debris
  double dustSettle = 40;  ///< seconds for dust to fill one row of a crater
  /// Rows at the bottom of the ground that are a flowing river (0 = no river).
  int riverRows = 0;
  /// River colors from the bank down to the deepest row.
  Rgb riverTop{74, 160, 216}, riverDeep{31, 106, 165};
  double riverFlow = 2.5;         ///< cells per second the ripples drift to the right
  double weatherDuration = 20;    ///< seconds a weather action lasts
  /// True if the scene has a day and night cycle (sun, moon, stars, dimming light).
  bool dayNight = false;
  double dayLength = 180;  ///< seconds one full day takes
  double startHour = 10;   ///< time of day when the scene starts, 0 to 24
  /// Sprites crossing the sky by day and by night (optional, may be empty).
  std::string sunSprite, moonSprite;
  std::pair<double, double> eventFirst{3, 8}, eventInterval{15, 40};  ///< seconds

  /// Sprites the scene starts with.
  std::vector<Population> population;
  /// Decorations placed when the scene starts.
  std::vector<Decor> decor;
  /// Visitors scheduled over time.
  std::vector<Event> events;

  /// Parses the text scene format. Returns nullptr and sets err on failure.
  static std::shared_ptr<const SceneDef> parse(std::string_view text, std::string& err);
};

}  // namespace dio
