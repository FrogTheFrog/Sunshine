// local includes
#include "src/main.h"
#include "windows_utils.h"

namespace display_device {

  namespace {

    bool
    fuzzy_compare_refresh_rates(const refresh_rate_t &r1, const refresh_rate_t &r2, const float max_diff = 1.f) {
      if (r1.denominator > 0 && r2.denominator > 0) {
        const float r1_f { static_cast<float>(r1.numerator) / r1.denominator };
        const float r2_f { static_cast<float>(r2.numerator) / r2.denominator };
        return (std::abs(r1_f - r2_f) <= max_diff);
      }

      return false;
    }

    bool
    fuzzy_compare_modes(const display_mode_t &a, const display_mode_t &b) {
      return a.resolution.width == b.resolution.width &&
             a.resolution.height == b.resolution.height &&
             fuzzy_compare_refresh_rates(a.refresh_rate, b.refresh_rate);
    }

    /*
     * Get all the devices that are duplicated ones. See comment where it is used as to
     * why we need this.
     */
    std::unordered_set<std::string>
    get_all_duplicated_devices(const std::unordered_set<std::string> &device_ids) {
      const auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
      if (!display_data) {
        // Error already logged
        return {};
      }

      // We start by iterating over the provided device id (or paths) and try to get a source mode
      // which contains the necessary info
      std::unordered_set<std::string> all_device_ids;
      for (const auto &device_id : device_ids) {
        if (device_id.empty()) {
          BOOST_LOG(error) << "device it is empty!";
          return {};
        }

        const auto provided_path { w_utils::get_active_path(device_id, display_data->paths) };
        if (!provided_path) {
          BOOST_LOG(warning) << "failed to find device for " << device_id << "!";
          return {};
        }

        const auto provided_path_source_mode { w_utils::get_source_mode(w_utils::get_source_index(*provided_path, display_data->modes), display_data->modes) };
        if (!provided_path_source_mode) {
          BOOST_LOG(error) << "active device does not have a source mode: " << device_id << "!";
          return {};
        }

        // We will now iterate over all of the active paths (provided path included) and check if
        // any of them are duplicated.
        for (const auto &path : display_data->paths) {
          const auto current_id { w_utils::get_device_id_for_valid_path(path, w_utils::ACTIVE_ONLY_DEVICES) };
          if (current_id.empty()) {
            continue;
          }

          if (all_device_ids.count(current_id) > 0) {
            // Already checked
            continue;
          }

          const auto source_mode { w_utils::get_source_mode(w_utils::get_source_index(path, display_data->modes), display_data->modes) };
          if (!source_mode) {
            BOOST_LOG(error) << "active device does not have a source mode: " << current_id << "!";
            return {};
          }

          if (!w_utils::are_duplicated_modes(*provided_path_source_mode, *source_mode)) {
            continue;
          }

          all_device_ids.insert(current_id);
        }
      }

      return all_device_ids;
    }

    bool
    do_set_modes(const device_display_mode_map_t &modes, bool allow_changes) {
      auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
      if (!display_data) {
        // Error already logged
        return false;
      }

      bool changes_applied { false };
      for (const auto &[device_id, mode] : modes) {
        const auto path { w_utils::get_active_path(device_id, display_data->paths) };
        if (!path) {
          BOOST_LOG(error) << "failed to find device for " << device_id << "!";
          return false;
        }

        const auto source_mode { w_utils::get_source_mode(w_utils::get_source_index(*path, display_data->modes), display_data->modes) };
        if (!source_mode) {
          BOOST_LOG(error) << "active device does not have a source mode: " << device_id << "!";
          return false;
        }

        bool new_changes { false };
        const bool resolution_changed { source_mode->width != mode.resolution.width || source_mode->height != mode.resolution.height };
        const bool refresh_rate_changed { path->targetInfo.refreshRate.Numerator != mode.refresh_rate.numerator ||
                                          path->targetInfo.refreshRate.Denominator != mode.refresh_rate.denominator };

        if (resolution_changed) {
          source_mode->width = mode.resolution.width;
          source_mode->height = mode.resolution.height;
          new_changes = true;
        }

        if (refresh_rate_changed) {
          path->targetInfo.refreshRate = { mode.refresh_rate.numerator, mode.refresh_rate.denominator };
          new_changes = true;
        }

        if (new_changes) {
          // Clear the target index so that Windows has to select a new target mode.
          w_utils::set_target_index(*path, boost::none);
          w_utils::set_desktop_index(*path, boost::none);  // Part of struct containing target index and so it needs to be cleared
        }

        changes_applied = changes_applied || new_changes;
      }

      if (!changes_applied) {
        BOOST_LOG(debug) << "no changes were made to display modes.";
        return true;
      }

      UINT32 flags { SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE | SDC_VIRTUAL_MODE_AWARE };
      if (allow_changes) {
        // It's probably best for Windows to select the "best" display settings for us. However, in case we
        // have custom resolution set in nvidia control panel for example, this flag will prevent successfully applying
        // settings to it.
        flags |= SDC_ALLOW_CHANGES;
      }

      const LONG result { SetDisplayConfig(display_data->paths.size(), display_data->paths.data(), display_data->modes.size(), display_data->modes.data(), flags) };
      if (result != ERROR_SUCCESS) {
        BOOST_LOG(error) << w_utils::get_ccd_error_string(result) << " failed to set display mode!";
        return false;
      }

      return true;
    };

  }  // namespace

