// standard includes
#include "fstream"

// local includes
#include "settings_topology.h"
#include "src/audio.h"
#include "src/display_device/to_string.h"
#include "src/main.h"

namespace display_device {

  namespace {

    std::string
    get_current_primary_display(const topology_metadata_t &metadata) {
      for (const auto &group : metadata.current_topology) {
        for (const auto &device_id : group) {
          if (is_primary_device(device_id)) {
            return device_id;
          }
        }
      }

      return std::string {};
    }

    std::string
    determine_new_primary_display(const parsed_config_t::device_prep_e &device_prep, const std::string &original_primary_display, const topology_metadata_t &metadata) {
      if (device_prep == parsed_config_t::device_prep_e::ensure_primary) {
        if (metadata.primary_device_requested) {
          // Primary device was requested - no device was specified by user.
          // This means we are keeping the original primary display.
        }
        else {
          // For primary devices it is enough to set 1 as a primary as the whole duplicated group
          // will become primary devices.
          const auto new_primary_device { metadata.duplicated_devices.front() };
          return new_primary_device;
        }
      }

      return original_primary_display;
    }

    device_display_mode_map_t
    determine_new_display_modes(const boost::optional<resolution_t> &resolution, const boost::optional<refresh_rate_t> &refresh_rate, const device_display_mode_map_t &original_display_modes, const topology_metadata_t &metadata) {
      device_display_mode_map_t new_modes { original_display_modes };

      if (resolution) {
        // For duplicate devices the resolution must match no matter what
        for (const auto &device_id : metadata.duplicated_devices) {
          new_modes[device_id].resolution = *resolution;
        }
      }

      if (refresh_rate) {
        if (metadata.primary_device_requested) {
          // No device has been specified, so if they're all are primary devices
          // we need to apply the refresh rate change to all duplicates
          for (const auto &device_id : metadata.duplicated_devices) {
            new_modes[device_id].refresh_rate = *refresh_rate;
          }
        }
        else {
          // Even if we have duplicate devices, their refresh rate may differ
          // and since the device was specified, let's apply the refresh
          // rate only to the specified device.
          new_modes[metadata.duplicated_devices.front()].refresh_rate = *refresh_rate;
        }
      }

      return new_modes;
    }

    hdr_state_map_t
    determine_new_hdr_states(const boost::optional<bool> &change_hdr_state, const hdr_state_map_t &original_hdr_states, const topology_metadata_t &metadata) {
      hdr_state_map_t new_states { original_hdr_states };

      if (change_hdr_state) {
        const hdr_state_e end_state { *change_hdr_state ? hdr_state_e::enabled : hdr_state_e::disabled };
        const auto try_update_new_state = [&new_states, end_state](const std::string &device_id) {
          const auto current_state { new_states[device_id] };
          if (current_state == hdr_state_e::unknown) {
            return;
          }

          new_states[device_id] = end_state;
        };

        if (metadata.primary_device_requested) {
          // No device has been specified, so if they're all are primary devices
          // we need to apply the HDR state change to all duplicates
          for (const auto &device_id : metadata.duplicated_devices) {
            try_update_new_state(device_id);
          }
        }
        else {
          // Even if we have duplicate devices, their HDR states may differ
          // and since the device was specified, let's apply the HDR state
          // only to the specified device.
          try_update_new_state(metadata.duplicated_devices.front());
        }
      }

      return new_states;
    }

