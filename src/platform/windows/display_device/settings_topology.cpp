// local includes
#include "settings_topology.h"
#include "src/display_device/to_string.h"
#include "src/main.h"

namespace display_device {

  namespace {
    /*!
     * Verifies that the specified (or a primary) device is available
     * and returns one id, even if it belong to a duplicate display.
     */
    std::string
    find_one_of_the_available_devices(const std::string &device_id) {
      const auto devices { enum_available_devices() };
      if (devices.empty()) {
        BOOST_LOG(error) << "display device list is empty!";
        return {};
      }
      BOOST_LOG(info) << "available display devices: " << to_string(devices);

      const auto device_it { std::find_if(std::begin(devices), std::end(devices), [&device_id](const auto &entry) {
        return device_id.empty() ? entry.second.device_state == device_state_e::primary : entry.first == device_id;
      }) };
      if (device_it == std::end(devices)) {
        BOOST_LOG(error) << "device " << (device_id.empty() ? "PRIMARY" : device_id) << " not found in the list of available devices!";
        return {};
      }

      return device_it->first;
    }

    boost::optional<active_topology_t>
    get_and_validate_current_topology() {
      auto initial_topology { get_current_topology() };
      if (!is_topology_valid(initial_topology)) {
        BOOST_LOG(error) << "display topology is invalid!";
        return boost::none;
      }
      BOOST_LOG(debug) << "current display topology: " << to_string(initial_topology);
      return initial_topology;
    }

    /*!
     * Finds duplicate devices for the device_id in the provided topology and appends them to the output list.
     * The device_id itself is put to the front.
     *
     * It is possible that the device is inactive and is not in the current topology.
     */
    std::vector<std::string>
    get_duplicate_devices(const std::string &device_id, const active_topology_t &current_topology) {
      std::vector<std::string> duplicated_devices;

      duplicated_devices.clear();
      duplicated_devices.push_back(device_id);

      for (const auto &group : current_topology) {
        for (const auto &group_device_id : group) {
          if (device_id == group_device_id) {
            std::copy_if(std::begin(group), std::end(group), std::back_inserter(duplicated_devices), [&](const auto &id) {
              return id != device_id;
            });
            break;
          }
        }
      }

      return duplicated_devices;
    }

    // It is possible that the user has changed the topology while the stream was
    // paused or something, so the current topology is no longer what it was when
    // we last knew about it.
    //
    // This is fine, however if we are updating the existing setting we want to
    // preserve the "initial" or the first topology when we started to change the
    // settings. So, imagine we have 2 users, one did not change anything, the other did:
    //
    // Good user:
    //   Previous configuration:
    //       [[DISPLAY1]] -> [[DISPLAY2]]
    //   Current configuration:
    //       [[DISPLAY2]] -> [[DISPLAY2]]
    //   Conclusion:
    //       User did not change the topology manually since in the current configuration
    //       we are switching to the same topology, but maybe the user stopped stream
    //       to change resolution or something, so we should go back to DISPLAY1
    //
    // Bad user:
    //   Previous configuration:
    //       [[DISPLAY1]] -> [[DISPLAY2]]
    //   Current configuration:
    //       [[DISPLAY4]] -> [[DISPLAY2]]
    //   Conclusion:
    //       User did change the topology manually to DISPLAY4 at some point, we should not
    //       go back to DISPLAY1, but to DISPLAY4 instead.
    active_topology_t
    determine_initial_topology_based_on_prev_config(const boost::optional<topology_data_t> &previously_configured_topology, const active_topology_t &current_topology) {
      if (previously_configured_topology) {
        if (is_topology_the_same(previously_configured_topology->modified, current_topology)) {
          return previously_configured_topology->initial;
        }
      }

      return current_topology;
    }

    bool
    is_device_found_in_active_topology(const std::string &device_id, const active_topology_t &current_topology) {
      for (const auto &group : current_topology) {
        for (const auto &group_device_id : group) {
          if (device_id == group_device_id) {
            return true;
          }
        }
      }

      return false;
    }

