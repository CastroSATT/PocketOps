#include "tools/ToolRegistry.h"

#include <cstring>

namespace tools {

ToolRegistry& ToolRegistry::instance() {
  static ToolRegistry reg;
  return reg;
}

bool ToolRegistry::add(Tool* tool) {
  if (!tool || count_ >= kMaxTools) {
    return false;
  }
  tools_[count_++] = tool;
  return true;
}

Tool* ToolRegistry::at(int index) const {
  if (index < 0 || index >= count_) {
    return nullptr;
  }
  return tools_[index];
}

Tool* ToolRegistry::findById(const char* id) const {
  if (!id) {
    return nullptr;
  }
  for (int i = 0; i < count_; ++i) {
    if (std::strcmp(tools_[i]->id(), id) == 0) {
      return tools_[i];
    }
  }
  return nullptr;
}

}  // namespace tools
