// standard includes
#include <iostream>
#include <system_error>

// local includes
#include "src/main.h"
#include "windows_utils.h"

namespace display_device {

  namespace w_utils {

    namespace {

      std::string
      convert_to_string(const std::wstring &str) {
        if (str.empty()) {
          return {};
        }

        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.to_bytes(str);
      }

    }  // namespace

    std::string
    get_ccd_error_string(const LONG error_code) {
      std::stringstream error;
      error << "[code: ";
      switch (error_code) {
        case ERROR_INVALID_PARAMETER:
          error << "ERROR_INVALID_PARAMETER";
          break;
        case ERROR_NOT_SUPPORTED:
          error << "ERROR_NOT_SUPPORTED";
          break;
        case ERROR_ACCESS_DENIED:
          error << "ERROR_ACCESS_DENIED";
          break;
        case ERROR_INSUFFICIENT_BUFFER:
          error << "ERROR_INSUFFICIENT_BUFFER";
          break;
        case ERROR_GEN_FAILURE:
          error << "ERROR_GEN_FAILURE";
          break;
        case ERROR_SUCCESS:
          error << "ERROR_SUCCESS";
          break;
        default:
          error << error_code;
          break;
      }
      error << ", message: " << std::system_category().message(static_cast<int>(HRESULT_FROM_WIN32(error_code))) << "]";
      return error.str();
    }

    bool
    is_primary(const DISPLAYCONFIG_SOURCE_MODE &mode) {
      return mode.position.x == 0 && mode.position.y == 0;
    }

    bool
    are_duplicated_modes(const DISPLAYCONFIG_SOURCE_MODE &a, const DISPLAYCONFIG_SOURCE_MODE &b) {
      // Same mode position means they are duplicated
      return a.position.x == b.position.x && a.position.y == b.position.y;
    }

    bool
    is_available(const DISPLAYCONFIG_PATH_INFO &path) {
      return path.targetInfo.targetAvailable == TRUE;
    }

    bool
    is_active(const DISPLAYCONFIG_PATH_INFO &path) {
      return static_cast<bool>(path.flags & DISPLAYCONFIG_PATH_ACTIVE);
    }

    void
    set_active(DISPLAYCONFIG_PATH_INFO &path) {
      path.flags |= DISPLAYCONFIG_PATH_ACTIVE;
    }

    void
    set_inactive(DISPLAYCONFIG_PATH_INFO &path) {
      path.flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
    }

    void
    clear_path_refresh_rate(DISPLAYCONFIG_PATH_INFO &path) {
      path.targetInfo.refreshRate.Denominator = 0;
      path.targetInfo.refreshRate.Numerator = 0;
      path.targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED;
    }

    std::string
    get_device_id(const DISPLAYCONFIG_PATH_INFO &path) {
      DISPLAYCONFIG_TARGET_DEVICE_NAME target_name = {};
      target_name.header.adapterId = path.targetInfo.adapterId;
      target_name.header.id = path.targetInfo.id;
      target_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
      target_name.header.size = sizeof(target_name);

      LONG result { DisplayConfigGetDeviceInfo(&target_name.header) };
      if (result != ERROR_SUCCESS) {
        BOOST_LOG(error) << get_ccd_error_string(result) << " failed to get target device name!";
        return {};
      }

      // This is not the prettiest id there is, but it seems to be unique.
      // The MONITOR ID that MultiMonitorTool uses is not always unique in some combinations, so we'll just go with the device path.
      auto device_id { convert_to_string(target_name.monitorDevicePath) };
      //std::replace(std::begin(device_id), std::end(device_id), '#', '-');  // Hashtags are not supported by Sunshine config
      return device_id;
    }

    std::string
    get_friendly_name(const DISPLAYCONFIG_PATH_INFO &path) {
      DISPLAYCONFIG_TARGET_DEVICE_NAME target_name = {};
      target_name.header.adapterId = path.targetInfo.adapterId;
      target_name.header.id = path.targetInfo.id;
      target_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
      target_name.header.size = sizeof(target_name);

      LONG result { DisplayConfigGetDeviceInfo(&target_name.header) };
      if (result != ERROR_SUCCESS) {
        BOOST_LOG(error) << get_ccd_error_string(result) << " failed to get target device name!";
        return {};
      }

      return target_name.flags.friendlyNameFromEdid ? convert_to_string(target_name.monitorFriendlyDeviceName) : std::string {};
    }

