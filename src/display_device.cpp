/**
 * @file src/display_device.cpp
 * @brief Definitions for display device handling.
 */
// header include
#include "display_device.h"

// lib includes
#include <boost/algorithm/string.hpp>
#include <display_device/audio_context_interface.h>
#include <display_device/file_settings_persistence.h>
#include <display_device/json.h>
#include <display_device/retry_scheduler.h>
#include <display_device/settings_manager_interface.h>
#include <mutex>
#include <regex>

// local includes
#include "audio.h"
#include "platform/common.h"
#include "rtsp.h"

// platform-specific includes
#ifdef _WIN32
  #include <display_device/windows/settings_manager.h>
  #include <display_device/windows/win_api_layer.h>
  #include <display_device/windows/win_display_device.h>
#endif

namespace display_device {
  namespace {
    constexpr std::chrono::milliseconds DEFAULT_RETRY_INTERVAL { 5000 };

    /**
     * @brief A global for the settings manager interface and other settings whose lifetime is managed by `display_device::init(...)`.
     */
    struct {
      std::mutex mutex {};
      std::chrono::milliseconds config_revert_delay { 0 };
      std::unique_ptr<RetryScheduler<SettingsManagerInterface>> sm_instance { nullptr };
    } DD_DATA;

    /**
     * @brief Helper class for capturing audio context when the API demands it.
     *
     * The capture is needed to be done in case some of the displays are going
     * to be deactivated before the stream starts. In this case the audio context
     * will be captured for this display and can be restored once it is turned back.
     */
    class sunshine_audio_context_t: public AudioContextInterface {
    public:
      [[nodiscard]] bool
      capture() override {
        captured_context = audio_context_t {};
        return true;
      }

      [[nodiscard]] bool
      isCaptured() const override {
        return static_cast<bool>(captured_context);
      }

      void
      release() override {
        // Wait a little and hope that the audio device becomes available again.
        // Maybe this could be implemented in a better way with some retries...
        std::this_thread::sleep_for(500ms);
        captured_context = boost::none;
      }

    private:
      struct audio_context_t {
        /**
         * @brief A reference to the audio context that will automatically extend the audio session.
         * @note It is auto-initialized here for convenience.
         */
        decltype(audio::get_audio_ctx_ref()) audio_ctx_ref { audio::get_audio_ctx_ref() };
      };

      boost::optional<audio_context_t> captured_context;
    };

    /**
     * @breif Convert string to unsigned int.
     * @note For random reason there is std::stoi, but not std::stou...
     * @param value String to be converted
     * @return Parsed unsigned integer.
     */
    unsigned int
    stou(const std::string &value) {
      unsigned long result { std::stoul(value) };
      if (result > std::numeric_limits<unsigned int>::max()) {
        throw std::out_of_range("stou");
      }
      return result;
    }

    /**
     * @brief Parse resolution value from the string.
     * @param input String to be parsed.
     * @param output Reference to output variable to fill in.
     * @returns True on successful parsing (empty string allowed), false otherwise.
     *
     * @examples
     * std::optional<Resolution> resolution;
     * if (parse_resolution_string("1920x1080", resolution)) {
     *   if (resolution) {
     *     // Value was specified
     *   }
     *   else {
     *     // Value was empty
     *   }
     * }
     * @examples_end
     */
    bool
    parse_resolution_string(const std::string &input, std::optional<Resolution> &output) {
      const std::string trimmed_input { boost::algorithm::trim_copy(input) };
      const std::regex resolution_regex { R"(^(\d+)x(\d+)$)" };

      std::smatch match;
      if (std::regex_match(trimmed_input, match, resolution_regex)) {
        try {
          output = Resolution {
            stou(match[1].str()),
            stou(match[2].str())
          };
          return true;
        }
        catch (const std::out_of_range &) {
          BOOST_LOG(error) << "Failed to parse resolution string " << trimmed_input << " (number out of range).";
        }
        catch (const std::exception &err) {
          BOOST_LOG(error) << "Failed to parse resolution string " << trimmed_input << ":\n"
                           << err.what();
        }
      }
      else {
        if (trimmed_input.empty()) {
          output = std::nullopt;
          return true;
        }

        BOOST_LOG(error) << "Failed to parse resolution string " << trimmed_input << ". It must match a \"1920x1080\" pattern!";
      }

      return false;
    }

