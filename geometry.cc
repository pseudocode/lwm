#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>

#include "geometry.h"

bool isLeftEdge(Edge e) {
  return e == ETopLeft || e == ELeft || e == EBottomLeft;
}

bool isRightEdge(Edge e) {
  return e == ETopRight || e == ERight || e == EBottomRight;
}

bool isTopEdge(Edge e) {
  return e == ETopLeft || e == ETop || e == ETopRight;
}

bool isBottomEdge(Edge e) {
  return e == EBottomLeft || e == EBottom || e == EBottomRight;
}

std::ostream& operator<<(std::ostream& os, const Point& p) {
  os << p.x << "," << p.y;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Area& a) {
  os << a.width << "x" << a.height;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Rect& r) {
  os << (r.xMax - r.xMin) << "x" << (r.yMax - r.yMin);
  if (r.xMin >= 0) {
    os << "+";
  }
  os << r.xMin;
  if (r.yMin >= 0) {
    os << "+";
  }
  os << r.yMin;
  return os;
}

std::ostream& operator<<(std::ostream& os, const std::vector<Rect>& rs) {
  os << "rects[";
  for (int i = 0; i < rs.size(); i++) {
    if (i) {
      os << " ";
    }
    os << rs[i];
  }
  os << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const XSizeHints& s) {
  os << "XSizeHints[";
#define D(x) (s.flags & x ? "" : "!") << #x << " "
  os << D(USPosition) << D(USSize) << D(PPosition) << D(PSize) << D(PMinSize)
     << D(PMaxSize) << D(PResizeInc) << D(PAspect) << D(PBaseSize)
     << D(PWinGravity) << "pos:" << s.width << "x" << s.height << "+" << s.x
     << "+" << s.y << " size: min=" << s.min_width << "," << s.min_height
     << "; max=" << s.max_width << "," << s.max_height
     << " aspect: min=" << s.min_aspect.x << ":" << s.min_aspect.y
     << "; max=" << s.max_aspect.x << ":" << s.max_aspect.y
     << " base=" << s.base_width << "," << s.base_height
     << " gravity=" << s.win_gravity << "]";
#undef D
  return os;
}

// static
Point Point::Sub(Point a, Point b) {
  return Point{a.x - b.x, a.y - b.y};
}

// static
Rect Rect::FromXYWH(int x, int y, int w, int h) {
  return Rect{x, y, x + w, y + h};
}

// static
Rect Rect::Intersect(const Rect& a, const Rect& b) {
  Rect res = Rect{std::max(a.xMin, b.xMin), std::max(a.yMin, b.yMin),
                  std::min(a.xMax, b.xMax), std::min(a.yMax, b.yMax)};
  if (res.xMax > res.xMin && res.yMax > res.yMin) {
    return res;
  }
  // No intersection.
  res = Rect{0, 0, 0, 0};
  return res;
}

// static
Rect Rect::Translate(Rect r, Point p) {
  return Rect{r.xMin + p.x, r.yMin + p.y, r.xMax + p.x, r.yMax + p.y};
}

int parseInt(const std::string& s) {
  const char* cs = s.c_str();
  if (cs[0] == '+') {
    cs++;
  }
  return atoi(cs);
}

// static
Rect Rect::Parse(std::string v) {
  Rect res = {};
  int sep = v.find('x');
  if (sep == std::string::npos) {
    return res;
  }
  const int w = parseInt(v.substr(0, sep));
  v = v.substr(sep + 1);
  sep = v.find_first_of("+-");
  if (sep == std::string::npos) {
    return res;
  }
  const int h = parseInt(v.substr(0, sep));
  v = v.substr(sep);
  sep = v.find_last_of("+-");
  if (sep == std::string::npos || sep == 0) {
    return res;
  }
  const int x = parseInt(v.substr(0, sep));
  const int y = parseInt(v.substr(sep));
  if (h != 0 && w != 0) {
    res.xMin = x;
    res.yMin = y;
    res.xMax = x + w;
    res.yMax = y + h;
  }
  return res;
}

// Any window outside these size ranges are just plain silly.
// Revisit MAX_MAX if we ever get to such a silly situation that we need more
// than 10 thousand pixels on a side.
#define MIN_MIN 10
#define MAX_MAX 10000

DimensionLimiter::DimensionLimiter(int min, int max, int base, int increment)
    : min_(min), max_(max), base_(base), increment_(increment) {
  // Fix any dodgy numbers.
  if (min_ < MIN_MIN) {
    min_ = MIN_MIN;
  }
  if (max_ < min_ || max_ > MAX_MAX) {
    max_ = MAX_MAX;
  }
  if (increment_ < 1) {
    increment_ = 1;
  }
}

void DimensionLimiter::Limit(int old_min,
                             int old_max,
                             int& new_min,
                             int& new_max) const {
  int size = new_max - new_min;
  // In all the cases further down, size_delta must be set to the value to *add*
  // to the size in order to make it conform to the rules.
  int size_delta = 0;
  if (size < min_) {
    size_delta = min_ - size;
  } else if (size > max_) {
    size_delta = max_ - size;
  } else {
    size_delta = -((size - base_) % increment_);
  }
  if (size_delta == 0) {
    return;
  }
  // Need to fix the size. To do that, we must pick which of min and max have
  // changed, and adjust it accordingly. If neither has change (unlikely) it
  // doesn't matter which we pick.
  // Note that the size_delta is a delta in *size*, so it should be added to
  // new_max, *or* subtracted from new_min.
  if (new_min != old_min) {
    new_min -= size_delta;
  } else {
    new_max += size_delta;
  }
  // It makes sense to pass in old_max from an API point of view, but we don't
  // really need it, because we only check if old_min has changed.
  old_max = old_max;
}

int DimensionLimiter::DisplayableSize(int v) const {
  return (v - base_) / increment_;
}
