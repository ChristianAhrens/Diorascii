/// @file term/terminal.h
/// @brief Raw-mode terminal session (input, size, output).

#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/input.h"
#include "term/vt.h"

namespace dio {

/// Raw-mode terminal session. Platform code lives in terminal_posix.cpp and
/// terminal_win.cpp; everything above it is shared.
class Terminal {
 public:
  /// Creates the session object. The terminal is not touched until init().
  Terminal();
  ~Terminal();  ///< restores the terminal
  /// Not copyable: the object owns the terminal state.
  Terminal(const Terminal&) = delete;
  /// Not assignable: the object owns the terminal state.
  Terminal& operator=(const Terminal&) = delete;

  bool init();  ///< false if stdin/stdout is not an interactive terminal
  /// Current size in columns and rows (80 by 24 if it cannot be determined).
  void size(int& width, int& height) const;
  bool takeResized();      ///< true once after each window size change
  bool quitRequested();    ///< set by SIGINT/SIGTERM/Ctrl-C
  /// Waits up to `timeoutMs` for input and returns the actions it contained.
  std::vector<Action> poll(int timeoutMs);
  /// Writes raw bytes (VT escape sequences and text) to the terminal.
  void write(const std::string& bytes);
  /// Color encoding used for this terminal.
  ColorMode colorMode() const { return colorMode_; }
  /// Overrides the detected color encoding.
  void setColorMode(ColorMode m) { colorMode_ = m; }

 private:
  struct Impl;
  /// Platform state.
  std::unique_ptr<Impl> impl_;
  /// Detected or forced color encoding.
  ColorMode colorMode_ = ColorMode::Ansi256;
};

}  // namespace dio
