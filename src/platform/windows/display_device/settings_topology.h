#pragma once

// local includes
#include "settings_data.h"

namespace display_device {

  /*!
   * Here we have 2 topology data items:
   *   - temporary is meant to be used when we fail to change
   *     something and have to revert back to the previous
   *     topology and settings.
   *   - final is meant to be used when we have made the
   *     changes successfully and to be used when we do
   *     a final setting reversion. It will try to go back
   *     to the very first topology we had, before we applied
   *     the very first changes.
   */
  struct handled_topology_data_t {
    topology_data_t temporary_topology_data;
    topology_data_t final_topology_data;
    topology_metadata_t metadata;
  };

  std::unordered_set<std::string>
  get_device_ids_from_topology(const active_topology_t &topology);

  //! Returns device ids that are found in new topology, but were not present in the previous topology.
  std::unordered_set<std::string>
  get_newly_enabled_devices_from_topology(const active_topology_t &previous_topology, const active_topology_t &new_topology);

  /*!
   * Performs necessary steps for changing topology based on the config parameters.
   * Also makes sure to evaluate previous configuration in case we are just updating
   * some of the settings (like resolution) where topology change might not be necessary.
   *
   * In case the function determines that we need to revert all of the previous settings
   * since the new topology is not compatible with the previously configured one, the `revert_settings`
   * function will be called to completely revert all changes.
   *
   * On failure it returns empty optional, otherwise topology data is returned.
   */
  boost::optional<handled_topology_data_t>
  handle_device_topology_configuration(const parsed_config_t &config, boost::optional<topology_data_t> previously_configured_topology, const std::function<void()> &revert_settings);

}  // namespace display_device