    std::string
    get_display_name(const DISPLAYCONFIG_PATH_INFO &path) {
      DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name = {};
      source_name.header.id = path.sourceInfo.id;
      source_name.header.adapterId = path.sourceInfo.adapterId;
      source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source_name.header.size = sizeof(source_name);

      LONG result { DisplayConfigGetDeviceInfo(&source_name.header) };
      if (result != ERROR_SUCCESS) {
        BOOST_LOG(error) << get_ccd_error_string(result) << " failed to get display name! ";
        return {};
      }

      return convert_to_string(source_name.viewGdiDeviceName);
    }

    hdr_state_e
    get_hdr_state(const DISPLAYCONFIG_PATH_INFO &path) {
      if (!is_active(path)) {
        return hdr_state_e::unknown;
      }

      DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color_info = {};
      color_info.header.adapterId = path.targetInfo.adapterId;
      color_info.header.id = path.targetInfo.id;
      color_info.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
      color_info.header.size = sizeof(color_info);

      LONG result { DisplayConfigGetDeviceInfo(&color_info.header) };
      if (result != ERROR_SUCCESS) {
        BOOST_LOG(error) << get_ccd_error_string(result) << " failed to get advanced color info! ";
        return hdr_state_e::unknown;
      }

      return color_info.advancedColorSupported ? color_info.advancedColorEnabled ? hdr_state_e::enabled : hdr_state_e::disabled : hdr_state_e::unknown;
    }

    bool
    set_hdr_state(const DISPLAYCONFIG_PATH_INFO &path, bool enable) {
      DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE color_state = {};
      color_state.header.adapterId = path.targetInfo.adapterId;
      color_state.header.id = path.targetInfo.id;
      color_state.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
      color_state.header.size = sizeof(color_state);

      color_state.enableAdvancedColor = enable ? 1 : 0;

      LONG result { DisplayConfigSetDeviceInfo(&color_state.header) };
      if (result != ERROR_SUCCESS) {
        BOOST_LOG(error) << get_ccd_error_string(result) << " failed to set advanced color info!";
        return false;
      }

      return true;
    }

    boost::optional<UINT32>
    get_source_index(const DISPLAYCONFIG_PATH_INFO &path, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes) {
      UINT32 index {};
      if (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) {
        index = path.sourceInfo.sourceModeInfoIdx;
        if (index == DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID) {
          return boost::none;
        }
      }
      else {
        index = path.sourceInfo.modeInfoIdx;
        if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
          return boost::none;
        }
      }

      if (index >= modes.size()) {
        BOOST_LOG(error) << "source index " << index << " is out of range " << modes.size();
        return boost::none;
      }

      return index;
    }

    boost::optional<UINT32>
    get_target_index(const DISPLAYCONFIG_PATH_INFO &path, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes) {
      UINT32 index {};
      if (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) {
        index = path.targetInfo.targetModeInfoIdx;
        if (index == DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID) {
          return boost::none;
        }
      }
      else {
        index = path.targetInfo.modeInfoIdx;
        if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID) {
          return boost::none;
        }
      }

      if (index >= modes.size()) {
        BOOST_LOG(error) << "target index " << index << " is out of range " << modes.size();
        return boost::none;
      }

