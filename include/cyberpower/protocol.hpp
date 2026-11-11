#pragma once

// Recovered wire protocol from PowerPanel Personal's libppbedrvc.dylib
// (x86_64, PowerPanel_Native). Every constant is tagged:
//   High        - immediate, cstring xref, or straight-line disassembly
//   Medium      - structure is High, the English name is inferred
//   Low         - partial evidence
//   Speculative - plausible, not nailed down in this binary

// This header is an umbrella over the per-area headers below.

#include "cyberpower/errors.hpp"
#include "cyberpower/protocol_hid.hpp"
#include "cyberpower/protocol_text.hpp"
#include "cyberpower/protocol_v3.hpp"

#include <string>

namespace cyberpower::protocol {

// ---------------------------------------------------------------------------
// Event names recovered as cstrings. High as names. Delivery is an internal
// queue (IpcReceiveEvent, FeedbackConverseEvent, TimerEvent); this library
// does not recreate the JNI/IPC bus.
// ---------------------------------------------------------------------------

constexpr const char* kEventNames[] = {
    "IpcEventReceive",
    "FeedbackConverseEvent",
    "AppInitEvent",
    "SynchronizeEvent",
    "TrivialNumberedEvent",
    "StopQueueEvent",
    "TimerEvent",
    "ReceiveConverseEvent",
};

// Model name strings compiled into the driver. High that the driver compares
// against them. Not a complete catalog (the cstring table is much longer).
constexpr const char* kModelNames[] = {
    "CPS1000EI", "CPS2000EI", "CPS600E",   "CPS1000E",   "CPS1500PIE",
    "CPS3500PIE", "CPS5000PIE", "CPS7500PIE", "CPS1500PRO", "CPS3500PRO",
    "CPS5000PRO", "CPS7500PRO",
};

// Parser self-check used by the example. Returns false if a recovered rule
// regresses. Does not need hardware.
bool self_test(std::string* detail);

}  // namespace cyberpower::protocol