    /**
     * @brief Parse refresh rate value from the string.
     * @param input String to be parsed.
     * @param output Reference to output variable to fill in.
     * @returns True on successful parsing (empty string allowed), false otherwise.
     *
     * @examples
     * std::optional<FloatingPoint> refresh_rate;
     * if (parse_refresh_rate_string("59.95", refresh_rate)) {
     *   if (refresh_rate) {
     *     // Value was specified
     *   }
     *   else {
     *     // Value was empty
     *   }
     * }
     * @examples_end
     */
    bool
    parse_refresh_rate_string(const std::string &input, std::optional<FloatingPoint> &output) {
      static const auto is_zero { [](const auto &character) { return character == '0'; } };
      const std::string trimmed_input { boost::algorithm::trim_copy(input) };
      const std::regex refresh_rate_regex { R"(^(\d+)(?:\.(\d+))?$)" };

      std::smatch match;
      if (std::regex_match(trimmed_input, match, refresh_rate_regex)) {
        try {
          // Here we are trimming zeros from the string to possibly reduce out of bounds case
          std::string trimmed_match_1 { boost::algorithm::trim_left_copy_if(match[1].str(), is_zero) };
          if (trimmed_match_1.empty()) {
            trimmed_match_1 = "0"s;  // Just in case ALL of the string is full of zeros, we want to leave one
          }

          std::string trimmed_match_2;
          if (match[2].matched) {
            trimmed_match_2 = boost::algorithm::trim_right_copy_if(match[2].str(), is_zero);
          }

          if (!trimmed_match_2.empty()) {
            // We have a decimal point and will have to split it into numerator and denominator.
            // For example:
            //   59.995:
            //     numerator = 59995
            //     denominator = 1000

            // We are essentially removing the decimal point here: 59.995 -> 59995
            const std::string numerator_str { trimmed_match_1 + trimmed_match_2 };
            const auto numerator { stou(numerator_str) };

            // Here we are counting decimal places and calculating denominator: 10^decimal_places
            const auto denominator { static_cast<unsigned int>(std::pow(10, trimmed_match_2.size())) };

            output = Rational { numerator, denominator };
          }
          else {
            // We do not have a decimal point, just a valid number.
            // For example:
            //   60:
            //     numerator = 60
            //     denominator = 1
            output = Rational { stou(trimmed_match_1), 1 };
          }
          return true;
        }
        catch (const std::out_of_range &) {
          BOOST_LOG(error) << "Failed to parse refresh rate string " << trimmed_input << " (number out of range).";
        }
        catch (const std::exception &err) {
          BOOST_LOG(error) << "Failed to parse refresh rate string " << trimmed_input << ":\n"
                           << err.what();
        }
      }
      else {
        if (trimmed_input.empty()) {
          output = std::nullopt;
          return true;
        }

        BOOST_LOG(error) << "Failed to parse refresh rate string " << trimmed_input << ". Must have a pattern of \"123\" or \"123.456\"!";
      }

      return false;
    }

    /**
     * @brief Parse device preparation option from the user configuration and the session information.
     * @param video_config User's video related configuration.
     * @returns Parsed device preparation value we need to use.
     *          Empty optional if no preparation nor configuration shall take place.
     *
     * @examples
     * const config::video_t &video_config { config::video };
     * const auto device_prep_option = parse_device_prep_option(video_config);
     * @examples_end
     */
    std::optional<SingleDisplayConfiguration::DevicePreparation>
    parse_device_prep_option(const config::video_t &video_config) {
      using config_option_e = config::video_t::dd_t::config_option_e;
      using device_prep_e = SingleDisplayConfiguration::DevicePreparation;

      switch (video_config.dd.configuration_option) {
        case config_option_e::verify_only:
          return device_prep_e::VerifyOnly;
        case config_option_e::ensure_active:
          return device_prep_e::EnsureActive;
        case config_option_e::ensure_primary:
          return device_prep_e::EnsurePrimary;
        case config_option_e::ensure_only_display:
          return device_prep_e::EnsureOnlyDisplay;
        case config_option_e::disabled:
          break;
      }

      return std::nullopt;
    }

