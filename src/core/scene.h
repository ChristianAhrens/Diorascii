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
  ///   weather <sprite> [<funnel>]  rain or a sandstorm for a while (the sprite is the drop or grain);
  ///                    with a funnel sprite, half of the time a tornado crosses the scene instead
  struct Action {
    /// Which action the scene offers.
    enum class Kind { None, Drop, Impact, Weather };
    /// What the action key does.
    Kind kind = Kind::None;
    /// Sprite that is dropped or hurled, depending on kind.
    std::string sprite;
    std::string extra;  ///< optional second sprite: the funnel of a tornado that sometimes replaces the storm
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
  /// True for a mountain scene: the whole height is a landscape. A meadow with a stream at the
  /// bottom, a forest belt, bare rock and snow on top of the peaks, and the sky above them. The
  /// ground-colors and ground-glyphs are the meadow's.
  bool mountains = false;
  /// Altitude of the highest peak, the tree line and the snow line, as shares of the height.
  double peakHeight = 0.86, treeline = 0.45, snowline = 0.68;
  /// Colors of the forest belt, the rock and the snow, each from the lower to the higher part.
  Rgb forestA{52, 104, 58}, forestB{36, 78, 44};
  Rgb rockA{126, 122, 118}, rockB{96, 92, 94};
  Rgb snowA{238, 244, 250}, snowB{206, 220, 238};
  /// Characters scattered over the forest belt, the rock and the snow.
  std::u32string forestGlyphs = U"^\"'^ ,", rockGlyphs = U".:  ,'", snowGlyphs = U"*. .  ";
  /// Rows at the bottom of the ground that are a flowing river (0 = no river).
  int riverRows = 0;
  /// River colors from the bank down to the deepest row.
  Rgb riverTop{74, 160, 216}, riverDeep{31, 106, 165};
  double riverFlow = 2.5;         ///< cells per second the ripples drift to the right
  double weatherDuration = 20;    ///< seconds a weather action lasts
  /// Color the scene turns towards under heavy weather (ochre for a sandstorm), and how strongly.
  Rgb overcastTint{0, 0, 0};
  /// How strongly the scene turns towards overcastTint at full cloud cover, 0 to 1 (0 = no tint).
  double overcastTintAmount = 0;
  /// True for a scene without an atmosphere (the moon): the cycle then only brightens and darkens
  /// the ground a little, while the sky stays black and the stars stay out, and there is no glow.
  bool airless = false;
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
  /// Rare catastrophes (a kraken, a thunderstorm...): one is picked now and then. They run on their
  /// own, much slower timer than events and wipe out life when they strike.
  std::vector<Event> rare;
  /// Seconds until the first rare event, and the pause between later ones.
  std::pair<double, double> rareFirst{150, 300}, rareInterval{300, 600};

  /// Parses the text scene format. Returns nullptr and sets err on failure.
  static std::shared_ptr<const SceneDef> parse(std::string_view text, std::string& err);
};

}  // namespace dio
