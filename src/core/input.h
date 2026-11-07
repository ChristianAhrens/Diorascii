/// @file core/input.h
/// @brief Abstract input actions every frontend translates its devices into.

#pragma once

namespace dio {

/// Frontends translate platform input (keyboard, Siri Remote, gamepad...) into
/// these abstract actions; the core never sees raw key codes.
enum class Action {
  /// No input.
  None,
  /// Leave the program.
  Quit,
  /// Toggle pausing the simulation.
  Pause,
  /// Drop food into the scene.
  Feed,
  /// Toggle the info line.
  Info,
  /// Rebuild the scene from a new seed.
  Redraw,
  /// Move up (menu selection).
  Up,
  /// Move down (menu selection).
  Down,
  /// Move left (menu value, or previous scene).
  Left,
  /// Move right (menu value, or next scene).
  Right,
  /// Confirm (opens the menu, activates an entry).
  Select,
  /// Cancel (opens the menu, or closes it).
  Back,
  Menu,   ///< open / close the menu
  Style,  ///< cycle full -> terminal -> mono colors
};

}  // namespace dio