    /*!
     * Some newly enabled displays do not handle HDR state correctly (IDD HDR display for example).
     * The colors can become very blown out/high contrast. A simple workaround is to toggle the HDR state
     * once the display has "settled down" or something.
     *
     * This is what this function does, it changes the HDR state to the opposite once that we will have in the
     * end, sleeps for a little and then allows us to continue changing HDR states to the final ones.
     *
     * "blank" comes as an inspiration from "vblank" as this function is meant to be used before changing the HDR
     * states to clean up something.
     */
    bool
    blank_hdr_states(const hdr_state_map_t &states, const std::unordered_set<std::string> &newly_enabled_devices) {
      const std::chrono::milliseconds delay { 1500 };
      if (delay > std::chrono::milliseconds::zero()) {
        bool state_changed { false };
        auto toggled_states { states };
        for (const auto &device_id : newly_enabled_devices) {
          auto state_it { toggled_states.find(device_id) };
          if (state_it == std::end(toggled_states)) {
            continue;
          }

          if (state_it->second == hdr_state_e::enabled) {
            state_it->second = hdr_state_e::disabled;
            state_changed = true;
          }
          else if (state_it->second == hdr_state_e::disabled) {
            state_it->second = hdr_state_e::enabled;
            state_changed = true;
          }
        }

        if (state_changed) {
          BOOST_LOG(info) << "toggling HDR states for newly enabled devices and waiting for " << delay.count() << "ms before actually applying the correct states.";
          if (!set_hdr_states(toggled_states)) {
            // Error already logged
            return false;
          }

          std::this_thread::sleep_for(delay);
        }
      }

      return true;
    }

    void
    remove_file(const std::filesystem::path &filepath) {
      try {
        if (!filepath.empty()) {
          std::filesystem::remove(filepath);
        }
      }
      catch (const std::exception &err) {
        BOOST_LOG(error) << "failed to remove " << filepath << ". Error: " << err.what();
      }
    }

  }  // namespace

  //! A simple struct that automatically captures the audio ctx reference to extend audio session.
  struct settings_t::shared_audio_t {
    decltype(audio::get_audio_ctx_ref()) audio_ref { audio::get_audio_ctx_ref() };
  };

  settings_t::settings_t() {
  }

  settings_t::~settings_t() {
  }

  settings_t::apply_result_t
  settings_t::apply_config(const config::video_t &config, const rtsp_stream::launch_session_t &session) {
    const auto parsed_config { make_parsed_config(config, session) };
    if (!parsed_config) {
      return { apply_result_t::result_e::config_parse_fail };
    }

    const auto result { apply_config(*parsed_config) };
    if (result) {
      if (parsed_config->device_prep == parsed_config_t::device_prep_e::ensure_only_display) {
        // It is very likely that in this situation our "current" audio device is gone, so we
        // want to extend the audio "session" until we revert our changes
        BOOST_LOG(info) << "Extending audio session lifetime";
        shared_audio = std::make_unique<shared_audio_t>();
      }
      else if (shared_audio) {
        // Just to be safe in the future when the video config can be reloaded
        // without Sunshine restarting, we should cleanup
        BOOST_LOG(info) << "Releasing audio session lifetime";
        shared_audio = nullptr;
      }
    }

    return result;
  }

