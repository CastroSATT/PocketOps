#pragma once

#include "storage/TfExpose.h"
#include "tools/Tool.h"

namespace tools {

// TF Card hub: submenu for Expose / Check / Format (same 1↓ 2↑ hold✓ 3← UX).
class TfCardTool : public Tool {
 public:
  const char* id() const override { return "tf_card"; }
  const char* name() const override { return "TF Card"; }
  const char* menuLabel() const override;
  uint16_t menuFgColor(bool selected) const override;
  bool menuLedPulse(uint8_t& /*r*/, uint8_t& /*g*/, uint8_t& /*b*/) const override {
    return false;  // keep default red (= on); SD badge flashes on activity
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void onButton(input::Event event) override;
  bool onBack() override;
  void draw() override;

 private:
  enum class Screen : uint8_t {
    SubMenu,
    Expose,
    Check,
    FormatConfirm1,
    FormatConfirm2,
    FormatResult,
  };

  enum class SubItem : uint8_t {
    Expose = 0,
    Check,
    Format,
    Count,
  };

  // Confirm rows: 0 = Cancel (safe default), 1 = proceed
  static constexpr int kConfirmCancel = 0;
  static constexpr int kConfirmProceed = 1;
  static constexpr int kConfirmCount = 2;

  void selectSub();
  void beginFormatConfirm();
  void runFormat();
  void onConfirmButton(input::Event event, Screen next_if_proceed);
  void drawSubMenu();
  void drawExpose();
  void drawCheck();
  void drawFormatConfirm(int step);
  void drawFormatResult();
  void drawConfirmRows(const char* proceed_label);

  Screen screen_ = Screen::SubMenu;
  int sub_index_ = 0;
  int confirm_index_ = kConfirmCancel;
  storage::TfCardInfo info_{};
  bool format_ok_ = false;
};

}  // namespace tools
