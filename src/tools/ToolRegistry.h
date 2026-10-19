#pragma once

#include "tools/Tool.h"

namespace tools {

constexpr int kMaxTools = 16;

class ToolRegistry {
 public:
  static ToolRegistry& instance();

  bool add(Tool* tool);
  int count() const { return count_; }
  Tool* at(int index) const;
  Tool* findById(const char* id) const;

 private:
  ToolRegistry() = default;
  Tool* tools_[kMaxTools] = {};
  int count_ = 0;
};

}  // namespace tools
