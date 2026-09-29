#include <cyberpower/ups.h>

int main(void) {
  cp_device_list devices = cp_ups_list();
  cp_ups_list_free(&devices);
  return 0;
}