    /**
     * @brief Parse resolution option from the user configuration and the session information.
     * @param video_config User's video related configuration.
     * @param session Session information.
     * @param config A reference to a display config object that will be modified on success.
     * @returns True on successful parsing, false otherwise.
     *
     * @examples
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * SingleDisplayConfiguration config;
     * const bool success = parse_resolution_option(video_config, *launch_session, config);
     * @examples_end
     */
    bool
    parse_resolution_option(const config::video_t &video_config, const rtsp_stream::launch_session_t &session, SingleDisplayConfiguration &config) {
      using resolution_option_e = config::video_t::dd_t::resolution_option_e;

      switch (video_config.dd.resolution_option) {
        case resolution_option_e::automatic: {
          if (!session.enable_sops) {
            BOOST_LOG(warning) << "Sunshine is configured to change resolution automatically, but the \"Optimize game settings\" is not set in the client! Resolution will not be changed.";
          }
          else if (session.width >= 0 && session.height >= 0) {
            config.m_resolution = Resolution {
              static_cast<unsigned int>(session.width),
              static_cast<unsigned int>(session.height)
            };
          }
          else {
            BOOST_LOG(error) << "Resolution provided by client session config is invalid: " << session.width << "x" << session.height;
            return false;
          }
          break;
        }
        case resolution_option_e::manual: {
          if (!session.enable_sops) {
            BOOST_LOG(warning) << "Sunshine is configured to change resolution manually, but the \"Optimize game settings\" is not set in the client! Resolution will not be changed.";
          }
          else {
            if (!parse_resolution_string(video_config.dd.manual_resolution, config.m_resolution)) {
              BOOST_LOG(error) << "Failed to parse manual resolution string!";
              return false;
            }

            if (!config.m_resolution) {
              BOOST_LOG(error) << "Manual resolution must be specified!";
              return false;
            }
          }
          break;
        }
        case resolution_option_e::disabled:
          break;
      }

      return true;
    }

    /**
     * @brief Parse refresh rate option from the user configuration and the session information.
     * @param video_config User's video related configuration.
     * @param session Session information.
     * @param config A reference to a config object that will be modified on success.
     * @returns True on successful parsing, false otherwise.
     *
     * @examples
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * SingleDisplayConfiguration config;
     * const bool success = parse_refresh_rate_option(video_config, *launch_session, config);
     * @examples_end
     */
    bool
    parse_refresh_rate_option(const config::video_t &video_config, const rtsp_stream::launch_session_t &session, SingleDisplayConfiguration &config) {
      using refresh_rate_option_e = config::video_t::dd_t::refresh_rate_option_e;

      switch (video_config.dd.refresh_rate_option) {
        case refresh_rate_option_e::automatic: {
          if (session.fps >= 0) {
            config.m_refresh_rate = Rational { static_cast<unsigned int>(session.fps), 1 };
          }
          else {
            BOOST_LOG(error) << "FPS value provided by client session config is invalid: " << session.fps;
            return false;
          }
          break;
        }
        case refresh_rate_option_e::manual: {
          if (!parse_refresh_rate_string(video_config.dd.manual_refresh_rate, config.m_refresh_rate)) {
            BOOST_LOG(error) << "Failed to parse manual refresh rate string!";
            return false;
          }

          if (!config.m_refresh_rate) {
            BOOST_LOG(error) << "Manual refresh rate must be specified!";
            return false;
          }
          break;
        }
        case refresh_rate_option_e::disabled:
          break;
      }

      return true;
    }

