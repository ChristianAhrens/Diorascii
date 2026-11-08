/// @file GridView.mm
/// @brief A view that draws a dio::Frame as a grid of monospace character cells.

#import "GridView.h"

#import <CoreText/CoreText.h>

#include <unordered_map>

/// A glyph ready to draw: the font that has it (the grid font or a fallback) and its index.
struct CachedGlyph {
  CTFontRef font = nullptr;
  CGGlyph glyph = 0;
};

@implementation GridView {
  /// The frame to draw.
  dio::Frame _frame;
  /// Regular and bold font, and the size of one cell in points.
  UIFont* _font;
  UIFont* _boldFont;
  CGSize _cell;
  /// Distance from the top of a cell to the text baseline.
  CGFloat _baseline;
  /// Where the grid starts: the safe area's top-left corner, snapped to the cell grid.
  CGPoint _origin;
  int _columns, _rows;
  /// Glyph lookups, keyed by code point and weight, so a frame costs no text layout.
  std::unordered_map<uint64_t, CachedGlyph> _glyphs;
  /// Keeps the fallback fonts that glyphs refer to alive.
  NSMutableArray* _fallbackFonts;
}

/// Font size of the grid. At the Apple TV's 1920 by 1080 points this gives about 130 columns
/// across the safe area, a quarter more than the 28 point size it started with (28 / 1.25).
static const CGFloat kFontSize = 22.4;

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.backgroundColor = UIColor.blackColor;
    self.opaque = YES;
    self.clearsContextBeforeDrawing = NO;
    _font = [UIFont monospacedSystemFontOfSize:kFontSize weight:UIFontWeightRegular];
    _boldFont = [UIFont monospacedSystemFontOfSize:kFontSize weight:UIFontWeightBold];
    const CGSize adv = [@"M" sizeWithAttributes:@{NSFontAttributeName : _font}];
    // Cells may be fractions of a point; backgrounds are snapped to whole points when drawn, so
    // neighbouring cells meet without anti-aliased seams.
    _cell = CGSizeMake(adv.width, _font.lineHeight);
    _baseline = _font.ascender;
    _fallbackFonts = [NSMutableArray array];
  }
  return self;
}

/// The view must be focusable: on tvOS the Siri Remote's touches and presses only reach a view
/// that has focus (or its ancestors), so without this the remote would do nothing.
- (BOOL)canBecomeFocused {
  return YES;
}

- (void)didUpdateFocusInContext:(UIFocusUpdateContext*)context withAnimationCoordinator:(UIFocusAnimationCoordinator*)coordinator {
  [super didUpdateFocusInContext:context withAnimationCoordinator:coordinator];
#ifndef NDEBUG
  NSLog(@"DIORASCII-INPUT focus changed: grid focused=%d", self.isFocused);
#endif
}

- (UIFocusEffect*)focusEffect {
  return nil;  // no highlight around the whole screen
}

- (void)layoutSubviews {
  [super layoutSubviews];
  const CGRect area = self.safeAreaLayoutGuide.layoutFrame;
  _columns = MAX(1, static_cast<int>(area.size.width / _cell.width));
  _rows = MAX(1, static_cast<int>(area.size.height / _cell.height));
  _origin = CGPointMake(round(area.origin.x + (area.size.width - _columns * _cell.width) / 2),
                        round(area.origin.y + (area.size.height - _rows * _cell.height) / 2));
}

- (int)columns { return _columns; }
- (int)rows { return _rows; }

- (void)showFrame:(const dio::Frame&)frame {
  _frame = frame;
  [self setNeedsDisplay];
}

