#pragma once
#include "../App.h"
#include "../Widgets.h"

class ContactDetailScreen;

// Repeaters within direct radio range (RADIO-8): MeshCore's discovery request and the list of those that
// answered, with the SNR in both directions. A search starts when the screen opens; R searches again.
class NearbyScreen : public Screen {
public:
  explicit NearbyScreen(ContactDetailScreen& detail) : _detail(detail) {}
  const char* title() const override { return _title; }
  const char* help() const override { return tr("R:search Enter:details `:back", "R:chercher Entrée:détail `:retour"); }
  void onEnter(App& app) override;
  bool onKey(App& app, const KeyEvent& e) override;
  bool tick(App& app) override;
  void draw(App& app, lgfx::LovyanGFX& g) override;

private:
  static constexpr int ROWS = LIST_VISIBLE_ROWS - 1;   // the first row is the search status
  void search(App& app);
  void remember(App& app);
  void syncSelection(App& app);    // the selection follows the repeater when the list re-sorts

  ContactDetailScreen& _detail;
  ListState _list;
  uint8_t _sel[8] = {};
  int _lastCount = -1;
  uint32_t _lastSecs = 0;
  char _title[40] = "";
};
