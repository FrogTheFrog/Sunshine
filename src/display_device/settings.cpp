// local includes
#include "settings.h"
#include "src/main.h"

namespace display_device {

  settings_t::apply_result_t::operator bool() const {
    return result == result_e::success;
  }

  int
  settings_t::apply_result_t::get_error_code() const {
    return static_cast<int>(result);
  }

  std::string
  settings_t::apply_result_t::get_error_message() const {
    switch (result) {
      case result_e::success:
        return "Success";
      case result_e::config_parse_fail:
        return "Failed to parse configuration";
      case result_e::validation_fail:
        return "Failed to validate display settings";
      case result_e::topology_fail:
        return "Failed to change or validate the display topology";
      case result_e::primary_display_fail:
        return "Failed to change primary display";
      case result_e::modes_fail:
        return "Failed to set new display modes";
      case result_e::hdr_states_fail:
        return "Failed to set new HDR states";
      case result_e::file_save_fail:
        return "Failed to save the original settings file";
      default:
        BOOST_LOG(fatal) << "result_e conversion not implemented!";
        return "FATAL";
    }
  }

  void
  settings_t::set_filepath(std::filesystem::path filepath) {
    this->filepath = std::move(filepath);
  }

}  // namespace display_device
