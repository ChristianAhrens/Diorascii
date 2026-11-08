/// @file GridView.h
/// @brief A view that draws a dio::Frame as a grid of monospace character cells.

#import <UIKit/UIKit.h>

#include "core/frame.h"

NS_ASSUME_NONNULL_BEGIN

/// Draws a frame with a bundled monospace font: a filled rectangle per cell background and one
/// glyph per cell. The number of columns and rows follows from the view size and the font.
@interface GridView : UIView

/// Columns that fit into the view's safe area.
@property(nonatomic, readonly) int columns;
/// Rows that fit into the view's safe area.
@property(nonatomic, readonly) int rows;

/// Shows `frame` (copied) on the next draw.
- (void)showFrame:(const dio::Frame&)frame;

@end

NS_ASSUME_NONNULL_END
