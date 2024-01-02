#pragma once

// standard includes
#include <filesystem>
#include <memory>

// local includes
#include "parsed_config.h"

namespace display_device {

  //! A platform specific class that applies and reverts the changes to the display devices.
  class settings_t {
  public:
    //! Convenience structure for informing the user about the failure type.
    struct apply_result_t {
      enum class result_e : int {
        success = 0,
        config_parse_fail = 700,
        validation_fail,
        topology_fail,
        primary_display_fail,
        modes_fail,
        hdr_states_fail,
        file_save_fail
      };

      operator bool() const;

      int
      get_error_code() const;

      std::string
      get_error_message() const;

      result_e result;
    };

    explicit settings_t();
    virtual ~settings_t();

    //! Sets the filepath to save persistent data to.
    void
    set_filepath(std::filesystem::path filepath);

    //! Parses the provided configurations and tries to apply them.
    apply_result_t
    apply_config(const config::video_t &config, const rtsp_stream::launch_session_t &session);

    //! Tries to apply the provided configuration.
    apply_result_t
    apply_config(const parsed_config_t &config);

    //! Reverts the applied settings either from cache or persistent file.
    void
    revert_settings();

  private:
    // Platform specific data
    struct data_t;

    void
    revert_settings(const data_t &data);

    bool
    save_settings(const data_t &data);

    void
    load_settings();

    std::unique_ptr<data_t> data;
    std::filesystem::path filepath;
  };

}  // namespace display_device
