#pragma once

// standard includes
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

// lib includes
#include <boost/optional.hpp>
#include <nlohmann/json.hpp>

namespace display_device {

  enum class device_state_e {
    inactive,
    active,
    primary  //! On Windows we can have multiple primary displays (when they are duplicated).
  };

  enum class hdr_state_e {
    unknown,  //! HDR state could not be retrieved from the system (even if the display could support it).
    disabled,
    enabled
  };

  // For JSON serialization for hdr_state_e
  NLOHMANN_JSON_SERIALIZE_ENUM(hdr_state_e, { { hdr_state_e::unknown, "unknown" },
                                              { hdr_state_e::disabled, "disabled" },
                                              { hdr_state_e::enabled, "enabled" } })

  //! A map of device id to its HDR state (ordered, for predictable print order)
  using hdr_state_map_t = std::map<std::string, hdr_state_e>;

  struct device_info_t {
    //! A name used by the system to represent the logical display this device is connected to.
    std::string display_name;

    //! A more human-readable name for the device.
    std::string friendly_name;

    //! Current state of the device.
    device_state_e device_state;

    //! Current state of the HDR support.
    hdr_state_e hdr_state;
  };

  //! A map of device id to its info data (ordered, for predictable print order).
  using device_info_map_t = std::map<std::string, device_info_t>;

  struct resolution_t {
    unsigned int width;
    unsigned int height;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(resolution_t, width, height)
  };

  //! Stores a floating point number in a "numerator/denominator" form
  struct refresh_rate_t {
    unsigned int numerator;
    unsigned int denominator;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(refresh_rate_t, numerator, denominator)
  };

  struct display_mode_t {
    resolution_t resolution;
    refresh_rate_t refresh_rate;

    // For JSON serialization
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(display_mode_t, resolution, refresh_rate)
  };

  // A map of device id to its mode data (ordered, for predictable print order).
  using device_display_mode_map_t = std::map<std::string, display_mode_t>;

  /*!
   * A list of a list of device ids representing the current topology.
   *
   * For example:
   * ```
   * [[EXTENDED_DISPLAY_1], [DUPLICATED_DISPLAY_1, DUPLICATED_DISPLAY_2], [EXTENDED_DISPLAY_2]]
   * ```
   *
   * @note On Windows the order does not matter as Windows will take care of the device the placement anyway.
   */
  using active_topology_t = std::vector<std::vector<std::string>>;

  /*!
   * Enumerates the available devices in the system.
   */
  device_info_map_t
  enum_available_devices();

  /*!
   * Gets display name associated with the device.
   * @note returns empty string if the device_id is empty or device is inactive.
   */
  std::string
  get_display_name(const std::string &device_id);

  /*!
   * Get current display mode for the provided devices.
   *
   * @note empty map will be returned if any of the devices does not have a mode.
   */
  device_display_mode_map_t
  get_current_display_modes(const std::unordered_set<std::string> &device_ids);

  /*!
   * Try to set the new display modes for the devices.
   *
   * @warning if any of the specified display are duplicated, modes MUST be provided
   *          for duplicates too!
   */
  bool
  set_display_modes(const device_display_mode_map_t &modes);

  /*!
   * Check whether the specified device is primary.
   */
  bool
  is_primary_device(const std::string &device_id);

  /*!
   * Try to set the device as a primary display.
   *
   * @note if the device is duplicated, the other paired device will also become a primary display.
   */
  bool
  set_as_primary_device(const std::string &device_id);

  /*!
   * Try to get the HDR state for the provided devices.
   *
   * @note on Windows the state cannot be retrieved until the device is active.
   */
  hdr_state_map_t
  get_current_hdr_states(const std::unordered_set<std::string> &device_ids);

  /*!
   * Try to set the HDR state for the devices.
   *
   * @note if UNKNOWN states are provided, they will be ignored.
   */
  bool
  set_hdr_states(const hdr_state_map_t &states);

  /*!
   * Get the currently active topology.
   *
   * @note empty list will be returned if topology could not be retrieved.
   */
  active_topology_t
  get_current_topology();

  /*!
   * Simply validates the topology to be valid.
   */
  bool
  is_topology_valid(const active_topology_t &topology);

  /*!
   * Checks if the topologies are close enough to be considered the same by the system.
   */
  bool
  is_topology_the_same(const active_topology_t &a, const active_topology_t &b);

  /*!
   * Try to set the active states for the devices.
   *
   * @warning there is a bug on Windows (yay) where it is unable to sometimes set
   *          topology correctly, but it thinks it did! See implementation for more
   *          details.
   */
  bool
  set_topology(const active_topology_t &new_topology);

}  // namespace display_device