    /**
     * @brief Parse HDR option from the user configuration and the session information.
     * @param video_config User's video related configuration.
     * @param session Session information.
     * @returns Parsed HDR state value we need to switch to.
     *          Empty optional if no action is required.
     *
     * @examples
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     * const auto hdr_option = parse_hdr_option(video_config, *launch_session);
     * @examples_end
     */
    std::optional<HdrState>
    parse_hdr_option(const config::video_t &video_config, const rtsp_stream::launch_session_t &session) {
      using hdr_option_e = config::video_t::dd_t::hdr_option_e;

      switch (video_config.dd.hdr_option) {
        case hdr_option_e::automatic:
          return session.enable_hdr ? HdrState::Enabled : HdrState::Disabled;
        case hdr_option_e::disabled:
          break;
      }

      return std::nullopt;
    }

    /**
     * @brief Construct a settings manager interface to manage display device settings.
     * @param persistence_filepath File location for saving persistent state.
     * @param video_config User's video related configuration.
     * @return An interface or nullptr if the OS does not support the interface.
     */
    std::unique_ptr<SettingsManagerInterface>
    make_settings_manager(const std::filesystem::path &persistence_filepath, const config::video_t &video_config) {
#ifdef _WIN32
      return std::make_unique<SettingsManager>(
        std::make_shared<WinDisplayDevice>(std::make_shared<WinApiLayer>()),
        std::make_shared<sunshine_audio_context_t>(),
        std::make_unique<PersistentState>(
          std::make_shared<FileSettingsPersistence>(persistence_filepath)),
        WinWorkarounds {
          .m_hdr_blank_delay = video_config.dd.wa.hdr_toggle ? std::make_optional(500ms) : std::nullopt });
#else
      return nullptr;
#endif
    }

    /**
     * @brief Defines the "revert config" algorithms.
     */
    enum class revert_option_e {
      try_once,  ///< Try reverting once and then abort.
      try_indefinitely,  ///< Keep trying to revert indefinitely.
      try_indefinitely_with_delay  ///< Keep trying to revert indefinitely, but delay the first try by some amount of time.
    };

    /**
     * @brief Reverts the configuration based on the provided option.
     * @note This is function does not lock mutex.
     */
    void
    revert_configuration_unlocked(const revert_option_e option) {
      if (!DD_DATA.sm_instance) {
        // Platform is not supported, nothing to do.
        return;
      }

      // Note: by default the executor function is immediately executed in the calling thread. With delay, we want to avoid that.
      SchedulerOptions scheduler_option { .m_sleep_durations = { DEFAULT_RETRY_INTERVAL } };
      if (option == revert_option_e::try_indefinitely_with_delay && DD_DATA.config_revert_delay > std::chrono::milliseconds::zero()) {
        scheduler_option.m_sleep_durations = { DD_DATA.config_revert_delay, DEFAULT_RETRY_INTERVAL };
        scheduler_option.m_execution = SchedulerOptions::Execution::ScheduledOnly;
      }

      DD_DATA.sm_instance->schedule([try_once = (option == revert_option_e::try_once)](auto &settings_iface, auto &stop_token) {
        // Here we want to keep retrying indefinitely until we succeed.
        if (settings_iface.revertSettings() || try_once) {
          stop_token.requestStop();
        }
      },
        scheduler_option);
    }
  }  // namespace

