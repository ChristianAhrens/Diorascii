/// @file term/terminal_posix.cpp
/// @brief Terminal backend for macOS and Linux (termios, poll, signals).

#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "term/terminal.h"

namespace dio {

namespace {
/// Set by the SIGWINCH handler, cleared by takeResized().
std::atomic<bool> gResized{false};
/// Set by the SIGINT and SIGTERM handlers.
std::atomic<bool> gQuit{false};
/// SIGWINCH handler: remembers that the window size changed.
void onWinch(int) { gResized = true; }
/// SIGINT and SIGTERM handler: asks the main loop to stop.
void onQuit(int) { gQuit = true; }

/// True on the Linux text console (TERM=linux), also from inside tmux, which hides the
/// outer terminal behind its own TERM but can tell us what the attached client uses.
bool onLinuxConsole() {
  auto isLinux = [](const char* t) { return t && std::strncmp(t, "linux", 5) == 0; };
  if (isLinux(std::getenv("TERM"))) return true;
  if (!std::getenv("TMUX")) return false;
  FILE* p = popen("tmux display-message -p '#{client_termname}' 2>/dev/null", "r");
  if (!p) return false;
  char buf[64] = {};
  const bool got = std::fgets(buf, sizeof buf, p) != nullptr;
  pclose(p);
  return got && isLinux(buf);
}

/// Writes the whole buffer to stdout, retrying after interruptions.
void writeAll(const char* data, size_t n) {
  while (n > 0) {
    const ssize_t w = ::write(STDOUT_FILENO, data, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return;
    }
    data += w;
    n -= static_cast<size_t>(w);
  }
}
}  // namespace

/// Platform state of Terminal (POSIX: termios settings and signal handlers).
struct Terminal::Impl {
  /// Terminal settings to restore on exit.
  termios saved{};
  /// True once init() succeeded.
  bool active = false;
  /// Signal handlers that were installed before ours.
  struct sigaction oldWinch {}, oldInt {}, oldTerm {};
};

Terminal::Terminal() : impl_(std::make_unique<Impl>()) {}

Terminal::~Terminal() {
  if (!impl_->active) return;
  // Show cursor, reset attributes, leave the alternate screen.
  static const char kLeave[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
  writeAll(kLeave, sizeof(kLeave) - 1);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &impl_->saved);
  sigaction(SIGWINCH, &impl_->oldWinch, nullptr);
  sigaction(SIGINT, &impl_->oldInt, nullptr);
  sigaction(SIGTERM, &impl_->oldTerm, nullptr);
}

bool Terminal::init() {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
  if (tcgetattr(STDIN_FILENO, &impl_->saved) != 0) return false;

  termios raw = impl_->saved;
  raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);  // keep ISIG so Ctrl-C still signals
  raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;

  struct sigaction sa {};
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = onWinch;
  sigaction(SIGWINCH, &sa, &impl_->oldWinch);
  sa.sa_handler = onQuit;
  sigaction(SIGINT, &sa, &impl_->oldInt);
  sigaction(SIGTERM, &sa, &impl_->oldTerm);

  impl_->active = true;
  static const char kEnter[] = "\x1b[?1049h\x1b[?25l\x1b[2J";
  writeAll(kEnter, sizeof(kEnter) - 1);

  const char* ct = std::getenv("COLORTERM");
  if (ct && (std::strstr(ct, "truecolor") || std::strstr(ct, "24bit"))) {
    colorMode_ = ColorMode::TrueColor;
  }
  if (onLinuxConsole()) colorMode_ = ColorMode::Console;  // 16 colors whatever COLORTERM claims
  return true;
}

void Terminal::size(int& width, int& height) const {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
    width = ws.ws_col;
    height = ws.ws_row;
  } else {
    width = 80;
    height = 24;
  }
}

bool Terminal::takeResized() { return gResized.exchange(false); }
bool Terminal::quitRequested() { return gQuit.load(); }

std::vector<Action> Terminal::poll(int timeoutMs) {
  std::vector<Action> actions;
  pollfd pfd{STDIN_FILENO, POLLIN, 0};
  if (::poll(&pfd, 1, timeoutMs) <= 0) return actions;

  char buf[64];
  const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
  for (ssize_t i = 0; i < n; ++i) {
    switch (buf[i]) {
      case 'q': case 'Q': case 3: actions.push_back(Action::Quit); break;
      case 'p': case 'P': actions.push_back(Action::Pause); break;
      case 'f': case 'F': actions.push_back(Action::Interact); break;
      case 'i': case 'I': actions.push_back(Action::Info); break;
      case 'r': case 'R': actions.push_back(Action::Redraw); break;
      case 'm': case 'M': actions.push_back(Action::Menu); break;
      case 'c': case 'C': actions.push_back(Action::Style); break;
      case 't': case 'T': actions.push_back(Action::Time); break;
      case 'x': case 'X': actions.push_back(Action::Rare); break;
      case ' ': actions.push_back(Action::Pause); break;
      case '\r': case '\n': actions.push_back(Action::Select); break;
      case 0x1b:
        if (i + 2 < n && buf[i + 1] == '[') {
          switch (buf[i + 2]) {
            case 'A': actions.push_back(Action::Up); break;
            case 'B': actions.push_back(Action::Down); break;
            case 'C': actions.push_back(Action::Right); break;
            case 'D': actions.push_back(Action::Left); break;
            default: break;
          }
          i += 2;
        } else {
          actions.push_back(Action::Back);
        }
        break;
      default: break;
    }
  }
  return actions;
}

void Terminal::write(const std::string& bytes) { writeAll(bytes.data(), bytes.size()); }

}  // namespace dio
