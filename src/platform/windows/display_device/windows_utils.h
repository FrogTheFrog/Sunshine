#pragma once

// the most stupid windows include (because it needs to be first...)
#include <windows.h>

// local includes
#include "src/display_device/display_device.h"

namespace display_device {

  namespace w_utils {

    constexpr bool ACTIVE_ONLY_DEVICES { true };
    constexpr bool ALL_DEVICES { false };
    constexpr bool CURRENT_MODE { true };
    constexpr bool ALL_MODES { false };

    struct path_and_mode_data_t {
      std::vector<DISPLAYCONFIG_PATH_INFO> paths;
      std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    };

    std::string
    get_ccd_error_string(const LONG error_code);

    bool
    is_primary(const DISPLAYCONFIG_SOURCE_MODE &mode);

    bool
    are_duplicated_modes(const DISPLAYCONFIG_SOURCE_MODE &a, const DISPLAYCONFIG_SOURCE_MODE &b);

    bool
    is_available(const DISPLAYCONFIG_PATH_INFO &path);

    bool
    is_active(const DISPLAYCONFIG_PATH_INFO &path);

    void
    set_active(DISPLAYCONFIG_PATH_INFO &path);

    void
    set_inactive(DISPLAYCONFIG_PATH_INFO &path);

    void
    clear_path_refresh_rate(DISPLAYCONFIG_PATH_INFO &path);

    std::string
    get_device_id(const DISPLAYCONFIG_PATH_INFO &path);

    std::string
    get_friendly_name(const DISPLAYCONFIG_PATH_INFO &path);

    std::string
    get_display_name(const DISPLAYCONFIG_PATH_INFO &path);

    hdr_state_e
    get_hdr_state(const DISPLAYCONFIG_PATH_INFO &path);

    bool
    set_hdr_state(const DISPLAYCONFIG_PATH_INFO &path, bool enable);

    boost::optional<UINT32>
    get_source_index(const DISPLAYCONFIG_PATH_INFO &path, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

    boost::optional<UINT32>
    get_target_index(const DISPLAYCONFIG_PATH_INFO &path, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

    void
    set_source_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index);

    void
    set_target_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index);

    void
    set_desktop_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index);

    void
    set_clone_group_id(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index);

    const DISPLAYCONFIG_SOURCE_MODE *
    get_source_mode(const boost::optional<UINT32> &index, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

    DISPLAYCONFIG_SOURCE_MODE *
    get_source_mode(const boost::optional<UINT32> &index, std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

    const DISPLAYCONFIG_TARGET_MODE *
    get_target_mode(const boost::optional<UINT32> &index, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

    DISPLAYCONFIG_TARGET_MODE *
    get_target_mode(const boost::optional<UINT32> &index, std::vector<DISPLAYCONFIG_MODE_INFO> &modes);

    std::string
    get_device_id_for_valid_path(const DISPLAYCONFIG_PATH_INFO &path, bool must_be_active);

    boost::optional<path_and_mode_data_t>
    query_display_config(bool active_only);

    const DISPLAYCONFIG_PATH_INFO *
    get_active_path(const std::string &device_id, const std::vector<DISPLAYCONFIG_PATH_INFO> &paths);

    DISPLAYCONFIG_PATH_INFO *
    get_active_path(const std::string &device_id, std::vector<DISPLAYCONFIG_PATH_INFO> &paths);

  }  // namespace w_utils

}  // namespace display_device
