/// @file term/terminal_win.cpp
/// @brief Terminal backend for Windows (console virtual terminal mode).

// Windows console backend. NOTE: written but not yet compiled/tested - the
// first CI run on windows-latest is the real check.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>

#include "term/terminal.h"

namespace dio {

namespace {
/// Set when the console window changes size, cleared by takeResized().
std::atomic<bool> gResized{false};
/// Set by Ctrl-C, Ctrl-Break or closing the console.
std::atomic<bool> gQuit{false};
/// Console control handler: asks the main loop to stop.
BOOL WINAPI onCtrl(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
    gQuit = true;
    return TRUE;
  }
  return FALSE;
}
}  // namespace

/// Platform state of Terminal (Windows: console handles and modes).
struct Terminal::Impl {
  /// Console input handle.
  HANDLE in = INVALID_HANDLE_VALUE;
  /// Console output handle.
  HANDLE out = INVALID_HANDLE_VALUE;
  /// Console modes to restore on exit.
  DWORD savedIn = 0, savedOut = 0;
  /// Output code page to restore on exit.
  UINT savedCp = 0;
  /// True once init() succeeded.
  bool active = false;
};

Terminal::Terminal() : impl_(std::make_unique<Impl>()) {}

Terminal::~Terminal() {
  if (!impl_->active) return;
  write("\x1b[0m\x1b[?25h\x1b[?1049l");
  SetConsoleMode(impl_->in, impl_->savedIn);
  SetConsoleMode(impl_->out, impl_->savedOut);
  SetConsoleOutputCP(impl_->savedCp);
}

bool Terminal::init() {
  impl_->in = GetStdHandle(STD_INPUT_HANDLE);
  impl_->out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleMode(impl_->in, &impl_->savedIn) || !GetConsoleMode(impl_->out, &impl_->savedOut))
    return false;

  DWORD outMode = impl_->savedOut | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
  if (!SetConsoleMode(impl_->out, outMode)) return false;  // pre-Win10 console: no VT support
  SetConsoleMode(impl_->in, ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT);

  impl_->savedCp = GetConsoleOutputCP();
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(onCtrl, TRUE);

  impl_->active = true;
  write("\x1b[?1049h\x1b[?25l\x1b[2J");
  colorMode_ = ColorMode::TrueColor;  // Windows Terminal and Win10 1703+ conhost
  return true;
}

void Terminal::size(int& width, int& height) const {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(impl_->out, &info)) {
    width = info.srWindow.Right - info.srWindow.Left + 1;
    height = info.srWindow.Bottom - info.srWindow.Top + 1;
  } else {
    width = 80;
    height = 24;
  }
}

bool Terminal::takeResized() { return gResized.exchange(false); }
bool Terminal::quitRequested() { return gQuit.load(); }

std::vector<Action> Terminal::poll(int timeoutMs) {
  std::vector<Action> actions;
  if (WaitForSingleObject(impl_->in, static_cast<DWORD>(timeoutMs)) != WAIT_OBJECT_0) return actions;

  INPUT_RECORD rec[32];
  DWORD n = 0;
  if (!ReadConsoleInputW(impl_->in, rec, 32, &n)) return actions;
  for (DWORD i = 0; i < n; ++i) {
    if (rec[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
      gResized = true;
      continue;
    }
    if (rec[i].EventType != KEY_EVENT || !rec[i].Event.KeyEvent.bKeyDown) continue;
    const KEY_EVENT_RECORD& k = rec[i].Event.KeyEvent;
    switch (k.wVirtualKeyCode) {
      case VK_UP: actions.push_back(Action::Up); continue;
      case VK_DOWN: actions.push_back(Action::Down); continue;
      case VK_LEFT: actions.push_back(Action::Left); continue;
      case VK_RIGHT: actions.push_back(Action::Right); continue;
      case VK_RETURN: actions.push_back(Action::Select); continue;
      case VK_ESCAPE: actions.push_back(Action::Back); continue;
      case VK_SPACE: actions.push_back(Action::Pause); continue;
      default: break;
    }
    switch (k.uChar.UnicodeChar) {
      case L'q': case L'Q': actions.push_back(Action::Quit); break;
      case L'p': case L'P': actions.push_back(Action::Pause); break;
      case L'f': case L'F': actions.push_back(Action::Feed); break;
      case L'i': case L'I': actions.push_back(Action::Info); break;
      case L'r': case L'R': actions.push_back(Action::Redraw); break;
      default: break;
    }
  }
  return actions;
}

void Terminal::write(const std::string& bytes) {
  DWORD written = 0;
  size_t off = 0;
  while (off < bytes.size()) {
    if (!WriteFile(impl_->out, bytes.data() + off, static_cast<DWORD>(bytes.size() - off), &written, nullptr))
      return;
    off += written;
  }
}

}  // namespace dio
