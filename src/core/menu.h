/// @file core/menu.h
/// @brief Modal ASCII menu driven by abstract actions.

#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/frame.h"
#include "core/input.h"

namespace dio {

/// A modal ASCII menu driven only by abstract actions, so the same menu works
/// with a keyboard, the Siri Remote or a gamepad. It owns no behavior: it reports
/// what the user changed as events and the app reacts.
class Menu {
 public:
  /// One entry: a button, or a choice among options.
  struct Item {
    /// Stable identifier used in events.
    std::string id;
    /// Text shown to the user.
    std::string label;
    std::vector<std::string> options;  ///< empty = a button
    size_t value = 0;                  ///< selected option
  };
  /// A change the user made, reported to the app.
  struct Event {
    /// Identifier of the entry that changed or was activated.
    std::string id;
    /// New option index (0 for buttons).
    size_t value = 0;
  };

  /// Creates a closed menu with a title.
  explicit Menu(std::string title) : title_(std::move(title)) {}

  /// Adds an entry with a list of options. Left, right or select cycle through them.
  /// @param value index of the initially selected option
  void addChoice(std::string id, std::string label, std::vector<std::string> options, size_t value = 0);
  /// Adds an entry that only reports an event when it is selected.
  void addButton(std::string id, std::string label);
  void setValue(const std::string& id, size_t value);  ///< no event is emitted
  /// Changes the text of an entry, for example when the scene action changes.
  void setLabel(const std::string& id, std::string label);
  /// Replaces the options of a choice, for example when a scene offers different ones.
  void setOptions(const std::string& id, std::vector<std::string> options, size_t value = 0);
  /// Currently selected option of a choice.
  size_t value(const std::string& id) const;

  /// Shows the menu and starts consuming input.
  void open() { open_ = true; }
  /// Hides the menu.
  void close() { open_ = false; }
  /// True while the menu is showing.
  bool isOpen() const { return open_; }

  /// Returns true if the action was consumed. Quit and anything the menu has no
  /// use for are left to the caller.
  bool handle(Action a);
  /// Returns and clears the events produced since the last call.
  std::vector<Event> takeEvents();

  /// Draws the menu over f if it is open. Falls back to a single line on
  /// frames too small for the box.
  void render(Frame& f, Style style) const;

 private:
  /// Entry with the given id, or nullptr.
  Item* find(const std::string& id);
  /// Moves a choice one option forward or backward (wrapping) and emits an event.
  void step(Item& item, int delta);

  /// Title shown at the top of the box.
  std::string title_;
  /// Entries in display order.
  std::vector<Item> items_;
  /// Index of the highlighted entry.
  size_t selected_ = 0;
  /// True while the menu is showing.
  bool open_ = false;
  /// Events waiting for takeEvents().
  std::vector<Event> events_;
};

}  // namespace dio
