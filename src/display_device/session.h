#pragma once

// local includes
#include "settings.h"

namespace display_device {

  //! A singleton for managing the display state for the current Sunshine session
  class session_t {
  public:
    class deinit_t {
    public:
      virtual ~deinit_t();
    };

    /*!
     * Gets the current session instance.
     */
    static session_t &
    get();

    /*!
     * Initializes the session which will perform recovery and cleanup in case we crashed
     * or did an unexpected shutdown.
     */
    static std::unique_ptr<deinit_t>
    init();

    /*!
     * Prepares the display device based on the session and the configuration.
     *
     * @returns result structure indicating whether we can continue with the streaming session creation or not.
     */
    settings_t::apply_result_t
    configure_display(const config::video_t &config, const rtsp_stream::launch_session_t &session);

    /*!
     * Try to restore the previous display state.
     *
     * @note Not everything can be restored if the display was unplugged, etc.
     */
    void
    restore_state();

  private:
    explicit session_t() = default;
    settings_t settings;
  };

}  // namespace display_device