  device_display_mode_map_t
  get_current_display_modes(const std::unordered_set<std::string> &device_ids) {
    if (device_ids.empty()) {
      BOOST_LOG(error) << "device id set is empty!";
      return {};
    }

    const auto display_data { w_utils::query_display_config(w_utils::ACTIVE_ONLY_DEVICES) };
    if (!display_data) {
      // Error already logged
      return {};
    }

    device_display_mode_map_t current_modes;
    for (const auto &device_id : device_ids) {
      if (device_id.empty()) {
        BOOST_LOG(error) << "device id is empty!";
        return {};
      }

      const auto path { w_utils::get_active_path(device_id, display_data->paths) };
      if (!path) {
        BOOST_LOG(error) << "failed to find device for " << device_id << "!";
        return {};
      }

      const auto source_mode { w_utils::get_source_mode(w_utils::get_source_index(*path, display_data->modes), display_data->modes) };
      if (!source_mode) {
        BOOST_LOG(error) << "active device does not have a source mode: " << device_id << "!";
        return {};
      }

      // For whatever reason they put refresh rate into path, but not the resolution.
      const auto target_refresh_rate { path->targetInfo.refreshRate };
      current_modes[device_id] = display_mode_t {
        { source_mode->width, source_mode->height },
        { target_refresh_rate.Numerator, target_refresh_rate.Denominator }
      };
    }

    return current_modes;
  }

  bool
  set_display_modes(const device_display_mode_map_t &modes) {
    if (modes.empty()) {
      BOOST_LOG(error) << "modes map is empty!";
      return false;
    }

    std::unordered_set<std::string> device_ids;
    for (const auto &[device_id, _] : modes) {
      if (!device_ids.insert(device_id).second) {
        // Sanity check since, it's technically not possible with unordered map to have duplicate keys
        BOOST_LOG(error) << "duplicate device id provided: " << device_id << "!";
        return false;
      }
    }

    // Here it is important to check that we have all the necessary modes, otherwise
    // setting modes will fail with ambiguous message.
    //
    // Duplicated devices can have different target modes (monitor) with different refresh rate,
    // however this does not apply to the source mode (frame buffer?) and they must have same
    // resolution.
    //
    // Without SDC_VIRTUAL_MODE_AWARE, devices would share the same source mode entry, but now
    // they have separate entries that are more or less identical.
    //
    // To avoid surprising end-user with unexpected source mode change, we validate the input
    // instead of changing it automatically. This also resolve the problem of having to choose
    // refresh rate for duplicate display - leave it to the end-user of this function...
    const auto all_device_ids { get_all_duplicated_devices(device_ids) };
    if (all_device_ids.empty()) {
      BOOST_LOG(error) << "failed to get all duplicated devices!";
      return false;
    }

    if (all_device_ids.size() != device_ids.size()) {
      BOOST_LOG(error) << "not all modes for duplicate displays were provided!";
      return false;
    }

    const auto original_modes { get_current_display_modes(device_ids) };
    if (original_modes.empty()) {
      // Error already logged
      return false;
    }

    constexpr bool allow_changes { true };
    if (!do_set_modes(modes, allow_changes)) {
      // Error already logged
      return false;
    }

    const auto all_modes_match = [&modes](const device_display_mode_map_t &current_modes) {
      for (const auto &[device_id, requested_mode] : modes) {
        auto mode_it { current_modes.find(device_id) };
        if (mode_it == std::end(current_modes)) {
          // I mean this race condition of disconnecting display device is technically possible...
          return false;
        }

        if (!fuzzy_compare_modes(mode_it->second, requested_mode)) {
          return false;
        }
      }

      return true;
    };

    auto current_modes { get_current_display_modes(device_ids) };
    if (!current_modes.empty()) {
      if (all_modes_match(current_modes)) {
        return true;
      }

      // We have a problem when using SetDisplayConfig with SDC_ALLOW_CHANGES
      // (which we should use as otherwise we need to set EVERYTHING correctly)
      // where it decides to use our new mode merely as a suggestion.
      //
      // This is good, since we don't have to be very precise with refresh rate,
      // but also bad since it can just ignore our specified mode.
      //
      // However, it is possible that the user has created a custom display modes
      // which is not exposed to the via Windows settings app. To allow this
      // resolution to be selected, we actually need to omit SDC_ALLOW_CHANGES
      // flag.

      // If the settings are completely bonkers, this could fail with the following message:
      //     [code: 1610, message: The configuration data for this product is corrupt. Contact your support personnel] failed to set display mode!
      BOOST_LOG(info) << "failed to change display modes using Windows recommended modes, trying to set modes more strictly!";
      if (do_set_modes(modes, !allow_changes)) {
        current_modes = get_current_display_modes(device_ids);
        if (!current_modes.empty() && all_modes_match(current_modes)) {
          return true;
        }
      }
    }

    do_set_modes(original_modes, allow_changes);  // Return value does not matter as we are trying out best to undo
    BOOST_LOG(error) << "failed to set display mode(-s) completely!";
    return false;
  }

}  // namespace display_device
