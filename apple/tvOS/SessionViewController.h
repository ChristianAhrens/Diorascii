/// @file SessionViewController.h
/// @brief Runs a dio::Session on the Apple TV: timing, Siri Remote input, drawing and sound.

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Owns the Session, a GridView that shows it, and the sound output. A display link steps the
/// simulation at the terminal's 20 frames per second; the Siri Remote (and game controllers or
/// keyboards, which the simulator uses) is translated into the same abstract actions the
/// terminal frontend produces.
@interface SessionViewController : UIViewController
@end

NS_ASSUME_NONNULL_END
