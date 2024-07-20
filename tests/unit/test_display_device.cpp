/**
 * @file tests/unit/test_display_device.cpp
 * @brief Test src/display_device.*.
 */
#include <src/config.h>
#include <src/display_device.h>
#include <src/rtsp.h>
#include <tests/conftest.cpp>

namespace {
  using config_option_e = config::video_t::dd_t::config_option_e;
  using DevicePreparation = display_device::SingleDisplayConfiguration::DevicePreparation;

  using hdr_option_e = config::video_t::dd_t::hdr_option_e;
  using HdrState = display_device::HdrState;

  using resolution_option_e = config::video_t::dd_t::resolution_option_e;
  using Resolution = display_device::Resolution;

  using refresh_rate_option_e = config::video_t::dd_t::refresh_rate_option_e;
  using Rational = display_device::Rational;

  constexpr unsigned int max_uint { std::numeric_limits<unsigned int>::max() };
  const std::string max_uint_string { std::to_string(std::numeric_limits<unsigned int>::max()) };

  template <class T>
  class DisplayDeviceConfigTest: public virtual BaseTest, public ::testing::WithParamInterface<T> {
  };
}  // namespace

namespace {
  using ParseDeviceId = DisplayDeviceConfigTest<std::pair<std::string, std::string>>;
  INSTANTIATE_TEST_SUITE_P(
    DisplayDeviceTest,
    ParseDeviceId,
    ::testing::Values(
      std::make_pair(""s, ""s),
      std::make_pair("SomeId"s, "SomeId"s),
      std::make_pair("{daeac860-f4db-5208-b1f5-cf59444fb768}"s, "{daeac860-f4db-5208-b1f5-cf59444fb768}"s)));
  TEST_P(ParseDeviceId, BasicTest) {
    const auto &[input_value, expected_value] = GetParam();

    config::video_t video_config {};
    video_config.dd.configuration_option = config_option_e::verify_only;
    video_config.output_name = input_value;

    const auto result { display_device::parse_configuration(video_config, {}) };
    EXPECT_EQ(std::get<display_device::SingleDisplayConfiguration>(result).m_device_id, expected_value);
  }
}  // namespace

namespace {
  using ParseConfigOption = DisplayDeviceConfigTest<std::pair<config_option_e, std::optional<DevicePreparation>>>;
  INSTANTIATE_TEST_SUITE_P(
    DisplayDeviceTest,
    ParseConfigOption,
    ::testing::Values(
      std::make_pair(config_option_e::disabled, std::nullopt),
      std::make_pair(config_option_e::verify_only, DevicePreparation::VerifyOnly),
      std::make_pair(config_option_e::ensure_active, DevicePreparation::EnsureActive),
      std::make_pair(config_option_e::ensure_primary, DevicePreparation::EnsurePrimary),
      std::make_pair(config_option_e::ensure_only_display, DevicePreparation::EnsureOnlyDisplay)));
  TEST_P(ParseConfigOption, BasicTest) {
    const auto &[input_value, expected_value] = GetParam();

    config::video_t video_config {};
    video_config.dd.configuration_option = input_value;

    const auto result { display_device::parse_configuration(video_config, {}) };
    if (const auto *parsed_config { std::get_if<display_device::SingleDisplayConfiguration>(&result) }; parsed_config) {
      ASSERT_EQ(parsed_config->m_device_prep, expected_value);
    }
    else {
      ASSERT_EQ(std::get_if<display_device::configuration_disabled_tag_t>(&result) != nullptr, !expected_value);
    }
  }
}  // namespace

namespace {
  using client_wants_hdr = bool;
  using ParseHdrOption = DisplayDeviceConfigTest<std::pair<std::pair<hdr_option_e, client_wants_hdr>, std::optional<HdrState>>>;
  INSTANTIATE_TEST_SUITE_P(
    DisplayDeviceTest,
    ParseHdrOption,
    ::testing::Values(
      std::make_pair(std::make_pair(hdr_option_e::disabled, client_wants_hdr { true }), std::nullopt),
      std::make_pair(std::make_pair(hdr_option_e::disabled, client_wants_hdr { false }), std::nullopt),
      std::make_pair(std::make_pair(hdr_option_e::automatic, client_wants_hdr { true }), HdrState::Enabled),
      std::make_pair(std::make_pair(hdr_option_e::automatic, client_wants_hdr { false }), HdrState::Disabled)));
  TEST_P(ParseHdrOption, BasicTest) {
    const auto &[input_value, expected_value] = GetParam();
    const auto &[input_hdr_option, input_enable_hdr] = input_value;

    config::video_t video_config {};
    video_config.dd.configuration_option = config_option_e::verify_only;
    video_config.dd.hdr_option = input_hdr_option;

    rtsp_stream::launch_session_t session {};
    session.enable_hdr = input_enable_hdr;

    const auto result { display_device::parse_configuration(video_config, session) };
    EXPECT_EQ(std::get<display_device::SingleDisplayConfiguration>(result).m_hdr_state, expected_value);
  }
}  // namespace

