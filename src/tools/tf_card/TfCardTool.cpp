#include "tools/tf_card/TfCardTool.h"

#include "display/Display.h"
#include "storage/TfExpose.h"

namespace tools {
namespace {

const char* kSubLabels[] = {"Expose", "Check card", "Format card"};

}  // namespace

const char* TfCardTool::menuLabel() const {
  return storage::TfExpose::instance().cardPresent() ? name() : "No card installed";
}

uint16_t TfCardTool::menuFgColor(bool selected) const {
  if (!storage::TfExpose::instance().cardPresent()) {
    return TFT_RED;
  }
  return selected ? TFT_WHITE : TFT_LIGHTGREY;
}

void TfCardTool::onEnter() {
  storage::TfExpose::instance().refreshCardPresent();
  screen_ = Screen::SubMenu;
  sub_index_ = 0;
  confirm_index_ = kConfirmCancel;
  draw();
}

void TfCardTool::onExit() {
  screen_ = Screen::SubMenu;
  confirm_index_ = kConfirmCancel;
}

void TfCardTool::loop() {}

bool TfCardTool::onBack() {
  if (screen_ == Screen::SubMenu) {
    return false;
  }
  if (screen_ == Screen::FormatConfirm2) {
    beginFormatConfirm();
    return true;
  }
  if (screen_ == Screen::FormatConfirm1 || screen_ == Screen::FormatResult) {
    screen_ = Screen::SubMenu;
    confirm_index_ = kConfirmCancel;
    draw();
    return true;
  }
  screen_ = Screen::SubMenu;
  draw();
  return true;
}

void TfCardTool::beginFormatConfirm() {
  // Leave Expose alone here. Format itself: if on → off → wait → wipe.
  screen_ = Screen::FormatConfirm1;
  confirm_index_ = kConfirmCancel;  // safe default — must move to Wipe
  draw();
}

void TfCardTool::selectSub() {
  switch (static_cast<SubItem>(sub_index_)) {
    case SubItem::Expose:
      screen_ = Screen::Expose;
      break;
    case SubItem::Check:
      screen_ = Screen::Check;
      storage::TfExpose::instance().checkCard(info_);
      break;
    case SubItem::Format:
      beginFormatConfirm();
      return;
    default:
      break;
  }
  draw();
}

void TfCardTool::runFormat() {
  auto showPct = [](int percent, const char* status) {
    if (percent < 0) {
      percent = 0;
    }
    if (percent > 100) {
      percent = 100;
    }
    auto& tft = display::tft();
    display::clear();
    display::statusBar("Format");

    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    char label[8];
    snprintf(label, sizeof(label), "%d%%", percent);
    tft.drawString(label, tft.width() / 2, 22);

    constexpr int pad = 12;
    const int bx = pad;
    const int by = 34;
    const int bw = tft.width() - pad * 2;
    const int bh = 10;
    tft.drawRect(bx, by, bw, bh, TFT_DARKGREY);
    const int fill = (bw - 2) * percent / 100;
    if (fill > 0) {
      tft.fillRect(bx + 1, by + 1, fill, bh - 2, TFT_GREEN);
    }

    if (status && status[0]) {
      const char* line = status;
      if (strncmp(status, "TF: ", 4) == 0) {
        line = status + 4;
      }
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.drawString(line, tft.width() / 2, 54);
    }
  };

  showPct(0, "starting");
  format_ok_ = storage::TfExpose::instance().formatPocketLayout(info_, showPct);
  screen_ = Screen::FormatResult;
  confirm_index_ = kConfirmCancel;
  draw();
}

void TfCardTool::onConfirmButton(input::Event event, Screen next_if_proceed) {
  if (event == input::Event::Single) {
    confirm_index_ = (confirm_index_ + 1) % kConfirmCount;
    draw();
    return;
  }
  if (event == input::Event::Double) {
    confirm_index_ = (confirm_index_ - 1 + kConfirmCount) % kConfirmCount;
    draw();
    return;
  }
  if (event != input::Event::Long) {
    return;
  }

  if (confirm_index_ == kConfirmCancel) {
    // Hold on Cancel = abort
    screen_ = Screen::SubMenu;
    confirm_index_ = kConfirmCancel;
    draw();
    return;
  }

  // Proceed selected
  if (next_if_proceed == Screen::FormatResult) {
    runFormat();
  } else {
    screen_ = next_if_proceed;
    confirm_index_ = kConfirmCancel;  // reset safe default on step 2
    draw();
  }
}

void TfCardTool::onButton(input::Event event) {
  auto& tf = storage::TfExpose::instance();

  if (screen_ == Screen::SubMenu) {
    const int n = static_cast<int>(SubItem::Count);
    if (event == input::Event::Single) {
      sub_index_ = (sub_index_ + 1) % n;
      draw();
    } else if (event == input::Event::Double) {
      sub_index_ = (sub_index_ - 1 + n) % n;
      draw();
    } else if (event == input::Event::Long) {
      selectSub();
    }
    return;
  }

  if (screen_ == Screen::Expose) {
    if (event == input::Event::Single || event == input::Event::Long) {
      tf.toggle();
      draw();
    }
    return;
  }

  if (screen_ == Screen::Check) {
    if (event == input::Event::Long) {
      tf.checkCard(info_);
      draw();
    }
    return;
  }

  if (screen_ == Screen::FormatConfirm1) {
    onConfirmButton(event, Screen::FormatConfirm2);
    return;
  }

  if (screen_ == Screen::FormatConfirm2) {
    onConfirmButton(event, Screen::FormatResult);
    return;
  }
}

void TfCardTool::draw() {
  switch (screen_) {
    case Screen::SubMenu:
      drawSubMenu();
      break;
    case Screen::Expose:
      drawExpose();
      break;
    case Screen::Check:
      drawCheck();
      break;
    case Screen::FormatConfirm1:
      drawFormatConfirm(1);
      break;
    case Screen::FormatConfirm2:
      drawFormatConfirm(2);
      break;
    case Screen::FormatResult:
      drawFormatResult();
      break;
  }
}

void TfCardTool::drawSubMenu() {
  auto& tft = display::tft();
  display::clear();
  display::statusBar("TF Card");

  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);