  settings_t::apply_result_t
  settings_t::apply_config(const parsed_config_t &config) {
    // The idea behind this method is simple.
    //
    // We take a original settings as our base. The original settings can be either the
    // settings from when we applied configuration for the first time, or if we don't have
    // a original settings from previous configuration, we take the current settings.
    //
    // We then apply new settings over our base settings. By doing this we make sure
    // that we always have a clean slate - if we apply config multiple times, the settings
    // will not accumulate and the things that we don't configure will be automatically
    // reverted.

    const boost::optional<topology_data_t> previously_configured_topology { data ? boost::make_optional(data->topology) : boost::none };
    const auto topology_result { handle_device_topology_configuration(config, previously_configured_topology, [&]() {
      revert_settings();
    }) };
    if (!topology_result) {
      // Error already logged
      return { apply_result_t::result_e::topology_fail };
    }

    data_t current_settings;
    auto guard = util::fail_guard([&]() {
      revert_settings(current_settings);
    });

    current_settings.topology = topology_result->temporary_topology_data;
    current_settings.original_primary_display = get_current_primary_display(topology_result->metadata);
    current_settings.original_modes = get_current_display_modes(get_device_ids_from_topology(topology_result->metadata.current_topology));
    current_settings.original_hdr_states = get_current_hdr_states(get_device_ids_from_topology(topology_result->metadata.current_topology));

    // Sanity check
    if (current_settings.original_primary_display.empty() ||
        current_settings.original_modes.empty() ||
        current_settings.original_hdr_states.empty()) {
      // Some error should already be logged except for "original_primary_display"
      return { apply_result_t::result_e::validation_fail };
    }

    // Gets the original field from either the previous data (preferred) or new original settings.
    const auto get_original_field = [&](auto &&field) {
      return (data ? *data : current_settings).*field;
    };

    const auto new_primary_display { determine_new_primary_display(config.device_prep, get_original_field(&data_t::original_primary_display), topology_result->metadata) };
    BOOST_LOG(info) << "changing primary display to: " << new_primary_display;
    if (!set_as_primary_device(new_primary_display)) {
      // Error already logged
      return { apply_result_t::result_e::primary_display_fail };
    }

    const auto new_modes { determine_new_display_modes(config.resolution, config.refresh_rate, get_original_field(&data_t::original_modes), topology_result->metadata) };
    BOOST_LOG(info) << "changing display modes to: " << to_string(new_modes);
    if (!set_display_modes(new_modes)) {
      // Error already logged
      return { apply_result_t::result_e::modes_fail };
    }

    const auto new_hdr_states { determine_new_hdr_states(config.change_hdr_state, get_original_field(&data_t::original_hdr_states), topology_result->metadata) };
    if (!blank_hdr_states(new_hdr_states, topology_result->metadata.newly_enabled_devices)) {
      // Error already logged
      return { apply_result_t::result_e::hdr_states_fail };
    }

    BOOST_LOG(info) << "changing HDR states to: " << to_string(new_hdr_states);
    if (!set_hdr_states(new_hdr_states)) {
      // Error already logged
      return { apply_result_t::result_e::hdr_states_fail };
    }

    if (data) {
      // This is the only value we will take over since the initial topology could have
      // been changed by the user and this is the only change we will accept.
      const auto prev_topology { data->topology };
      data->topology = topology_result->final_topology_data;
      if (!save_settings(*data)) {
        data->topology = prev_topology;
        return { apply_result_t::result_e::file_save_fail };
      }
    }
    else {
      data = std::make_unique<data_t>(current_settings);
      data->topology = topology_result->final_topology_data;
      if (!save_settings(*data)) {
        data = nullptr;
        return { apply_result_t::result_e::file_save_fail };
      }
    }

    guard.disable();
    return { apply_result_t::result_e::success };
  }

  void
  settings_t::revert_settings() {
    if (!data) {
      load_settings();
    }

    if (data) {
      revert_settings(*data);
      remove_file(filepath);
      data = nullptr;
    }

    if (shared_audio) {
      BOOST_LOG(info) << "Releasing audio session lifetime";
      shared_audio = nullptr;
    }
  }

