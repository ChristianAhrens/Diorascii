/// @file SessionViewController.mm
/// @brief Runs a dio::Session on the Apple TV: timing, Siri Remote input, drawing and sound.

#import "SessionViewController.h"

#include <chrono>
#include <memory>

#include "app/localclock.h"
#include "app/session.h"
#include "audio/audio.h"
#include "core/assets.h"
#include "GridView.h"

/// Input diagnostics in Debug builds: what the remote delivers, and whether the grid has focus.
#ifdef NDEBUG
#define DIO_INPUT_LOG(...)
#else
#define DIO_INPUT_LOG(...) NSLog(@"DIORASCII-INPUT " __VA_ARGS__)
#endif

/// The simulation step, as in the terminal program.
static const double kStep = 1.0 / 20;

/// The hint about the menu appears this many seconds after the start (the scene title is
/// shown before it), for this many seconds, and only for the first few launches.
static const double kHintDelay = 2.5, kHintDuration = 5.0;
static const NSInteger kHintLaunches = 5;
static NSString* const kHintCountKey = @"menuHintCount";

@implementation SessionViewController {
  /// Sprites and scenes, built into the app.
  dio::AssetPack _pack;
  /// The application state; created once the grid size is known.
  std::unique_ptr<dio::Session> _session;
  /// Ambient sound.
  dio::AudioOutput _audio;
  /// Draws the session's frame.
  GridView* _grid;
  dio::Frame _frame;
  CADisplayLink* _link;
  /// A long press already opened the menu, so the release must not also run the action.
  BOOL _longPressHandled;
  /// Seconds since the session started, and whether this launch still has to show the menu hint.
  double _elapsed;
  BOOL _hintPending;
}

