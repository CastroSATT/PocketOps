#pragma once

#include "tools/ToolRegistry.h"

namespace ui {

class Menu {
 public:
  void begin();
  void setIndex(int index);
  int index() const { return index_; }

  void moveDown();
  void moveUp();

  tools::Tool* selected() const;
  void draw() const;

 private:
  int index_ = 0;
};

}  // namespace ui
