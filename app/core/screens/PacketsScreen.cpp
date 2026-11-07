#include "PacketsScreen.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {

void formatTime(App& app, const PacketRecord& r, char* out, size_t n) {
  if (clockValid(r.unixTime)) {
    uint32_t t = r.unixTime + (int32_t)app.services().settings->get().utcOffsetMinutes * 60;
    snprintf(out, n, "%02u:%02u:%02u", (unsigned)(t / 3600 % 24), (unsigned)(t / 60 % 60), (unsigned)(t % 60));
  } else {
    snprintf(out, n, "+%us", (unsigned)(r.ms / 1000));
  }
}

void hex(const uint8_t* p, int n, char* out, size_t cap) {
  size_t o = 0;
  out[0] = 0;
  for (int i = 0; i < n && o + 2 < cap; i++) o += snprintf(out + o, cap - o, "%02x", p[i]);
}

}  // namespace

void PacketsScreen::onEnter(App&) {
  _list = ListState();
  _paused = false;
}

void PacketsScreen::freeze(App& app) {
  if (_paused) return;
  _paused = true;
  _frozenTotal = app.services().mesh->packetLog().total();
}

int PacketsScreen::offset(const PacketLog& log) const {
  if (!_paused || log.total() < _frozenTotal) return 0;
  return (int)(log.total() - _frozenTotal);
}

int PacketsScreen::visible(const PacketLog& log) const {
  int n = log.count() - offset(log);
  return n > 0 ? n : 0;
}

bool PacketsScreen::onKey(App& app, const KeyEvent& e) {
  MeshService& mesh = *app.services().mesh;
  const PacketLog& log = mesh.packetLog();
  int n = visible(log);
  if (e.code == KeyCode::Up || e.code == KeyCode::Down) {
    freeze(app);
    _list.move(e.code == KeyCode::Up ? -1 : 1, n, LIST_VISIBLE_ROWS);
    return true;
  }
  if (e.is('p')) {
    if (_paused) { _paused = false; _list = ListState(); }
    else freeze(app);
    return true;
  }
  if (e.is('c')) {
    mesh.clearPacketLog();
    _paused = false;
    _list = ListState();
    return true;
  }
  if (e.is('l')) {
    bool on = !mesh.packetCapture();
    BackupService* sd = app.services().backup;
    if (on && !(sd && sd->available())) {
      app.notify(tr("No SD card", "Pas de carte SD"), tr("Insert a microSD card (FAT32)", "Insérer une carte microSD (FAT32)"), 2500);
    } else if (!mesh.setPacketCapture(on)) {
      app.notify(tr("Failed", "Échec"), tr("Cannot write to the SD card", "Écriture impossible sur la SD"), 2500);
    } else {
      app.notify(on ? tr("Writing to the SD card", "Écriture sur la carte SD") : tr("SD capture stopped", "Capture SD arrêtée"),
                 "cardputer_packets.csv", 2000);
    }
    return true;
  }
  if (e.code == KeyCode::Enter && n > 0) {
    if (const PacketRecord* r = log.recent(offset(log) + _list.sel)) {
      _detail.show(*r);
      app.push(&_detail);
    }
    return true;
  }
  return false;
}

bool PacketsScreen::tick(App& app) {
  uint32_t total = app.services().mesh->packetLog().total();
  bool changed = total != _lastTotal;
  _lastTotal = total;
  return changed;
}

