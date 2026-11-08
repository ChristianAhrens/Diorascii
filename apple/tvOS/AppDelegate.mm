/// @file AppDelegate.mm
/// @brief Entry point of the tvOS app: app delegate, scene delegate and main().

#import <UIKit/UIKit.h>

#import "SessionViewController.h"

/// Application-level callbacks: keeps the television awake while the scene runs. The window
/// itself belongs to the scene (see SceneDelegate).
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  application.idleTimerDisabled = YES;
  return YES;
}

@end

/// Creates the full-screen window with the session view controller when the system connects
/// the app's scene (required since the UIScene lifecycle became mandatory).
@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation SceneDelegate

- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)connectionOptions {
  UIWindowScene* windowScene = (UIWindowScene*)scene;
  self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
  self.window.rootViewController = [[SessionViewController alloc] init];
  [self.window makeKeyAndVisible];
}

@end

int main(int argc, char* argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
  }
}