  constexpr int row_h = 14;
  constexpr int top = 18;
  const int n = static_cast<int>(SubItem::Count);

  for (int i = 0; i < n; ++i) {
    const int y = top + i * row_h;
    const bool sel = (i == sub_index_);
    const uint16_t bg = sel ? TFT_DARKGREEN : TFT_BLACK;
    const uint16_t fg = sel ? TFT_WHITE : TFT_LIGHTGREY;
    tft.fillRect(0, y, tft.width(), row_h, bg);
    tft.setTextColor(fg, bg);
    char line[36];
    snprintf(line, sizeof(line), "%c %s", sel ? '>' : ' ', kSubLabels[i]);
    tft.drawString(line, 4, y + row_h / 2);
  }

  tft.fillRect(0, tft.height() - 12, tft.width(), 12, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("1dn 2up holdOK 3bk", tft.width() / 2, tft.height() - 6);
}

void TfCardTool::drawExpose() {
  auto& tft = display::tft();
  auto& tf = storage::TfExpose::instance();

  display::clear();
  display::statusBar("Expose");

  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);

  if (tf.enabled()) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("EXPOSE ON", tft.width() / 2, 32);
    tft.setTextFont(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("PC sees TF drive", tft.width() / 2, 50);
  } else {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("EXPOSE OFF", tft.width() / 2, 32);
    tft.setTextFont(1);
    if (tf.lastError()[0]) {
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString(tf.lastError(), tft.width() / 2, 50);
    } else {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.drawString("1x = toggle", tft.width() / 2, 50);
    }
  }

  tft.setTextFont(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("3x = back", tft.width() / 2, tft.height() - 6);
}

void TfCardTool::drawCheck() {
  auto& tft = display::tft();
  display::clear();
  display::statusBar("Check");

  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);

  if (!info_.present) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("No card", tft.width() / 2, 36);
  } else {
    char line[40];
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "%s %uMB", info_.type_name, info_.size_mb);
    tft.drawString(line, tft.width() / 2, 22);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    snprintf(line, sizeof(line), "free %uMB", info_.free_mb);
    tft.drawString(line, tft.width() / 2, 36);

    tft.setTextColor(info_.dirs_ok ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    tft.drawString(info_.dirs_ok ? "folders OK" : "dirs missing", tft.width() / 2, 50);
  }

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("hold=recheck 3=bk", tft.width() / 2, tft.height() - 6);
}

void TfCardTool::drawConfirmRows(const char* proceed_label) {
  auto& tft = display::tft();
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);

  constexpr int row_h = 14;
  constexpr int top = 34;
  const char* labels[kConfirmCount] = {"Cancel", proceed_label};

  for (int i = 0; i < kConfirmCount; ++i) {
    const int y = top + i * row_h;
    const bool sel = (i == confirm_index_);
    const uint16_t bg = sel ? TFT_DARKGREEN : TFT_BLACK;
    uint16_t fg = sel ? TFT_WHITE : TFT_LIGHTGREY;
    if (i == kConfirmProceed && sel) {
      fg = TFT_ORANGE;
    }
    tft.fillRect(0, y, tft.width(), row_h, bg);
    tft.setTextColor(fg, bg);
    char line[36];
    snprintf(line, sizeof(line), "%c %s", sel ? '>' : ' ', labels[i]);
    tft.drawString(line, 4, y + row_h / 2);
  }
}

void TfCardTool::drawFormatConfirm(int step) {
  auto& tft = display::tft();
  display::clear();
  display::statusBar(step == 1 ? "Format 1/2" : "Format 2/2");

  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  if (step == 1) {
    tft.drawString("WIPE ALL DATA?", tft.width() / 2, 20);
  } else {
    tft.drawString("LAST CHANCE", tft.width() / 2, 20);
  }

  drawConfirmRows(step == 1 ? "Wipe…" : "ERASE");

  tft.fillRect(0, tft.height() - 12, tft.width(), 12, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("1dn 2up hold 3bk", tft.width() / 2, tft.height() - 6);
}

void TfCardTool::drawFormatResult() {
  auto& tft = display::tft();
  display::clear();
  display::statusBar("Format");

  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);

  if (!format_ok_) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Format failed", tft.width() / 2, 28);
    auto& tf = storage::TfExpose::instance();
    if (tf.lastError()[0]) {
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString(tf.lastError(), tft.width() / 2, 44);
    }
  } else {
    char line[40];
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Format OK", tft.width() / 2, 22);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "%s %uMB", info_.type_name, info_.size_mb);
    tft.drawString(line, tft.width() / 2, 36);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("name: POCKETOPS", tft.width() / 2, 48);
  }

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("3x = back", tft.width() / 2, tft.height() - 6);
}

}  // namespace tools
