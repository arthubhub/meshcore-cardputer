#pragma once
#include "I18n.h"
// Great-circle distance and initial bearing (WGS84 sphere approximation, < 0.5 % error).
#include <math.h>

namespace geo {

constexpr double EARTH_RADIUS_M = 6371000.0;
constexpr double DEG = 3.14159265358979323846 / 180.0;

inline double distanceM(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * DEG, dLon = (lon2 - lon1) * DEG;
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG) * cos(lat2 * DEG) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * EARTH_RADIUS_M * atan2(sqrt(a), sqrt(1 - a));
}

// Degrees clockwise from north, 0..360.
inline double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double y = sin((lon2 - lon1) * DEG) * cos(lat2 * DEG);
  double x = cos(lat1 * DEG) * sin(lat2 * DEG) - sin(lat1 * DEG) * cos(lat2 * DEG) * cos((lon2 - lon1) * DEG);
  double b = atan2(y, x) / DEG;
  return b < 0 ? b + 360 : b;
}

// 8-point compass label in the UI language: N, NE, E, SE, S, SW, W, NW (French: SO, O, NO)
inline const char* compassLabel(double deg) {
  static const char* const EN[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  static const char* const FR[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
  int i = (int)((deg + 22.5) / 45.0) % 8;
  if (i < 0) i += 8;
  return i18n::french() ? FR[i] : EN[i];
}

}  // namespace geo

// ---- Web Mercator (slippy map tiles, 256 px) ----
namespace geo {

constexpr int TILE_SIZE = 256;

// Global pixel coordinates of a position at zoom z (0..2^z*256).
inline void toPixel(double lat, double lon, int z, double& px, double& py) {
  if (lat > 85.0511) lat = 85.0511;
  if (lat < -85.0511) lat = -85.0511;
  double n = (double)(1u << z) * TILE_SIZE;
  px = (lon + 180.0) / 360.0 * n;
  double s = sin(lat * DEG);
  py = (0.5 - log((1 + s) / (1 - s)) / (4 * 3.14159265358979323846)) * n;
}

inline void fromPixel(double px, double py, int z, double& lat, double& lon) {
  double n = (double)(1u << z) * TILE_SIZE;
  lon = px / n * 360.0 - 180.0;
  double y = 3.14159265358979323846 * (1 - 2 * py / n);
  lat = atan(sinh(y)) / DEG;
}

// Ground resolution in metres per pixel at a latitude.
inline double metersPerPixel(double lat, int z) {
  return 156543.03392 * cos(lat * DEG) / (double)(1u << z);
}

// The rectangle around a set of positions, and the closest zoom that shows it whole. A set crossing the 180th
// meridian is seen as spanning the whole world: no mesh does.
class Bounds {
public:
  void add(double lat, double lon) {
    if (!_any) { _south = _north = lat; _west = _east = lon; _any = true; return; }
    if (lat < _south) _south = lat;
    if (lat > _north) _north = lat;
    if (lon < _west) _west = lon;
    if (lon > _east) _east = lon;
  }
  void add(const Bounds& o) {
    if (!o._any) return;
    add(o._south, o._west);
    add(o._north, o._east);
  }
  bool empty() const { return !_any; }

  // Centre of the rectangle on the map, which is not the middle of the latitudes (Mercator).
  void center(double& lat, double& lon) const {
    double x0, y0, x1, y1;
    toPixel(_north, _west, 0, x0, y0);
    toPixel(_south, _east, 0, x1, y1);
    fromPixel((x0 + x1) / 2, (y0 + y1) / 2, 0, lat, lon);
  }

  // Highest zoom in [zmin, zmax] where the rectangle fits in w x h pixels, leaving `margin` pixels on each side.
  int fitZoom(int w, int h, int margin, int zmin, int zmax) const {
    for (int z = zmax; z > zmin; z--) {
      double x0, y0, x1, y1;
      toPixel(_north, _west, z, x0, y0);
      toPixel(_south, _east, z, x1, y1);
      if (x1 - x0 <= w - 2 * margin && y1 - y0 <= h - 2 * margin) return z;
    }
    return zmin;
  }

private:
  bool _any = false;
  double _south = 0, _north = 0, _west = 0, _east = 0;
};

}  // namespace geo
