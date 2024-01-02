#pragma once

// local includes
#include "src/display_device/settings.h"

namespace display_device {

  /*!
   * Contains information from the latest topology change that was
   * taken care off. It is used for determining display modes, HDR states and etc.
   */
  struct topology_metadata_t {
    active_topology_t current_topology;
    std::unordered_set<std::string> newly_enabled_devices;
    bool primary_device_requested;
    std::vector<std::string> duplicated_devices;
  };

  /*!
   * Contains the initial topology that we had before we switched
   * to the topology that we have modified. They can be equal.
   * Initial topology info is needed so that we could go back to it
   * once we undo the changes in the modified topology.
   */
  struct topology_data_t {
    active_topology_t initial;
    active_topology_t modified;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(topology_data_t, initial, modified)
  };

  /*!
   * Data needed for reverting the changes we have made.
   * "Original" settings belong the the modified topology.
   */
  struct settings_t::data_t {
    topology_data_t topology;
    std::string original_primary_display;
    device_display_mode_map_t original_modes;
    hdr_state_map_t original_hdr_states;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(data_t, topology, original_primary_display, original_modes, original_hdr_states)
  };

}  // namespace display_device
