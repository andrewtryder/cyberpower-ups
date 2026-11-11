#pragma once

// Helpers shared by the C API translation units.

#include "cyberpower/ups.h"
#include "cyberpower/ups.hpp"

#include <string>

namespace cyberpower::capi {

// malloc-backed copy of `text` (freed by the matching cp_*_free). May return nullptr.
char* dup_cstr(const std::string& text);

// Fills `out` from `status`; the caller releases it with cp_ups_status_free.
void fill_status(cp_status* out, const cyberpower::Status& status);

struct Session {
  cyberpower::Ups ups;
  explicit Session(cyberpower::Ups opened) : ups(std::move(opened)) {}
};

inline Session* as_session(cp_ups* ups) { return reinterpret_cast<Session*>(ups); }

}  // namespace cyberpower::capi
