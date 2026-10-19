#include "ui/Menu.h"

#include "display/Display.h"
#include "storage/ConfigStore.h"

namespace ui {

void Menu::begin() {
  index_ = 0;
  draw();
}

void Menu::setIndex(int index) {
  const int n = tools::ToolRegistry::instance().count();
  if (n <= 0) {
    index_ = 0;
    return;
  }
  if (index < 0) {
    index = n - 1;
  } else if (index >= n) {
    index = 0;
  }
  index_ = index;
}

void Menu::moveDown() {
  setIndex(index_ + 1);
  draw();
}

void Menu::moveUp() {
  setIndex(index_ - 1);
  draw();
}

tools::Tool* Menu::selected() const {
  return tools::ToolRegistry::instance().at(index_);
}

void Menu::draw() const {
  auto& tft = display::tft();
  auto& reg = tools::ToolRegistry::instance();

  display::clear();
  display::statusBar(storage::ConfigStore::instance().productName());

  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);

  const int n = reg.count();
  if (n == 0) {
    display::centeredText("No tools", nullptr, TFT_YELLOW);
    return;
  }

  // Show up to 4 rows under the status bar
  constexpr int row_h = 14;
  constexpr int top = 18;
  constexpr int visible = 4;

  int start = index_ - (visible / 2);
  if (start < 0) {
    start = 0;
  }
  if (start > n - visible && n > visible) {
    start = n - visible;
  }

  for (int row = 0; row < visible; ++row) {
    const int i = start + row;
    if (i >= n) {
      break;
    }
    const int y = top + row * row_h;
    const bool sel = (i == index_);
    tools::Tool* tool = reg.at(i);
    const uint16_t bg = sel ? TFT_DARKGREEN : TFT_BLACK;
    const uint16_t fg = tool->menuFgColor(sel);

    tft.fillRect(0, y, tft.width(), row_h, bg);
    tft.setTextColor(fg, bg);
    char line[40];
    snprintf(line, sizeof(line), "%c %s", sel ? '>' : ' ', tool->menuLabel());
    tft.drawString(line, 4, y + row_h / 2);
  }

  // Hint footer
  tft.fillRect(0, tft.height() - 12, tft.width(), 12, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("1dn 2up holdOK 3bk", tft.width() / 2, tft.height() - 6);
}

}  // namespace ui