- (void)loadView {
  _grid = [[GridView alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.view = _grid;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  _pack = dio::AssetPack::embedded();

  // Swipes on the remote's touch surface. Vertical swipes move in the menu while it is open;
  // otherwise up changes the time of day and down switches the sound.
  const UISwipeGestureRecognizerDirection directions[] = {
      UISwipeGestureRecognizerDirectionLeft, UISwipeGestureRecognizerDirectionRight,
      UISwipeGestureRecognizerDirectionUp, UISwipeGestureRecognizerDirectionDown};
  for (auto direction : directions) {
    UISwipeGestureRecognizer* swipe =
        [[UISwipeGestureRecognizer alloc] initWithTarget:self action:@selector(swiped:)];
    swipe.direction = direction;
    swipe.allowedTouchTypes = @[ @(UITouchTypeIndirect) ];
    [self.view addGestureRecognizer:swipe];
  }
  // Holding the touch surface's click opens the menu.
  UILongPressGestureRecognizer* hold =
      [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(held:)];
  hold.allowedPressTypes = @[ @(UIPressTypeSelect) ];
  hold.minimumPressDuration = 0.6;
  [self.view addGestureRecognizer:hold];
}

/// The grid takes focus, which is what makes the Siri Remote's touches and presses arrive.
- (NSArray<id<UIFocusEnvironment>>*)preferredFocusEnvironments {
  return @[ _grid ];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  const int cols = _grid.columns, rows = _grid.rows;
  if (!_session) {
    auto scenes = _pack.scenes();
    dio::Session::Config config;
    config.seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    config.sound = true;      // a television app is expected to play its sound
    config.quitEntry = false;  // the Menu button leaves the app
    _session = std::make_unique<dio::Session>(_pack, scenes, config, cols, rows);
    NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
    const NSInteger launches = [defaults integerForKey:kHintCountKey];
    _hintPending = launches < kHintLaunches;
    [defaults setInteger:launches + 1 forKey:kHintCountKey];
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    _link.preferredFramesPerSecond = 20;
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
  } else if (cols != _session->world().width() || rows != _session->world().height()) {
    _session->resize(cols, rows);
  }
}

/// One frame: step, sound, draw.
- (void)tick:(CADisplayLink*)link {
  _session->setClockHour(dio::localClockHour());
  _session->step(kStep);
  _elapsed += kStep;
  if (_hintPending && _elapsed >= kHintDelay) {
    _hintPending = NO;
    if (![self menuOpen]) _session->toast("Hold the click for the menu", kHintDuration);
  }
  static int ticks = 0;
  if (++ticks % 100 == 1) DIO_INPUT_LOG(@"grid focused=%d window key=%d", _grid.isFocused, self.view.window.isKeyWindow);
  [self syncSound];
  if (_audio.running()) _audio.setParams(_session->ambient());
  _session->render(_frame);
  _session->takeRepaint();  // the view redraws every cell each frame anyway
  [_grid showFrame:_frame];
}

/// Opens or closes the sound device to match what the session wants.
- (void)syncSound {
  if (_session->soundWanted() && !_audio.running() && !_audio.start()) _session->soundUnavailable();
  if (!_session->soundWanted() && _audio.running()) _audio.stop();
}

/// True while the ASCII menu is open and takes the navigation.
- (BOOL)menuOpen {
  return _session && _session->menu().isOpen();
}

- (void)swiped:(UISwipeGestureRecognizer*)swipe {
  DIO_INPUT_LOG(@"swipe direction=%ld", (long)swipe.direction);
  if (!_session) return;
  switch (swipe.direction) {
    case UISwipeGestureRecognizerDirectionLeft: _session->handle(dio::Action::Left); break;
    case UISwipeGestureRecognizerDirectionRight: _session->handle(dio::Action::Right); break;
    case UISwipeGestureRecognizerDirectionUp:
      _session->handle([self menuOpen] ? dio::Action::Up : dio::Action::Time);
      break;
    case UISwipeGestureRecognizerDirectionDown:
      _session->handle([self menuOpen] ? dio::Action::Down : dio::Action::Audio);
      break;
    default: break;
  }
}

- (void)held:(UILongPressGestureRecognizer*)hold {
  DIO_INPUT_LOG(@"hold state=%ld", (long)hold.state);
  if (hold.state != UIGestureRecognizerStateBegan || !_session) return;
  _longPressHandled = YES;
  if (![self menuOpen]) _session->handle(dio::Action::Menu);
}

/// Maps a keyboard key (the Simulator's keyboard, or a keyboard on a real Apple TV) to the same
/// actions the remote produces; the letters are the terminal program's keys. Returns whether the
/// key was used.
- (BOOL)handleKey:(UIKey*)key {
  const BOOL menu = [self menuOpen];
  switch (key.keyCode) {
    case UIKeyboardHIDUsageKeyboardLeftArrow: _session->handle(dio::Action::Left); return YES;
    case UIKeyboardHIDUsageKeyboardRightArrow: _session->handle(dio::Action::Right); return YES;
    case UIKeyboardHIDUsageKeyboardUpArrow:
      _session->handle(menu ? dio::Action::Up : dio::Action::Time);
      return YES;
    case UIKeyboardHIDUsageKeyboardDownArrow:
      _session->handle(menu ? dio::Action::Down : dio::Action::Audio);
      return YES;
    case UIKeyboardHIDUsageKeyboardReturnOrEnter:
      _session->handle(menu ? dio::Action::Select : dio::Action::Interact);
      return YES;
    case UIKeyboardHIDUsageKeyboardSpacebar: _session->handle(dio::Action::Pause); return YES;
    case UIKeyboardHIDUsageKeyboardEscape:
      if (!menu) return NO;
      _session->handle(dio::Action::Back);
      return YES;
    default: break;
  }
  NSString* letter = key.charactersIgnoringModifiers.lowercaseString;
  if (letter.length != 1) return NO;
  switch ([letter characterAtIndex:0]) {
    case 'm': _session->handle(dio::Action::Menu); return YES;
    case 'f': _session->handle(dio::Action::Interact); return YES;
    case 'x': _session->handle(dio::Action::Rare); return YES;
    case 'c': _session->handle(dio::Action::Style); return YES;
    case 't': _session->handle(dio::Action::Time); return YES;
    case 'a': _session->handle(dio::Action::Audio); return YES;
    case 'i': _session->handle(dio::Action::Info); return YES;
    case 'p': _session->handle(dio::Action::Pause); return YES;
    case 'r': _session->handle(dio::Action::Redraw); return YES;
    default: return NO;
  }
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
  BOOL handled = NO;
  for (UIPress* press in presses) {
    DIO_INPUT_LOG(@"pressesEnded type=%ld key=%ld", (long)press.type, (long)press.key.keyCode);
    if (press.key) {
      if ([self handleKey:press.key]) handled = YES;
      continue;
    }
    switch (press.type) {
      case UIPressTypeSelect:
        if (_longPressHandled) _longPressHandled = NO;
        else _session->handle([self menuOpen] ? dio::Action::Select : dio::Action::Interact);
        handled = YES;
        break;
      case UIPressTypePlayPause: _session->handle(dio::Action::Pause); handled = YES; break;
      case UIPressTypeLeftArrow: _session->handle(dio::Action::Left); handled = YES; break;
      case UIPressTypeRightArrow: _session->handle(dio::Action::Right); handled = YES; break;
      case UIPressTypeUpArrow:
        _session->handle([self menuOpen] ? dio::Action::Up : dio::Action::Time);
        handled = YES;
        break;
      case UIPressTypeDownArrow:
        _session->handle([self menuOpen] ? dio::Action::Down : dio::Action::Audio);
        handled = YES;
        break;
      case UIPressTypeMenu:
        // Closes the ASCII menu; with the menu closed the system takes over and leaves the app.
        if ([self menuOpen]) { _session->handle(dio::Action::Back); handled = YES; }
        break;
      default: break;
    }
  }
  if (!handled) [super pressesEnded:presses withEvent:event];
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
  // The remote acts on release (pressesEnded). The press is swallowed so the system does not
  // react to it, except Menu with the menu closed, which must reach the system to leave the app.
  for (UIPress* press in presses) {
    DIO_INPUT_LOG(@"pressesBegan type=%ld", (long)press.type);
    if (press.type == UIPressTypeMenu && ![self menuOpen]) {
      [super pressesBegan:presses withEvent:event];
      return;
    }
  }
}

@end
