#include "internal/serial_posix.hpp"
#include "cyberpower/protocol.hpp"

#include <cstring>
#include <string>

namespace cyberpower::platform {
namespace {

speed_t to_posix_baud(int baud) {
  switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    default: return 0;
  }
}

}  // namespace

bool configure_posix_termios(termios& tio, int baud, std::string& error) {
  // Put terminal in raw mode:
  // - Input: disable break handling, CR/NL translation, parity checking, stripping, flow control.
  // - Output: raw output (disable post-processing).
  // - Local: disable canonical mode, echo, signals, extended input processing.
  cfmakeraw(&tio);

  // 8 data bits
  tio.c_cflag &= ~CSIZE;
  tio.c_cflag |= CS8;

  // No parity
  tio.c_cflag &= ~PARENB;

  // 1 stop bit
  tio.c_cflag &= ~CSTOPB;

  // Ignore modem control lines, enable receiver
  tio.c_cflag |= (CLOCAL | CREAD);

#ifdef CRTSCTS
  // No hardware RTS/CTS flow control
  tio.c_cflag &= ~CRTSCTS;
#endif

  // No software XON/XOFF flow control
  tio.c_iflag &= ~(IXON | IXOFF | IXANY);

  // Non-blocking reads
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  const speed_t speed = to_posix_baud(baud);
  if (speed == 0) {
    error = "unsupported baud rate: " + std::to_string(baud);
    return false;
  }

  if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0) {
    error = "cfsetispeed/cfsetospeed failed";
    return false;
  }

  return true;
}

}  // namespace cyberpower::platform