void PacketsScreen::draw(App& app, lgfx::LovyanGFX& g) {
  MeshService& mesh = *app.services().mesh;
  const PacketLog& log = mesh.packetLog();
  int n = visible(log);
  snprintf(_title, sizeof(_title), "%s %u%s%s", tr("Packets", "Paquets"), (unsigned)log.total(),
           _paused ? tr(" paused", " en pause") : "", mesh.packetCapture() ? " SD" : "");
  if (n == 0) {
    g.setFont(&fontText);
    g.setTextColor(DIM);
    g.setTextDatum(lgfx::middle_center);
    g.drawString(tr("Nothing heard yet", "Rien d'entendu pour l'instant"), SCREEN_W / 2, CONTENT_Y + CONTENT_H / 2);
    g.setTextDatum(lgfx::top_left);
    return;
  }
  _list.clamp(n, LIST_VISIBLE_ROWS);
  int skip = offset(log);
  for (int i = 0; i < LIST_VISIBLE_ROWS && _list.top + i < n; i++) {
    int k = _list.top + i;
    const PacketRecord* r = log.recent(skip + k);
    if (!r) break;
    char when[12], from[8], left[40], right[24];
    formatTime(app, *r, when, sizeof(when));
    hex(r->from, r->fromLen, from, sizeof(from));
    snprintf(left, sizeof(left), "%s %s%s %s", when, r->tx() ? ">" : "", PacketLog::typeName(r->type), from);
    char hops[8];
    if (r->flood()) snprintf(hops, sizeof(hops), "%u", r->hops);
    else hops[0] = 0;
    const char* region = !r->scoped() ? "" : r->ourRegion() ? "=" : "~";
    if (r->tx()) snprintf(right, sizeof(right), "%s%s%s", PacketLog::routeName(r->route), hops, region);
    else snprintf(right, sizeof(right), "%s%s%s %+.1f", PacketLog::routeName(r->route), hops, region, r->snrX4 / 4.0f);
    drawListRow(g, CONTENT_Y + 1 + i * LINE_H, left, right, k == _list.sel, r->tx() ? ACCENT : DIM, r->tx());
  }
  drawScrollbar(g, _list.top, LIST_VISIBLE_ROWS, n);
}

// ---------------------------------------------------------------- PacketDetailScreen

void PacketDetailScreen::draw(App& app, lgfx::LovyanGFX& g) {
  char v[8][32];
  const char* labels[8];
  int n = 0;
  const auto row = [&](const char* label) -> char* {
    labels[n] = label;
    return v[n++];
  };
  char when[12];
  formatTime(app, _r, when, sizeof(when));
  snprintf(row(_r.tx() ? tr("Sent", "Envoyé") : tr("Heard", "Entendu")), 32, "%s", when);
  snprintf(row("Type"), 32, tr("%s, %u bytes", "%s, %u octets"), PacketLog::typeName(_r.type), _r.len);
  const char* route = _r.route == wire::ROUTE_DIRECT ? "direct" : _r.route == wire::ROUTE_FLOOD ? "flood"
                    : _r.route == wire::ROUTE_TRANSPORT_FLOOD ? tr("flood with region", "flood avec région")
                                                              : tr("direct with region", "direct avec région");
  snprintf(row("Route"), 32, "%s", route);
  snprintf(row(tr("Hops", "Sauts")), 32, tr("%u, ID on %u byte(s)", "%u, ID sur %u octet(s)"), _r.hops, _r.hashBytes);
  char hexa[12];
  if (_r.source == PacketRecord::SRC_NONE) {
    snprintf(row(tr("From", "De")), 32, "-");
  } else {
    hex(_r.from, _r.fromLen, hexa, sizeof(hexa));
    if (_r.source == PacketRecord::SRC_CHANNEL) snprintf(row(tr("Channel ID", "ID canal")), 32, "%s", hexa);
    else if (_r.flags & PacketRecord::HAS_TO) snprintf(row(tr("From > to", "De > à")), 32, "%s > %02x", hexa, _r.to);
    else snprintf(row(tr("From", "De")), 32, "%s", hexa);
  }
  if (_r.hops) {
    hex(_r.lastHop, _r.hashBytes, hexa, sizeof(hexa));
    snprintf(row(tr("Heard via", "Entendu via")), 32, "%s", hexa);
  } else {
    snprintf(row(tr("Heard via", "Entendu via")), 32, "%s", _r.tx() ? "-" : tr("the sender", "l'émetteur"));
  }
  if (_r.tx()) snprintf(row(tr("Signal", "Signal")), 32, "-");
  else snprintf(row(tr("Signal", "Signal")), 32, "%.1f dB  %d dBm", _r.snrX4 / 4.0f, _r.rssi);
  if (!_r.scoped()) snprintf(row(tr("Region", "Région")), 32, "%s", tr("none", "aucune"));
  else if (_r.ourRegion()) snprintf(row(tr("Region", "Région")), 32, "%s", tr("yours", "la tienne"));
  else snprintf(row(tr("Region", "Région")), 32, tr("other (%04x)", "autre (%04x)"), _r.transportCode);
  for (int i = 0; i < n; i++) drawListRow(g, CONTENT_Y + 1 + i * LINE_H, labels[i], v[i], false, FG, true);
}
