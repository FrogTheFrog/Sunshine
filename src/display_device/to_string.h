#pragma once

// local includes
#include "display_device.h"

namespace display_device {

  std::string
  to_string(device_state_e value);

  std::string
  to_string(hdr_state_e value);

  std::string
  to_string(const hdr_state_map_t &value);

  std::string
  to_string(const device_info_t &value);

  std::string
  to_string(const device_info_map_t &value);

  std::string
  to_string(const resolution_t &value);

  std::string
  to_string(const refresh_rate_t &value);

  std::string
  to_string(const display_mode_t &value);

  std::string
  to_string(const device_display_mode_map_t &value);

  std::string
  to_string(const active_topology_t &value);

}  // namespace display_device
