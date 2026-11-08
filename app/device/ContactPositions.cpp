#if !CPADV_DEMO   // radio only: the demo firmware has no MeshCore contacts
#include "ContactPositions.h"
#include "../core/drivers/GeoText.h"
#include <helpers/AdvertDataHelpers.h>
#include <math.h>

PositionBook::Meta ContactPositions::meta(const ContactInfo& c) const {
  PositionBook::Meta m = _book.get(c.id.pub_key);
  if (m.source == PositionSource::None && (c.gps_lat || c.gps_lon)) {   // older files: the position of an advert
    m.source = PositionSource::Advert;
    m.time = c.last_advert_timestamp;
  }
  return m;
}

bool ContactPositions::offer(ContactInfo& c, int32_t latE6, int32_t lonE6, uint32_t when, PositionSource source) {
  const PositionBook::Meta m = _book.get(c.id.pub_key);
  ContactPosition pos;
  pos.latE6 = c.gps_lat;
  pos.lonE6 = c.gps_lon;
  pos.time = m.time;
  pos.source = m.source;
  if (!pos.offer(latE6, lonE6, when, source)) return false;
  c.gps_lat = pos.latE6;
  c.gps_lon = pos.lonE6;
  PositionBook::Meta updated;
  updated.time = pos.time;
  updated.source = pos.source;
  _book.set(c.id.pub_key, updated);   // full: the position is kept, only its time is lost
  return true;
}

bool ContactPositions::fromMessage(ContactInfo& c, const char* text, uint32_t senderTime, bool& found) {
  double lat, lon;
  found = geotext::find(text, lat, lon);
  return found && offer(c, (int32_t)lround(lat * 1e6), (int32_t)lround(lon * 1e6), senderTime, PositionSource::Message);
}

ContactPositions::Before ContactPositions::before(const ContactInfo* existing) {
  Before b;
  if (existing) {
    b.known = true;
    b.latE6 = existing->gps_lat;
    b.lonE6 = existing->gps_lon;
  }
  return b;
}

bool ContactPositions::afterAdvert(const Before& before, ContactInfo* c, uint32_t timestamp, const uint8_t* appData,
                                   size_t appDataLen) {
  AdvertDataParser advert(appData, appDataLen);
  // Not taken by MeshCore (replayed, or not added), or no position in it: nothing to decide
  if (!c || c->last_advert_timestamp != timestamp || !advert.isValid() || !advert.hasLatLon()) return false;
  const int32_t lat = c->gps_lat, lon = c->gps_lon;
  if (before.known) {
    c->gps_lat = before.latE6;
    c->gps_lon = before.lonE6;
  }
  return offer(*c, lat, lon, timestamp, PositionSource::Advert);
}
#endif  // !CPADV_DEMO