  void
  settings_t::revert_settings(const data_t &data) {
    // On Windows settings are saved per an active topology list/pairing/set.
    // This makes it complicated when having to revert the changes as we MUST
    // be in the same topology we made those changes to (except for HDR, because it's
    // not a part of a path/mode lists that are used for topology, but the display
    // still needs to be activate to change it).
    //
    // Unplugging inactive devices does not change the topology, however plugging one
    // in will (maybe), as Windows seems to try to activate the device automatically. Unplugging
    // active device will also change the topology.

    const bool initial_topology_was_changed { !is_topology_the_same(data.topology.initial, data.topology.modified) };
    const bool primary_display_was_changed { !data.original_primary_display.empty() };
    const bool display_modes_were_changed { !data.original_modes.empty() };
    const bool hdr_states_were_changed { !data.original_hdr_states.empty() };
    const bool topology_change_is_needed_for_reverting_changes { primary_display_was_changed || display_modes_were_changed || hdr_states_were_changed };

    if (!topology_change_is_needed_for_reverting_changes && !initial_topology_was_changed) {
      return;
    }

    const auto topology_we_modified { data.topology.modified };
    const auto current_topology { get_current_topology() };
    BOOST_LOG(info) << "current display topology: " << to_string(current_topology);

    bool changed_topology_as_last_effort { false };
    bool topology_is_same_as_when_we_modified { true };
    if (!is_topology_the_same(current_topology, topology_we_modified)) {
      topology_is_same_as_when_we_modified = false;
      BOOST_LOG(warning) << "topology is different from the one that was modified!";

      if (topology_change_is_needed_for_reverting_changes) {
        BOOST_LOG(info) << "changing back to the modified topology to revert the changes.";
        if (!set_topology(topology_we_modified)) {
          // Error already logged
        }
        else {
          changed_topology_as_last_effort = true;
          topology_is_same_as_when_we_modified = true;
        }
      }
    }

    if (hdr_states_were_changed) {
      if (topology_is_same_as_when_we_modified) {
        BOOST_LOG(info) << "changing back the HDR states to: " << to_string(data.original_hdr_states);
        if (!set_hdr_states(data.original_hdr_states)) {
          // Error already logged
        }
      }
      else {
        BOOST_LOG(error) << "current topology is not the same when HDR states were changed. Cannot revert the changes!";
      }
    }

    if (display_modes_were_changed) {
      if (topology_is_same_as_when_we_modified) {
        BOOST_LOG(info) << "changing back the display modes to: " << to_string(data.original_modes);
        if (!set_display_modes(data.original_modes)) {
          // Error already logged
        }
      }
      else {
        BOOST_LOG(error) << "current topology is not the same when display modes were changed. Cannot revert the changes!";
      }
    }

    if (primary_display_was_changed) {
      if (topology_is_same_as_when_we_modified) {
        BOOST_LOG(info) << "changing back the primary device to: " << data.original_primary_display;
        if (!set_as_primary_device(data.original_primary_display)) {
          // Error already logged
        }
      }
      else {
        BOOST_LOG(error) << "current topology is not the same when primary display was changed. Cannot revert the changes!";
      }
    }

    bool last_topology_lifeline { false };
    if (changed_topology_as_last_effort) {
      BOOST_LOG(info) << "changing back to the original topology before recovery has started: " << to_string(current_topology);
      if (!set_topology(current_topology)) {
        // Error already logged
        last_topology_lifeline = true;
      }
    }
    else if (topology_is_same_as_when_we_modified && initial_topology_was_changed) {
      BOOST_LOG(info) << "changing back to the initial topology before if was first modified: " << to_string(data.topology.initial);
      if (!set_topology(data.topology.initial)) {
        // Error already logged
        last_topology_lifeline = true;
      }
    }

    if (last_topology_lifeline) {
      // If we are here we don't know what's happening.
      // User could end up with display that is not visible or something, so maybe
      // the best choice now is to try to active every single display that
      // is available?

      // Extended topology should be the one with the biggest chance of
      // success.
      active_topology_t extended_topology;
      const auto devices { enum_available_devices() };
      for (const auto &[device_id, _] : devices) {
        extended_topology.push_back({ device_id });
      }

      BOOST_LOG(warning) << "activating all displays as the last ditch effort.";
      if (!set_topology(extended_topology)) {
        // Error already logged
        last_topology_lifeline = true;
      }
    }

    // Once we are are no longer changing topology, apply HDR fix for the final state
    {
      const auto final_topology { get_current_topology() };
      const auto current_hdr_states { get_current_hdr_states(get_device_ids_from_topology(final_topology)) };
      const auto newly_enabled_devices { get_newly_enabled_devices_from_topology(topology_we_modified, final_topology) };

      BOOST_LOG(info) << "trying to fix HDR states (if needed).";
      blank_hdr_states(current_hdr_states, newly_enabled_devices);  // Return value ignored
      set_hdr_states(current_hdr_states);  // Return value ignored
    }
  }

  bool
  settings_t::save_settings(const data_t &data) {
    if (!filepath.empty()) {
      try {
        std::ofstream file(filepath, std::ios::out | std::ios::trunc);
        nlohmann::json json_data = data;

        // Write json with indentation
        file << std::setw(4) << json_data << std::endl;
        return true;
      }
      catch (const std::exception &err) {
        BOOST_LOG(info) << "Failed to save display settings: " << err.what();
      }
    }

    return false;
  }

  void
  settings_t::load_settings() {
    try {
      if (!filepath.empty() && std::filesystem::exists(filepath)) {
        std::ifstream file(filepath);
        data = std::make_unique<data_t>(nlohmann::json::parse(file));
      }
    }
    catch (const std::exception &err) {
      BOOST_LOG(info) << "Failed to load saved display settings: " << err.what();
    }
  }

}  // namespace display_device