  std::unique_ptr<platf::deinit_t>
  init(const std::filesystem::path &persistence_filepath, const config::video_t &video_config) {
    std::lock_guard lock { DD_DATA.mutex };
    // We can support re-init without any issues, however we should make sure to clean up first!
    revert_configuration_unlocked(revert_option_e::try_once);
    DD_DATA.config_revert_delay = video_config.dd.config_revert_delay;
    DD_DATA.sm_instance = nullptr;

    // If we fail to create settings manager, this means platform is not supported, and
    // we will need to provided error-free pass-trough in other methods
    if (auto settings_manager { make_settings_manager(persistence_filepath, video_config) }) {
      DD_DATA.sm_instance = std::make_unique<RetryScheduler<SettingsManagerInterface>>(std::move(settings_manager));

      const auto available_devices { DD_DATA.sm_instance->execute([](auto &settings_iface) { return settings_iface.enumAvailableDevices(); }) };
      BOOST_LOG(info) << "Currently available display devices:\n"
                      << toJson(available_devices);

      // In case we have failed to revert configuration before shutting down, we should
      // do it now.
      revert_configuration_unlocked(revert_option_e::try_indefinitely);
    }

    class deinit_t: public platf::deinit_t {
    public:
      ~deinit_t() override {
        std::lock_guard lock { DD_DATA.mutex };
        revert_configuration_unlocked(revert_option_e::try_once);
        DD_DATA.sm_instance = nullptr;
      }
    };
    return std::make_unique<deinit_t>();
  }

  std::string
  map_output_name(const std::string &output_name) {
    std::lock_guard lock { DD_DATA.mutex };
    if (!DD_DATA.sm_instance) {
      // Fallback to giving back the output name if the platform is not supported.
      return output_name;
    }

    return DD_DATA.sm_instance->execute([&output_name](auto &settings_iface) { return settings_iface.getDisplayName(output_name); });
  }

  void
  configure_display(const config::video_t &video_config, const rtsp_stream::launch_session_t &session) {
    const auto result { parse_configuration(video_config, session) };
    if (const auto *parsed_config { std::get_if<SingleDisplayConfiguration>(&result) }; parsed_config) {
      configure_display(*parsed_config);
      return;
    }

    if (const auto *disabled { std::get_if<configuration_disabled_tag_t>(&result) }; disabled) {
      revert_configuration();
      return;
    }

    // Error already logged for failed_to_parse_tag_t case, and we also don't
    // want to revert active configuration in case we have any
  }

  void
  configure_display(const SingleDisplayConfiguration &config) {
    std::lock_guard lock { DD_DATA.mutex };
    if (!DD_DATA.sm_instance) {
      // Platform is not supported, nothing to do.
      return;
    }

    DD_DATA.sm_instance->schedule([config](auto &settings_iface, auto &stop_token) {
      // We only want to keep retrying in case of a transient errors.
      // In other cases, when we either fail or succeed we just want to stop...
      if (settings_iface.applySettings(config) != SettingsManagerInterface::ApplyResult::ApiTemporarilyUnavailable) {
        stop_token.requestStop();
      }
    },
      { .m_sleep_durations = { DEFAULT_RETRY_INTERVAL } });
  }

  void
  revert_configuration() {
    std::lock_guard lock { DD_DATA.mutex };
    revert_configuration_unlocked(revert_option_e::try_indefinitely_with_delay);
  }

  bool
  reset_persistence() {
    std::lock_guard lock { DD_DATA.mutex };
    if (!DD_DATA.sm_instance) {
      // Platform is not supported, assume success.
      return true;
    }

    return DD_DATA.sm_instance->execute([](auto &settings_iface, auto &stop_token) {
      // Whatever the outcome is we want to stop interfering with the used,
      // so any schedulers need to be stopped.
      stop_token.requestStop();
      return settings_iface.resetPersistence();
    });
  }

  std::variant<failed_to_parse_tag_t, configuration_disabled_tag_t, SingleDisplayConfiguration>
  parse_configuration(const config::video_t &video_config, const rtsp_stream::launch_session_t &session) {
    const auto device_prep { parse_device_prep_option(video_config) };
    if (!device_prep) {
      return configuration_disabled_tag_t {};
    }

    SingleDisplayConfiguration config;
    config.m_device_id = video_config.output_name;
    config.m_device_prep = *device_prep;
    config.m_hdr_state = parse_hdr_option(video_config, session);

    if (!parse_resolution_option(video_config, session, config)) {
      // Error already logged
      return failed_to_parse_tag_t {};
    }

    if (!parse_refresh_rate_option(video_config, session, config)) {
      // Error already logged
      return failed_to_parse_tag_t {};
    }

    return config;
  }
}  // namespace display_device