/*******************************************************************************
  Offline tests for the Astrospheric weather driver.

  Feeds saved/synthetic JSON shaped like the Astrospheric v2 GetForecastData
  response through the driver's parser and its INDI weather safety logic.
  No network access and no API key are used: the environment variable is
  cleared and the INDI config path points at an empty temp location so a real
  user config can never be loaded.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.
*******************************************************************************/

#include <gtest/gtest.h>

#include "astrospheric_forecast.h"
#include "indi_astrospheric_weather.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#ifndef ASTROSPHERIC_FIXTURE_DIR
#error "ASTROSPHERIC_FIXTURE_DIR must point at test/weather/data"
#endif

namespace
{

// 2026-11-03T00:00:00Z
constexpr time_t kStart = 1793664000;

std::string readFixture(const std::string &name)
{
    std::ifstream in(std::string(ASTROSPHERIC_FIXTURE_DIR) + "/" + name);
    EXPECT_TRUE(in.good()) << "missing fixture " << name;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// One hourly entry in v2 shape, with every value overridable.
std::string hour(const std::string &t, const std::string &cloud = "12.0", const std::string &wind = "5.0")
{
    auto v = [](const std::string &x) { return "{\"ActualValue\":" + x + "}"; };
    return "{\"UTCForecastHour\":\"" + t + "\","
           "\"Cloud\":" + v(cloud) + ",\"Temperature\":" + v("283.15") + ",\"Wind\":" + v(wind) + ","
           "\"DewPoint\":" + v("273.15") + ",\"WindDirection\":" + v("270") + ","
           "\"Seeing\":" + v("3.5") + ",\"Transparency\":" + v("10") + "}";
}

// Keep the driver away from any real key or config on the build machine.
class IsolatedEnvironment : public ::testing::Environment
{
    public:
        void SetUp() override
        {
            unsetenv("ASTROSPHERIC_API_KEY");
            const std::string cfg = ::testing::TempDir() + "astrospheric_test_config.xml";
            std::remove(cfg.c_str());
            setenv("INDICONFIG", cfg.c_str(), 1);
        }
};

::testing::Environment *const isolatedEnv __attribute__((unused)) = ::testing::AddGlobalTestEnvironment(new IsolatedEnvironment);

} // namespace

// ---------------------------------------------------------------------------
// Parsing: normal values
// ---------------------------------------------------------------------------

TEST(AstrosphericParse, NormalFixtureConvertsUnits)
{
    astrospheric::Forecast fc;
    std::string error;
    ASSERT_TRUE(astrospheric::parseForecast(readFixture("astrospheric_v2_normal.json"), fc, error)) << error;

    // Hourly entries 00,01,02 then a 3-hour step: the parser stops at the gap.
    ASSERT_EQ(fc.hours(), 3u);
    EXPECT_EQ(fc.temperature.size(), 3u);
    EXPECT_EQ(fc.windSpeed.size(), 3u);
    EXPECT_EQ(fc.dewPoint.size(), 3u);
    EXPECT_EQ(fc.windDirection.size(), 3u);
    EXPECT_EQ(fc.seeing.size(), 3u);
    EXPECT_EQ(fc.transparency.size(), 3u);

    EXPECT_EQ(fc.startTime, kStart);
    EXPECT_EQ(fc.startTimeStr, "2026-11-03T00:00:00Z");

    EXPECT_DOUBLE_EQ(fc.cloudCover[0], 12.0);
    EXPECT_NEAR(fc.temperature[0], 10.0, 1e-9);   // K -> C
    EXPECT_NEAR(fc.windSpeed[0], 18.0, 1e-9);     // 5 m/s -> kph
    EXPECT_NEAR(fc.dewPoint[0], 0.0, 1e-9);       // K -> C
    EXPECT_DOUBLE_EQ(fc.windDirection[0], 270.0);
    EXPECT_DOUBLE_EQ(fc.seeing[0], 3.5);
    EXPECT_DOUBLE_EQ(fc.transparency[0], 10.0);

    EXPECT_DOUBLE_EQ(fc.cloudCover[1], 65.0);
    EXPECT_NEAR(fc.windSpeed[1], 28.8, 1e-9);
    EXPECT_DOUBLE_EQ(fc.cloudCover[2], 90.0);
    EXPECT_NEAR(fc.windSpeed[2], 43.2, 1e-9);

    EXPECT_TRUE(fc.hasCredits);
    EXPECT_EQ(fc.creditCost, 15);
    EXPECT_EQ(fc.creditsRemaining, 99985);
}

TEST(AstrosphericParse, CreditFieldsAreOptional)
{
    astrospheric::Forecast fc;
    std::string error;
    const std::string body = "{\"HourlyForecast\":[" + hour("2026-11-03T00:00:00Z") + "]}";
    ASSERT_TRUE(astrospheric::parseForecast(body, fc, error)) << error;
    EXPECT_EQ(fc.hours(), 1u);
    EXPECT_FALSE(fc.hasCredits);
}

TEST(AstrosphericParse, CreditCostDefaultsToZero)
{
    astrospheric::Forecast fc;
    std::string error;
    const std::string body = "{\"APICreditsRemaining\":500,\"HourlyForecast\":[" + hour("2026-11-03T00:00:00Z") + "]}";
    ASSERT_TRUE(astrospheric::parseForecast(body, fc, error)) << error;
    EXPECT_TRUE(fc.hasCredits);
    EXPECT_EQ(fc.creditCost, 0);
    EXPECT_EQ(fc.creditsRemaining, 500);
}

TEST(AstrosphericParse, StopsAtFirstNonHourlyStep)
{
    astrospheric::Forecast fc;
    std::string error;
    const std::string body = "{\"HourlyForecast\":[" + hour("2026-11-03T00:00:00Z") + "," +
                             hour("2026-11-03T03:00:00Z") + "]}";
    ASSERT_TRUE(astrospheric::parseForecast(body, fc, error)) << error;
    EXPECT_EQ(fc.hours(), 1u);
}

TEST(AstrosphericParse, UTCDateTime)
{
    EXPECT_EQ(astrospheric::parseUTCDateTime("2026-11-03T00:00:00Z"), kStart);
    EXPECT_EQ(astrospheric::parseUTCDateTime("2026-11-03T01:00:00Z"), kStart + 3600);
    EXPECT_EQ(astrospheric::parseUTCDateTime("not a date"), static_cast<time_t>(-1));
    EXPECT_EQ(astrospheric::parseUTCDateTime(""), static_cast<time_t>(-1));
}

// ---------------------------------------------------------------------------
// Parsing: missing fields
// ---------------------------------------------------------------------------

TEST(AstrosphericParse, MissingFieldInLaterHourFailsWholeParse)
{
    astrospheric::Forecast fc;
    std::string error;
    EXPECT_FALSE(astrospheric::parseForecast(readFixture("astrospheric_v2_missing_wind.json"), fc, error));
    EXPECT_NE(error.find("JSON parsing error"), std::string::npos) << error;
    EXPECT_EQ(fc.hours(), 0u);        // nothing half-filled
    EXPECT_TRUE(fc.windSpeed.empty());
}

TEST(AstrosphericParse, MissingHourlyForecast)
{
    astrospheric::Forecast fc;
    std::string error;
    EXPECT_FALSE(astrospheric::parseForecast("{\"APICreditsRemaining\":10}", fc, error));
    EXPECT_NE(error.find("HourlyForecast"), std::string::npos) << error;
}

TEST(AstrosphericParse, EmptyOrWrongTypeHourlyForecast)
{
    astrospheric::Forecast fc;
    std::string error;
    EXPECT_FALSE(astrospheric::parseForecast("{\"HourlyForecast\":[]}", fc, error));
    EXPECT_NE(error.find("HourlyForecast"), std::string::npos) << error;
    EXPECT_FALSE(astrospheric::parseForecast("{\"HourlyForecast\":{}}", fc, error));
    EXPECT_FALSE(astrospheric::parseForecast("{\"HourlyForecast\":\"x\"}", fc, error));
}

TEST(AstrosphericParse, MissingTimestamp)
{
    astrospheric::Forecast fc;
    std::string error;
    EXPECT_FALSE(astrospheric::parseForecast("{\"HourlyForecast\":[{\"Cloud\":{\"ActualValue\":1}}]}", fc, error));
    EXPECT_NE(error.find("JSON parsing error"), std::string::npos) << error;
}

TEST(AstrosphericParse, BadTimestampFormat)
{
    astrospheric::Forecast fc;
    std::string error;
    EXPECT_FALSE(astrospheric::parseForecast("{\"HourlyForecast\":[" + hour("03/11/2026 00:00") + "]}", fc, error));
    EXPECT_EQ(error, "Failed to parse UTCForecastHour.");
}

TEST(AstrosphericParse, MissingActualValue)
{
    astrospheric::Forecast fc;
    std::string error;
    std::string body = "{\"HourlyForecast\":[" + hour("2026-11-03T00:00:00Z") + "]}";
    const std::string from = "\"Seeing\":{\"ActualValue\":3.5}";
    body.replace(body.find(from), from.size(), "\"Seeing\":{}");
    EXPECT_FALSE(astrospheric::parseForecast(body, fc, error));
    EXPECT_EQ(fc.hours(), 0u);
}

TEST(AstrosphericParse, ApiErrorInfo)
{
    astrospheric::Forecast fc;
    std::string error;
    EXPECT_FALSE(astrospheric::parseForecast(readFixture("astrospheric_v2_error.json"), fc, error));
    EXPECT_EQ(error, "Astrospheric API error: Invalid API Key");
}

// ---------------------------------------------------------------------------
// Parsing: malformed JSON
// ---------------------------------------------------------------------------

TEST(AstrosphericParse, MalformedJson)
{
    astrospheric::Forecast fc;
    std::string error;
    for (const std::string &body : {readFixture("astrospheric_v2_truncated.json"),
                                    std::string(""), std::string("not json"),
                                    std::string("{\"HourlyForecast\":[")})
    {
        error.clear();
        EXPECT_FALSE(astrospheric::parseForecast(body, fc, error)) << body;
        EXPECT_NE(error.find("JSON parsing error"), std::string::npos) << error;
        EXPECT_EQ(fc.hours(), 0u);
    }
}

TEST(AstrosphericParse, NonNumericValue)
{
    astrospheric::Forecast fc;
    std::string error;
    const std::string body = "{\"HourlyForecast\":[" + hour("2026-11-03T00:00:00Z", "\"abc\"") + "]}";
    EXPECT_FALSE(astrospheric::parseForecast(body, fc, error));
    EXPECT_NE(error.find("JSON parsing error"), std::string::npos) << error;
}

TEST(AstrosphericParse, NonObjectRoot)
{
    astrospheric::Forecast fc;
    std::string error;
    EXPECT_FALSE(astrospheric::parseForecast("[]", fc, error));
    EXPECT_FALSE(astrospheric::parseForecast("42", fc, error));
}

// ---------------------------------------------------------------------------
// Safety thresholds, using the driver's real parameter setup and INDI's
// WeatherInterface range logic.
// ---------------------------------------------------------------------------

class TestableAstrospheric : public AstrosphericWeather
{
    public:
        void set(const char *name, double value) { setParameterValue(name, value); }
        IPState state(const char *name) const { return checkParameterState(name); }
        bool isCritical(const char *name) { return critialParametersLP.findWidgetByName(name) != nullptr; }
        IPState overall()
        {
            syncCriticalParameters();
            return critialParametersLP.getState();
        }
};

class AstrosphericSafety : public ::testing::Test
{
    protected:
        void SetUp() override
        {
            driver.reset(new TestableAstrospheric());
            ASSERT_TRUE(driver->initProperties());
        }
        std::unique_ptr<TestableAstrospheric> driver;
};

TEST_F(AstrosphericSafety, CloudAndWindAreCritical)
{
    EXPECT_TRUE(driver->isCritical("WEATHER_CLOUD_COVER"));
    EXPECT_TRUE(driver->isCritical("WEATHER_WIND_SPEED"));
    EXPECT_FALSE(driver->isCritical("WEATHER_TEMPERATURE"));
}

TEST_F(AstrosphericSafety, CloudThresholds)
{
    // OK < 60 %, warning 60-80 %, Alert > 80 %
    const struct { double v; IPState s; } cases[] =
    {
        {0.0, IPS_OK}, {30.0, IPS_OK}, {59.9, IPS_OK}, {60.0, IPS_OK},
        {60.1, IPS_BUSY}, {70.0, IPS_BUSY}, {80.0, IPS_BUSY},
        {80.1, IPS_ALERT}, {100.0, IPS_ALERT},
    };
    for (const auto &c : cases)
    {
        driver->set("WEATHER_CLOUD_COVER", c.v);
        EXPECT_EQ(driver->state("WEATHER_CLOUD_COVER"), c.s) << "cloud " << c.v;
    }
}

TEST_F(AstrosphericSafety, WindThresholds)
{
    // OK < 25 kph, warning 25-40 kph, Alert > 40 kph
    const struct { double v; IPState s; } cases[] =
    {
        {0.0, IPS_OK}, {10.0, IPS_OK}, {24.9, IPS_OK}, {25.0, IPS_OK},
        {25.1, IPS_BUSY}, {32.0, IPS_BUSY}, {40.0, IPS_BUSY},
        {40.1, IPS_ALERT}, {80.0, IPS_ALERT},
    };
    for (const auto &c : cases)
    {
        driver->set("WEATHER_WIND_SPEED", c.v);
        EXPECT_EQ(driver->state("WEATHER_WIND_SPEED"), c.s) << "wind " << c.v;
    }
}

TEST_F(AstrosphericSafety, ParsedFixtureDrivesOverallState)
{
    astrospheric::Forecast fc;
    std::string error;
    ASSERT_TRUE(astrospheric::parseForecast(readFixture("astrospheric_v2_normal.json"), fc, error)) << error;
    ASSERT_EQ(fc.hours(), 3u);

    // Hour 0: cloud 12 %, wind 18 kph -> OK
    driver->set("WEATHER_CLOUD_COVER", fc.cloudCover[0]);
    driver->set("WEATHER_WIND_SPEED", fc.windSpeed[0]);
    EXPECT_EQ(driver->overall(), IPS_OK);

    // Hour 1: cloud 65 %, wind 28.8 kph -> warning
    driver->set("WEATHER_CLOUD_COVER", fc.cloudCover[1]);
    driver->set("WEATHER_WIND_SPEED", fc.windSpeed[1]);
    EXPECT_EQ(driver->state("WEATHER_WIND_SPEED"), IPS_BUSY);
    EXPECT_EQ(driver->overall(), IPS_BUSY);

    // Hour 2: cloud 90 %, wind 43.2 kph -> Alert
    driver->set("WEATHER_CLOUD_COVER", fc.cloudCover[2]);
    driver->set("WEATHER_WIND_SPEED", fc.windSpeed[2]);
    EXPECT_EQ(driver->state("WEATHER_WIND_SPEED"), IPS_ALERT);
    EXPECT_EQ(driver->overall(), IPS_ALERT);

    // Wind alone is enough to raise the alarm on a clear sky.
    driver->set("WEATHER_CLOUD_COVER", 0.0);
    driver->set("WEATHER_WIND_SPEED", 45.0);
    EXPECT_EQ(driver->overall(), IPS_ALERT);
}
