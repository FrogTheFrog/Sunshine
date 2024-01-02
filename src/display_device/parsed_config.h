#pragma once

// local includes
#include "display_device.h"

// forward declarations
namespace config {
  struct video_t;
}
namespace rtsp_stream {
  struct launch_session_t;
}

namespace display_device {

  //! Config that was parsed from video config and session params, and is ready to be applied.
  struct parsed_config_t {
    enum class device_prep_e : int {
      no_operation,  //!< User has to make sure the display device is active
      ensure_active,  //!< Activate the device if needed
      ensure_primary,  //!< Activate the device if needed and make it a primary display
      ensure_only_display  //!< Deactivate other displays and turn on the specified one
    };
    static int
    device_prep_from_view(std::string_view value);

    enum class resolution_change_e : int {
      no_operation,  //!< Keep the current resolution
      automatic,  //!< Set the resolution to the one received from the client
      manual  //!< User has to specify the resolution
    };
    static int
    resolution_change_from_view(std::string_view value);

    enum class refresh_rate_change_e : int {
      no_operation,  //!< Keep the current refresh rate
      automatic,  //!< Set the refresh rate to the FPS value received from the client
      manual  //!< User has to specify the refresh rate
    };
    static int
    refresh_rate_change_from_view(std::string_view value);

    enum class hdr_prep_e : int {
      no_operation,  //!< User has to switch the HDR state manually
      automatic  //!< Switch HDR state based on session settings and if display supports it
    };
    static int
    hdr_prep_from_view(std::string_view value);

    std::string device_id;
    device_prep_e device_prep;
    boost::optional<resolution_t> resolution;
    boost::optional<refresh_rate_t> refresh_rate;
    boost::optional<bool> change_hdr_state;
  };

  /*!
   * Parses the configuration and session parameters.
   *
   * @returns config that is ready to be used or empty optional if some error has occurred.
   */
  boost::optional<parsed_config_t>
  make_parsed_config(const config::video_t &config, const rtsp_stream::launch_session_t &session);

}  // namespace display_device
