#pragma once

// Text and JSON rendering for the cpups command-line tool.

#include "cyberpower/ups.hpp"

#include <ostream>
#include <string>

const char* transport_name(cyberpower::TransportKind kind);
std::string json_escape(const std::string& text);

// Human-readable multi-line status block.
void print_status_text(const cyberpower::Status& status);

// JSON object for a status sample (no trailing newline).
void print_status_json_object(std::ostream& out, const cyberpower::Status& status);

// JSON object for one device: either its status, an open error, or just info.
void print_device_json(std::ostream& out, const cyberpower::DeviceInfo& device,
                       const cyberpower::Status* status, const std::string* open_error);

// One-line summary used by --monitor.
std::string format_monitor_line(const cyberpower::Status& status);
