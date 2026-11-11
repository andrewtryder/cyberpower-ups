#pragma once

#include <termios.h>
#include <string>

namespace cyberpower::platform {

// Configures a POSIX termios structure for UPS serial communication:
// raw mode, 8 data bits, no parity, 1 stop bit, CLOCAL | CREAD, nonblocking reads,
// and configured baud rate (default 2400).
bool configure_posix_termios(termios& tio, int baud, std::string& error);

}  // namespace cyberpower::platform

