#pragma once
#include "../App.h"
#include "../Widgets.h"

class PacketDetailScreen : public Screen {
public:
  void show(const PacketRecord& r) { _r = r; }
  const char* title() const override { return tr("Packet", "Paquet"); }
  void draw(App& app, lgfx::LovyanGFX& g) override;
private:
  PacketRecord _r;
};

// Every packet heard or sent, newest first (RADIO-6). Moving the cursor or P freezes the list, so a line can be read
// while the radio keeps recording. L copies the packets to a CSV file on the SD card.
class PacketsScreen : public Screen {
public:
  explicit PacketsScreen(PacketDetailScreen& detail) : _detail(detail) {}
  const char* title() const override { return _title; }
  const char* help() const override { return tr("P:pause C:clear L:SD Enter:details", "P:pause C:vider L:SD Entrée:détail"); }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  void freeze(App& app);
  int offset(const PacketLog& log) const;   // packets newer than the frozen view
  int visible(const PacketLog& log) const;

  PacketDetailScreen& _detail;
  ListState _list;
  bool _paused = false;
  uint32_t _frozenTotal = 0;
  uint32_t _lastTotal = 0;
  char _title[32] = "";
};
