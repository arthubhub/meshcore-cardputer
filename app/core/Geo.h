#pragma once
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

// French 8-point compass label: N, NE, E, SE, S, SO, O, NO
inline const char* compassLabel(double deg) {
  static const char* const L[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
  int i = (int)((deg + 22.5) / 45.0) % 8;
  return L[i < 0 ? i + 8 : i];
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

}  // namespace geo
