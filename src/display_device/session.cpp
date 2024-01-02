// local includes
#include "session.h"
#include "src/platform/common.h"
#include "to_string.h"

namespace display_device {

  session_t::deinit_t::~deinit_t() {
    session_t::get().restore_state();
  }

  session_t &
  session_t::get() {
    static session_t session;
    return session;
  }

  std::unique_ptr<session_t::deinit_t>
  session_t::init() {
    const auto devices { enum_available_devices() };
    if (!devices.empty()) {
      BOOST_LOG(info) << "available display devices: " << to_string(devices);
    }

    session_t::get().settings.set_filepath(platf::appdata().string() + "/original_display_settings.json");
    session_t::get().restore_state();
    return std::make_unique<deinit_t>();
  }

  settings_t::apply_result_t
  session_t::configure_display(const config::video_t &config, const rtsp_stream::launch_session_t &session) {
    return settings.apply_config(config, session);
  }

  void
  session_t::restore_state() {
    return settings.revert_settings();
  }

}  // namespace display_device