namespace {
  struct failed_to_parse_resolution_t {};
  struct no_resolution_t {};

  using sops_enabled = bool;
  struct client_resolution_t {
    int width;
    int height;
  };

  using ParseResolutionOption = DisplayDeviceConfigTest<std::pair<std::tuple<resolution_option_e, sops_enabled, std::variant<client_resolution_t, std::string>>,
    std::variant<failed_to_parse_resolution_t, no_resolution_t, Resolution>>>;
  INSTANTIATE_TEST_SUITE_P(
    DisplayDeviceTest,
    ParseResolutionOption,
    ::testing::Values(
      //---- Disabled cases ----
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { true }, client_resolution_t { 1920, 1080 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { true }, "1920x1080"s), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { true }, client_resolution_t { -1, -1 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { true }, "invalid_res"s), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { false }, client_resolution_t { 1920, 1080 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { false }, "1920x1080"s), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { false }, client_resolution_t { -1, -1 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::disabled, sops_enabled { false }, "invalid_res"s), no_resolution_t {}),
      //---- Automatic cases ----
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { true }, client_resolution_t { 1920, 1080 }), Resolution { 1920, 1080 }),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { true }, "1920x1080"s), Resolution {}),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { true }, client_resolution_t { -1, -1 }), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { true }, "invalid_res"s), Resolution {}),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { false }, client_resolution_t { 1920, 1080 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { false }, "1920x1080"s), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { false }, client_resolution_t { -1, -1 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { false }, "invalid_res"s), no_resolution_t {}),
      //---- Manual cases ----
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, client_resolution_t { 1920, 1080 }), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "1920x1080"s), Resolution { 1920, 1080 }),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, client_resolution_t { -1, -1 }), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "invalid_res"s), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { false }, client_resolution_t { 1920, 1080 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { false }, "1920x1080"s), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { false }, client_resolution_t { -1, -1 }), no_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { false }, "invalid_res"s), no_resolution_t {}),
      //---- Both negative values from client are checked ----
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { true }, client_resolution_t { 0, 0 }), Resolution { 0, 0 }),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { true }, client_resolution_t { -1, 0 }), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::automatic, sops_enabled { true }, client_resolution_t { 0, -1 }), failed_to_parse_resolution_t {}),
      //---- Resolution string format validation ----
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "0x0"s), Resolution { 0, 0 }),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "0x"s), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "x0"s), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "-1x1"s), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "1x-1"s), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "x0x0"s), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, "0x0x"s), failed_to_parse_resolution_t {}),
      //---- String number is out of bounds ----
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, max_uint_string + "x"s + max_uint_string), Resolution { max_uint, max_uint }),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, max_uint_string + "0"s + "x"s + max_uint_string), failed_to_parse_resolution_t {}),
      std::make_pair(std::make_tuple(resolution_option_e::manual, sops_enabled { true }, max_uint_string + "x"s + max_uint_string + "0"s), failed_to_parse_resolution_t {})));
  TEST_P(ParseResolutionOption, BasicTest) {
    const auto &[input_value, expected_value] = GetParam();
    const auto &[input_resolution_option, input_enable_sops, input_resolution] = input_value;

    config::video_t video_config {};
    video_config.dd.configuration_option = config_option_e::verify_only;
    video_config.dd.resolution_option = input_resolution_option;

    rtsp_stream::launch_session_t session {};
    session.enable_sops = input_enable_sops;

    if (const auto *client_res { std::get_if<client_resolution_t>(&input_resolution) }; client_res) {
      video_config.dd.manual_resolution = {};
      session.width = client_res->width;
      session.height = client_res->height;
    }
    else {
      video_config.dd.manual_resolution = std::get<std::string>(input_resolution);
      session.width = {};
      session.height = {};
    }

    const auto result { display_device::parse_configuration(video_config, session) };
    if (const auto *failed_option { std::get_if<failed_to_parse_resolution_t>(&expected_value) }; failed_option) {
      EXPECT_NO_THROW(std::get<display_device::failed_to_parse_tag_t>(result));
    }
    else {
      std::optional<Resolution> expected_resolution;
      if (const auto *valid_resolution_option { std::get_if<Resolution>(&expected_value) }; valid_resolution_option) {
        expected_resolution = *valid_resolution_option;
      }

      EXPECT_EQ(std::get<display_device::SingleDisplayConfiguration>(result).m_resolution, expected_resolution);
    }
  }
}  // namespace