      return index;
    }

    void
    set_source_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index) {
      if (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) {
        if (index) {
          path.sourceInfo.sourceModeInfoIdx = *index;
        }
        else {
          path.sourceInfo.sourceModeInfoIdx = DISPLAYCONFIG_PATH_SOURCE_MODE_IDX_INVALID;
        }
      }
      else {
        if (index) {
          path.sourceInfo.modeInfoIdx = *index;
        }
        else {
          path.sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        }
      }
    }

    void
    set_target_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index) {
      if (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) {
        if (index) {
          path.targetInfo.targetModeInfoIdx = *index;
        }
        else {
          path.targetInfo.targetModeInfoIdx = DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID;
        }
      }
      else {
        if (index) {
          path.targetInfo.modeInfoIdx = *index;
        }
        else {
          path.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        }
      }
    }

    void
    set_desktop_index(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index) {
      if (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) {
        if (index) {
          path.targetInfo.desktopModeInfoIdx = *index;
        }
        else {
          path.targetInfo.desktopModeInfoIdx = DISPLAYCONFIG_PATH_DESKTOP_IMAGE_IDX_INVALID;
        }
      }
    }

    void
    set_clone_group_id(DISPLAYCONFIG_PATH_INFO &path, const boost::optional<UINT32> &index) {
      if (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) {
        if (index) {
          path.sourceInfo.cloneGroupId = *index;
        }
        else {
          path.sourceInfo.cloneGroupId = DISPLAYCONFIG_PATH_CLONE_GROUP_INVALID;
        }
      }
    }

    const DISPLAYCONFIG_SOURCE_MODE *
    get_source_mode(const boost::optional<UINT32> &index, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes) {
      if (!index) {
        return nullptr;
      }

      const auto &mode { modes[*index] };
      if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
        BOOST_LOG(error) << "mode at index " << *index << " is not source mode!";
        return nullptr;
      }

      return &mode.sourceMode;
    }

    DISPLAYCONFIG_SOURCE_MODE *
    get_source_mode(const boost::optional<UINT32> &index, std::vector<DISPLAYCONFIG_MODE_INFO> &modes) {
      return const_cast<DISPLAYCONFIG_SOURCE_MODE *>(get_source_mode(index, const_cast<const std::vector<DISPLAYCONFIG_MODE_INFO> &>(modes)));
    }

    const DISPLAYCONFIG_TARGET_MODE *
    get_target_mode(const boost::optional<UINT32> &index, const std::vector<DISPLAYCONFIG_MODE_INFO> &modes) {
      if (!index) {
        return nullptr;
      }

      const auto &mode { modes[*index] };
      if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
        BOOST_LOG(error) << "mode at index " << *index << " is not target mode!";
        return nullptr;
      }

      return &mode.targetMode;
    }

    DISPLAYCONFIG_TARGET_MODE *
    get_target_mode(const boost::optional<UINT32> &index, std::vector<DISPLAYCONFIG_MODE_INFO> &modes) {
      return const_cast<DISPLAYCONFIG_TARGET_MODE *>(get_target_mode(index, const_cast<const std::vector<DISPLAYCONFIG_MODE_INFO> &>(modes)));
    }

    std::string
    get_device_id_for_valid_path(const DISPLAYCONFIG_PATH_INFO &path, bool must_be_active) {
      // As far as we are concerned, for us the path is valid as long as it is available,
      // has a device id that we can use and has a display name. Optionally, we might
      // want it to be active.

      if (!is_available(path)) {
        // Could be transient issue according to MSDOCS (no longer available, but still "active")
        return {};
      }

      if (must_be_active) {
        if (!is_active(path)) {
          return {};
        }
      }

      const auto current_id { get_device_id(path) };
      if (current_id.empty()) {
        return {};
      }

      const auto display_name { get_display_name(path) };
      if (display_name.empty()) {
        return {};
      }

      return current_id;
    }

    boost::optional<path_and_mode_data_t>
    query_display_config(bool active_only) {
      std::vector<DISPLAYCONFIG_PATH_INFO> paths;
      std::vector<DISPLAYCONFIG_MODE_INFO> modes;
      LONG result = ERROR_SUCCESS;

      // When we want to enable/disable displays, we need to get all paths as they will not be active.
      // This will require some additional filtering of duplicate and otherwise useless paths.
      UINT32 flags = active_only ? QDC_ONLY_ACTIVE_PATHS : QDC_ALL_PATHS;
      flags |= QDC_VIRTUAL_MODE_AWARE;  // supported from W10 onwards

      do {
        UINT32 path_count { 0 };
        UINT32 mode_count { 0 };

        result = GetDisplayConfigBufferSizes(flags, &path_count, &mode_count);
        if (result != ERROR_SUCCESS) {
          BOOST_LOG(error) << get_ccd_error_string(result) << " failed to get display paths and modes!";
          return boost::none;
        }

        paths.resize(path_count);
        modes.resize(mode_count);
        result = QueryDisplayConfig(flags, &path_count, paths.data(), &mode_count, modes.data(), nullptr);

        // The function may have returned fewer paths/modes than estimated
        paths.resize(path_count);
        modes.resize(mode_count);

        // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
        // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
      } while (result == ERROR_INSUFFICIENT_BUFFER);

      if (result != ERROR_SUCCESS) {
        BOOST_LOG(error) << get_ccd_error_string(result) << " failed to query display paths and modes!";
        return boost::none;
      }

      return path_and_mode_data_t { paths, modes };
    }

    const DISPLAYCONFIG_PATH_INFO *
    get_active_path(const std::string &device_id, const std::vector<DISPLAYCONFIG_PATH_INFO> &paths) {
      for (const auto &path : paths) {
        const auto current_id { get_device_id_for_valid_path(path, ACTIVE_ONLY_DEVICES) };
        if (current_id.empty()) {
          continue;
        }

        if (current_id == device_id) {
          return &path;
        }
      }

      return nullptr;
    }

    DISPLAYCONFIG_PATH_INFO *
    get_active_path(const std::string &device_id, std::vector<DISPLAYCONFIG_PATH_INFO> &paths) {
      return const_cast<DISPLAYCONFIG_PATH_INFO *>(get_active_path(device_id, const_cast<const std::vector<DISPLAYCONFIG_PATH_INFO> &>(paths)));
    }

  }  // namespace w_utils

}  // namespace display_device
