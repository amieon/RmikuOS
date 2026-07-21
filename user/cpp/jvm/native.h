#pragma once
#include "types.h"

void register_natives(VM& vm);
Value call_native(VM& vm, const std::string& cls, const std::string& name,
                  const std::string& desc, std::vector<Value>& args);