    /*!
     * Using all of the currently available data we try to determine
     * what should the final topology be like. Multiple factors need to be taken into account, follow the
     * comments in the code to understand them better.
     */
    active_topology_t
    determine_final_topology(const parsed_config_t::device_prep_e device_prep, const bool primary_device_requested, const std::vector<std::string> &duplicated_devices, const active_topology_t &current_topology) {
      boost::optional<active_topology_t> final_topology;

      const bool topology_change_requested { device_prep != parsed_config_t::device_prep_e::no_operation };
      if (topology_change_requested) {
        if (device_prep == parsed_config_t::device_prep_e::ensure_only_display) {
          // Device needs to be the only one that's active or if it's a PRIMARY device,
          // only the whole PRIMARY group needs to be active (in case they are duplicated)

          if (primary_device_requested) {
            if (current_topology.size() > 1) {
              // There are other topology groups other than the primary devices,
              // so we need to change that
              final_topology = active_topology_t { { duplicated_devices } };
            }
            else {
              // Primary device group is the only one active, nothing to do
            }
          }
          else {
            // Since primary_device_requested == false, it means a device was specified via config by the user
            // and is the only device that needs to be enabled

            if (is_device_found_in_active_topology(duplicated_devices.front(), current_topology)) {
              // Device is currently active in the active topology group

              if (duplicated_devices.size() > 1 || current_topology.size() > 1) {
                // We have more than 1 device in the group or we have more than 1 topology groups.
                // We need to disable all other devices
                final_topology = active_topology_t { { duplicated_devices.front() } };
              }
              else {
                // Our device is the only one that's active, nothing to do
              }
            }
            else {
              // Our device is not active, we need to activate it and ONLY it
              final_topology = active_topology_t { { duplicated_devices.front() } };
            }
          }
        }
        else {
          // The device needs to be active at least.

          if (primary_device_requested || is_device_found_in_active_topology(duplicated_devices.front(), current_topology)) {
            // Device is already active, nothing to do here
          }
          else {
            // Create the extended topology as it's probably what makes sense the most...
            final_topology = current_topology;
            final_topology->push_back({ duplicated_devices.front() });
          }
        }
      }

      return final_topology ? *final_topology : current_topology;
    }

  }  // namespace

  std::unordered_set<std::string>
  get_device_ids_from_topology(const active_topology_t &topology) {
    std::unordered_set<std::string> device_ids;
    for (const auto &group : topology) {
      for (const auto &device_id : group) {
        device_ids.insert(device_id);
      }
    }

    return device_ids;
  }

  std::unordered_set<std::string>
  get_newly_enabled_devices_from_topology(const active_topology_t &previous_topology, const active_topology_t &new_topology) {
    const auto prev_ids { get_device_ids_from_topology(previous_topology) };
    auto new_ids { get_device_ids_from_topology(new_topology) };

    for (auto &id : prev_ids) {
      new_ids.erase(id);
    }

    return new_ids;
  }

  boost::optional<handled_topology_data_t>
  handle_device_topology_configuration(const parsed_config_t &config, boost::optional<topology_data_t> previously_configured_topology, const std::function<void()> &revert_settings) {
    const bool primary_device_requested { config.device_id.empty() };
    const std::string requested_device_id { find_one_of_the_available_devices(config.device_id) };
    if (requested_device_id.empty()) {
      // Error already logged
      return boost::none;
    }

    auto current_topology { get_and_validate_current_topology() };
    if (!current_topology) {
      // Error already logged
      return boost::none;
    }

    // When dealing with the "requested device" here and in other functions we need to keep
    // in mind that it could belong to a duplicated display and thus all of them
    // need to be taken into account, which complicates everything...
    auto duplicated_devices { get_duplicate_devices(requested_device_id, *current_topology) };
    auto final_topology { determine_final_topology(config.device_prep, primary_device_requested, duplicated_devices, *current_topology) };

    // If we still have a previously configured topology, we could potentially skip making any changes to the topology.
    // However, it could also mean that we need to revert any previous changes in case we had missed that chance somehow.
    if (previously_configured_topology) {
      // If the topology we are switching to is the same as the final topology we had before,
      // we don't need to revert anything as the other handlers will take care of it.
      // Otherwise, we MUST revert the changes!
      if (!is_topology_the_same(previously_configured_topology->modified, final_topology)) {
        BOOST_LOG(warning) << "previous topology does not match the new one. Reverting previous changes!";
        revert_settings();

        // Clearing the optional to reflect the current state.
        previously_configured_topology = boost::none;

        // There is always a possibility that after reverting changes, we could
        // fail to restore the original topology for whatever reason so we need to redo
        // our previous steps just to be safe
        current_topology = get_and_validate_current_topology();
        if (!current_topology) {
          // Error already logged
          return boost::none;
        }

        duplicated_devices = get_duplicate_devices(requested_device_id, *current_topology);
        final_topology = determine_final_topology(config.device_prep, primary_device_requested, duplicated_devices, *current_topology);
      }
    }

    if (!is_topology_the_same(*current_topology, final_topology)) {
      BOOST_LOG(info) << "changing display topology to: " << to_string(final_topology);
      if (!set_topology(final_topology)) {
        // Error already logged.
        return boost::none;
      }

      // It is possible that we no longer has duplicate display, so we need to update the list
      duplicated_devices = get_duplicate_devices(requested_device_id, final_topology);
    }

    // This check is mainly to cover the case for "config.device_prep == no_operation" as we at least
    // have to validate the device exists, but it doesn't hurt to double check it in all cases.
    if (!is_device_found_in_active_topology(requested_device_id, final_topology)) {
      BOOST_LOG(error) << "device " << requested_device_id << " is not active!";
      return boost::none;
    }

    return handled_topology_data_t {
      topology_data_t {
        *current_topology,
        final_topology },
      topology_data_t {
        // We also need to take into account the previous configuration (if we still have one)
        determine_initial_topology_based_on_prev_config(previously_configured_topology, *current_topology),
        final_topology },
      topology_metadata_t {
        final_topology,
        get_newly_enabled_devices_from_topology(*current_topology, final_topology),
        primary_device_requested,
        duplicated_devices }
    };
  }

}  // namespace display_device