namespace {
  struct failed_to_parse_refresh_rate_t {};
  struct no_refresh_rate_t {};

  using client_fps = int;
  using ParseRefreshRateOption = DisplayDeviceConfigTest<std::pair<std::tuple<refresh_rate_option_e, std::variant<client_fps, std::string>>,
    std::variant<failed_to_parse_refresh_rate_t, no_refresh_rate_t, Rational>>>;
  INSTANTIATE_TEST_SUITE_P(
    DisplayDeviceTest,
    ParseRefreshRateOption,
    ::testing::Values(
      //---- Disabled cases ----
      std::make_pair(std::make_tuple(refresh_rate_option_e::disabled, client_fps { 60 }), no_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::disabled, "60"s), no_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::disabled, "59.9885"s), no_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::disabled, client_fps { -1 }), no_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::disabled, "invalid_refresh_rate"s), no_refresh_rate_t {}),
      //---- Automatic cases ----
      std::make_pair(std::make_tuple(refresh_rate_option_e::automatic, client_fps { 60 }), Rational { 60, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::automatic, "60"s), Rational { 0, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::automatic, "59.9885"s), Rational { 0, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::automatic, client_fps { -1 }), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::automatic, "invalid_refresh_rate"s), Rational { 0, 1 }),
      //---- Manual cases ----
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, client_fps { 60 }), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "60"s), Rational { 60, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "59.9885"s), Rational { 599885, 10000 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, client_fps { -1 }), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "invalid_refresh_rate"s), failed_to_parse_refresh_rate_t {}),
      //---- Refresh rate string format validation ----
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "0000000000000"s), Rational { 0, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "0"s), Rational { 0, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "00000000.0000000"s), Rational { 0, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "0.0"s), Rational { 0, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "000000000000010"s), Rational { 10, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "00000010.0000000"s), Rational { 10, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "00000010.1000000"s), Rational { 101, 10 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "00000010.0100000"s), Rational { 1001, 100 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "00000000.1000000"s), Rational { 1, 10 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "60,0"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "-60.0"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "60.-0"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "a60.0"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "60.0b"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "a60"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "60b"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, "-60"s), failed_to_parse_refresh_rate_t {}),
      //---- String number is out of bounds ----
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, max_uint_string), Rational { max_uint, 1 }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, max_uint_string + "0"s), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, max_uint_string.substr(0, 1) + "."s + max_uint_string.substr(1)), Rational { max_uint, static_cast<unsigned int>(std::pow(10, max_uint_string.size() - 1)) }),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, max_uint_string.substr(0, 1) + "0"s + "."s + max_uint_string.substr(1)), failed_to_parse_refresh_rate_t {}),
      std::make_pair(std::make_tuple(refresh_rate_option_e::manual, max_uint_string.substr(0, 1) + "."s + "0"s + max_uint_string.substr(1)), failed_to_parse_refresh_rate_t {})));
  TEST_P(ParseRefreshRateOption, BasicTest) {
    const auto &[input_value, expected_value] = GetParam();
    const auto &[input_refresh_rate_option, input_refresh_rate] = input_value;

    config::video_t video_config {};
    video_config.dd.configuration_option = config_option_e::verify_only;
    video_config.dd.refresh_rate_option = input_refresh_rate_option;

    rtsp_stream::launch_session_t session {};
    if (const auto *client_refresh_rate { std::get_if<client_fps>(&input_refresh_rate) }; client_refresh_rate) {
      video_config.dd.manual_refresh_rate = {};
      session.fps = *client_refresh_rate;
    }
    else {
      video_config.dd.manual_refresh_rate = std::get<std::string>(input_refresh_rate);
      session.fps = {};
    }

    const auto result { display_device::parse_configuration(video_config, session) };
    if (const auto *failed_option { std::get_if<failed_to_parse_refresh_rate_t>(&expected_value) }; failed_option) {
      EXPECT_NO_THROW(std::get<display_device::failed_to_parse_tag_t>(result));
    }
    else {
      std::optional<display_device::FloatingPoint> expected_refresh_rate;
      if (const auto *valid_refresh_rate_option { std::get_if<Rational>(&expected_value) }; valid_refresh_rate_option) {
        expected_refresh_rate = *valid_refresh_rate_option;
      }

      EXPECT_EQ(std::get<display_device::SingleDisplayConfiguration>(result).m_refresh_rate, expected_refresh_rate);
    }
  }
}  // namespace
