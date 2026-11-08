#pragma once
// From positions to the pixels of a map view, and straight lines clipped to that view (MAP-6). A line of a long route
// can end thousands of pixels away at a low zoom, far beyond what the drawing library's 16-bit coordinates hold, so
// everything is clipped here in floating point first.
#include "../Geo.h"

class MapProjection {
public:
  MapProjection(int z, double left, double top, int width, int height)
    : _z(z), _left(left), _top(top), _w(width), _h(height) {}

  int zoom() const { return _z; }
  int width() const { return _w; }
  int height() const { return _h; }

  void toView(double lat, double lon, double& x, double& y) const {
    geo::toPixel(lat, lon, _z, x, y);
    x -= _left;
    y -= _top;
  }

  // Keeps the part of a segment inside the view (bounds included). False: nothing of it is visible.
  bool clip(double& x0, double& y0, double& x1, double& y1) const;

private:
  int code(double x, double y) const;
  int _z;
  double _left, _top;
  int _w, _h;
};