/// Finds (and remembers) the glyph for a code point. Characters the grid font lacks come from a
/// system fallback font; characters nobody has stay empty.
- (CachedGlyph)glyphFor:(char32_t)ch bold:(bool)bold {
  const uint64_t key = (static_cast<uint64_t>(ch) << 1) | (bold ? 1 : 0);
  auto it = _glyphs.find(key);
  if (it != _glyphs.end()) return it->second;
  CachedGlyph result;
  CTFontRef base = (__bridge CTFontRef)(bold ? _boldFont : _font);
  UniChar units[2];
  CFIndex count = 1;
  if (ch < 0x10000) {
    units[0] = static_cast<UniChar>(ch);
  } else {
    units[0] = static_cast<UniChar>(0xD800 + ((ch - 0x10000) >> 10));
    units[1] = static_cast<UniChar>(0xDC00 + ((ch - 0x10000) & 0x3FF));
    count = 2;
  }
  CGGlyph glyphs[2] = {0, 0};
  if (CTFontGetGlyphsForCharacters(base, units, glyphs, count)) {
    result.font = base;
    result.glyph = glyphs[0];
  } else {
    NSString* text = [NSString stringWithCharacters:units length:static_cast<NSUInteger>(count)];
    CTFontRef fallback = CTFontCreateForString(base, (__bridge CFStringRef)text, CFRangeMake(0, count));
    if (fallback) {
      if (CTFontGetGlyphsForCharacters(fallback, units, glyphs, count)) {
        result.font = fallback;
        result.glyph = glyphs[0];
      }
      [_fallbackFonts addObject:(__bridge_transfer id)fallback];
    }
  }
  _glyphs[key] = result;
  return result;
}

- (void)drawRect:(CGRect)rect {
  CGContextRef ctx = UIGraphicsGetCurrentContext();
  const dio::Rgb defaultFg{200, 200, 200}, defaultBg{0, 0, 0};
  uint32_t lastColor = 0xFFFFFFFF;  // no color set yet
  auto setColor = [&](dio::Rgb c) {
    const uint32_t key = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
    if (key == lastColor) return;
    lastColor = key;
    CGContextSetRGBFillColor(ctx, c.r / 255.0, c.g / 255.0, c.b / 255.0, 1);
  };
  /// The colors a cell is drawn in, after defaults and reverse video.
  auto colorsOf = [&](const dio::Cell& c, dio::Rgb& fg, dio::Rgb& bg) {
    fg = c.defaultFg ? defaultFg : c.fg;
    bg = c.defaultBg ? defaultBg : c.bg;
    if (c.reverse) std::swap(fg, bg);
  };

  setColor(defaultBg);
  CGContextFillRect(ctx, rect);

  // Backgrounds: one rectangle per run of equal color.
  for (int y = 0; y < _frame.height(); ++y) {
    int x = 0;
    while (x < _frame.width()) {
      dio::Rgb fg, bg;
      colorsOf(_frame.at(x, y), fg, bg);
      int end = x + 1;
      while (end < _frame.width()) {
        dio::Rgb fg2, bg2;
        colorsOf(_frame.at(end, y), fg2, bg2);
        if (!(bg2 == bg)) break;
        ++end;
      }
      if (!(bg == defaultBg)) {
        setColor(bg);
        const CGFloat x0 = round(_origin.x + x * _cell.width), x1 = round(_origin.x + end * _cell.width);
        const CGFloat y0 = round(_origin.y + y * _cell.height), y1 = round(_origin.y + (y + 1) * _cell.height);
        CGContextFillRect(ctx, CGRectMake(x0, y0, x1 - x0, y1 - y0));
      }
      x = end;
    }
  }

  // Glyphs. UIKit's context has its origin at the top left, while Core Text draws glyphs for a
  // bottom-left origin: flip the context back and use negated y coordinates.
  CGContextSaveGState(ctx);
  CGContextScaleCTM(ctx, 1, -1);
  for (int y = 0; y < _frame.height(); ++y) {
    for (int x = 0; x < _frame.width(); ++x) {
      const dio::Cell& c = _frame.at(x, y);
      if (c.ch == U' ' || c.ch == 0) continue;
      const CachedGlyph g = [self glyphFor:c.ch bold:c.bold];
      if (!g.font || !g.glyph) continue;
      dio::Rgb fg, bg;
      colorsOf(c, fg, bg);
      setColor(fg);
      CGPoint position = CGPointMake(round(_origin.x + x * _cell.width), -round(_origin.y + y * _cell.height + _baseline));
      CGGlyph glyph = g.glyph;
      CTFontDrawGlyphs(g.font, &glyph, &position, 1, ctx);
    }
  }
  CGContextRestoreGState(ctx);
}

@end
