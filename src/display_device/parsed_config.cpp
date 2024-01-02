// lib includes
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <cmath>

// local includes
#include "parsed_config.h"
#include "src/config.h"
#include "src/main.h"
#include "src/rtsp.h"

namespace display_device {

  namespace {
    bool
    parse_resolution_option(const config::video_t &config, const rtsp_stream::launch_session_t &session, parsed_config_t &parsed_config) {
      const auto resolution_option { static_cast<parsed_config_t::resolution_change_e>(config.resolution_change) };
      switch (resolution_option) {
        case parsed_config_t::resolution_change_e::automatic: {
          if (!session.enable_sops) {
            //  "Optimize game settings" must be enabled on the client side
            parsed_config.resolution = boost::none;
          }
          else if (session.width >= 0 && session.height >= 0) {
            parsed_config.resolution = resolution_t {
              static_cast<unsigned int>(session.width),
              static_cast<unsigned int>(session.height)
            };
          }
          else {
            BOOST_LOG(error) << "resolution provided by client session config is invalid: " << session.width << "x" << session.height;
            return false;
          }
          break;
        }
        case parsed_config_t::resolution_change_e::manual: {
          const std::string trimmed_string { boost::algorithm::trim_copy(config.manual_resolution) };
          const boost::regex resolution_regex { R"(^(\d+)x(\d+)$)" };  // std::regex hangs in CTOR for some reason when called in a thread...

          boost::smatch match;
          if (boost::regex_match(trimmed_string, match, resolution_regex)) {
            try {
              parsed_config.resolution = resolution_t {
                static_cast<unsigned int>(std::stol(match[1])),
                static_cast<unsigned int>(std::stol(match[2]))
              };
            }
            catch (const std::invalid_argument &err) {
              BOOST_LOG(error) << "failed to parse manual resolution string (invalid argument):\n"
                               << err.what();
              return false;
            }
            catch (const std::out_of_range &err) {
              BOOST_LOG(error) << "failed to parse manual resolution string (number out of range):\n"
                               << err.what();
              return false;
            }
            catch (const std::exception &err) {
              BOOST_LOG(error) << "failed to parse manual resolution string:\n"
                               << err.what();
              return false;
            }
          }
          else {
            BOOST_LOG(error) << "failed to parse manual resolution string. It must match a \"WIDTHxHEIGHT\" pattern!";
            return false;
          }
          break;
        }
        case parsed_config_t::resolution_change_e::no_operation:
        default:
          break;
      }

      return true;
    }

    bool
    parse_refresh_rate_option(const config::video_t &config, const rtsp_stream::launch_session_t &session, parsed_config_t &parsed_config) {
      const auto refresh_rate_option { static_cast<parsed_config_t::refresh_rate_change_e>(config.refresh_rate_change) };
      switch (refresh_rate_option) {
        case parsed_config_t::refresh_rate_change_e::automatic: {
          if (session.fps >= 0) {
            parsed_config.refresh_rate = refresh_rate_t { static_cast<unsigned int>(session.fps), 1 };
          }
          else {
            BOOST_LOG(error) << "FPS value provided by client session config is invalid: " << session.fps;
            return false;
          }
          break;
        }
        case parsed_config_t::refresh_rate_change_e::manual: {
          const std::string trimmed_string { boost::algorithm::trim_copy(config.manual_refresh_rate) };
          const boost::regex resolution_regex { R"(^(\d+)(?:\.(\d+))?$)" };  // std::regex hangs in CTOR for some reason when called in a thread...

          boost::smatch match;
          if (boost::regex_match(trimmed_string, match, resolution_regex)) {
            try {
              if (match[2].matched) {
                // We have a decimal point and will have to split it into numerator and denominator.
                // For example:
                //   59.995:
                //     numerator = 59995
                //     denominator = 1000

                const std::string numerator_str { match[1].str() + match[2].str() };
                const auto numerator { static_cast<unsigned int>(std::stol(numerator_str)) };
                const auto denominator { static_cast<unsigned int>(std::pow(10, std::distance(match[2].first, match[2].second))) };

                parsed_config.refresh_rate = refresh_rate_t { numerator, denominator };
              }
              else {
                parsed_config.refresh_rate = refresh_rate_t { static_cast<unsigned int>(std::stol(match[1])), 1 };
              }
            }
            catch (const std::invalid_argument &err) {
              BOOST_LOG(error) << "failed to parse manual refresh rate string (invalid argument):\n"
                               << err.what();
              return false;
            }
            catch (const std::out_of_range &err) {
              BOOST_LOG(error) << "failed to parse manual refresh rate string (number out of range):\n"
                               << err.what();
              return false;
            }
            catch (const std::exception &err) {
              BOOST_LOG(error) << "failed to parse manual refresh rate string:\n"
                               << err.what();
              return false;
            }
          }
          else {
            BOOST_LOG(error) << "failed to parse manual refresh rate string! Must have a pattern of \"123\" or \"123.456\"!";
            return false;
          }
          break;
        }
        case parsed_config_t::refresh_rate_change_e::no_operation:
        default:
          break;
      }

      return true;
    }

    boost::optional<bool>
    parse_hdr_option(const config::video_t &config, const rtsp_stream::launch_session_t &session) {
      const auto hdr_prep_option { static_cast<parsed_config_t::hdr_prep_e>(config.hdr_prep) };
      switch (hdr_prep_option) {
        case parsed_config_t::hdr_prep_e::automatic:
          return session.enable_hdr;
        case parsed_config_t::hdr_prep_e::no_operation:
        default:
          return boost::none;
      }
    }
  }  // namespace

  int
  parsed_config_t::device_prep_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::device_prep_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(ensure_active);
    _CONVERT_(ensure_primary);
    _CONVERT_(ensure_only_display);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::device_prep_e::no_operation);
  }

  int
  parsed_config_t::resolution_change_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::resolution_change_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(automatic);
    _CONVERT_(manual);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::resolution_change_e::no_operation);
  }

  int
  parsed_config_t::refresh_rate_change_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::refresh_rate_change_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(automatic);
    _CONVERT_(manual);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::refresh_rate_change_e::no_operation);
  }

  int
  parsed_config_t::hdr_prep_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::hdr_prep_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(automatic);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::hdr_prep_e::no_operation);
  }

  boost::optional<parsed_config_t>
  make_parsed_config(const config::video_t &config, const rtsp_stream::launch_session_t &session) {
    parsed_config_t parsed_config;
    parsed_config.device_id = config.output_name;
    parsed_config.device_prep = static_cast<parsed_config_t::device_prep_e>(config.display_device_prep);
    parsed_config.change_hdr_state = parse_hdr_option(config, session);

    if (!parse_resolution_option(config, session, parsed_config)) {
      // Error already logged
      return boost::none;
    }

    if (!parse_refresh_rate_option(config, session, parsed_config)) {
      // Error already logged
      return boost::none;
    }

    return parsed_config;
  }

}  // namespace display_device
