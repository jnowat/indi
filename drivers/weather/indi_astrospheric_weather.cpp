/*******************************************************************************
 Copyright(c) 2025 Jacob Nowatzke. All rights reserved.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License version 2 as published by the Free Software Foundation.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.

 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.

 Contributions:
 - Grok-3 (xAI)
 - GPT-4o (OpenAI)
*******************************************************************************/

#include "indi_astrospheric_weather.h"

#include <indicom.h>
#include <httplib.h>
#ifdef _USE_SYSTEM_JSONLIB
#include <nlohmann/json.hpp>
#else
#include <indijson.hpp>
#endif
using json = nlohmann::json;

#include <memory>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>

#define ASTROSPHERIC_API_HOST "astrosphericpublicaccess.azurewebsites.net"
#define ASTROSPHERIC_API_PATH "/api/GetForecastData_V1"
#define ASTROSPHERIC_CONNECTION_TIMEOUT_SEC 5
#define ASTROSPHERIC_READ_TIMEOUT_SEC 15
#define ASTROSPHERIC_DATA_REFRESH_INTERVAL_SEC 21600 // 6 hours
#define ASTROSPHERIC_EXPECTED_HOURS 82
#define ASTROSPHERIC_CREDIT_LIMIT 100
#define ASTROSPHERIC_CREDIT_THRESHOLD 90

AstrosphericWeather::AstrosphericWeather()
{
    setVersion(1, 2);
    setDeviceName("Astrospheric Weather");

    forecastValid = false;
    lastFetchTime = 0;
    forecastHours = 0;
    forecastStartTime = 0;
    apiCreditsUsed = 0;
}

const char *AstrosphericWeather::getDefaultName()
{
    return "Astrospheric Weather";
}

bool AstrosphericWeather::initProperties()
{
    setDriverInterface(WEATHER_INTERFACE);
    INDI::Weather::initProperties();

    APIKeyTP[0].fill("API_KEY_VALUE", "Key", "");
    APIKeyTP.fill(getDeviceName(), "ASTROSPHERIC_API_KEY", "API Key", "Options", IP_RW, 60, IPS_IDLE);
    defineProperty(APIKeyTP);

    addParameter("WEATHER_CLOUD_COVER", "Cloud Cover (%)", 0, 100, 50);
    addParameter("WEATHER_TEMPERATURE", "Temperature (C)", -50, 50, 0);
    addParameter("WEATHER_WIND_SPEED", "Wind Speed (kph)", 0, 200, 50);
    addParameter("WEATHER_DEW_POINT", "Dew Point (C)", -50, 50, 0);
    addParameter("WEATHER_WIND_DIRECTION", "Wind Direction (°)", 0, 360, 0);
    addParameter("WEATHER_SEEING", "Seeing (0–5)", 0, 5, 0);
    addParameter("WEATHER_TRANSPARENCY", "Transparency (0–27+)", 0, 30, 0);

    setCriticalParameter("WEATHER_CLOUD_COVER");

    LOG_INFO("Astrospheric Weather driver initialized.");
    return true;
}

bool AstrosphericWeather::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0)
    {
        if (APIKeyTP.isNameMatch(name))
        {
            APIKeyTP.update(texts, names, n);
            APIKeyTP.setState(IPS_OK);
            APIKeyTP.apply();
            saveConfig(true, APIKeyTP.getName());
            forecastValid = false;
            return true;
        }
    }

    return INDI::Weather::ISNewText(dev, name, texts, names, n);
}

bool AstrosphericWeather::saveConfigItems(FILE *fp)
{
    INDI::Weather::saveConfigItems(fp);
    APIKeyTP.save(fp);
    return true;
}

IPState AstrosphericWeather::updateWeather()
{
    if (!APIKeyTP[0].getText() || strlen(APIKeyTP[0].getText()) == 0)
    {
        LOG_ERROR("API Key is not set.");
        APIKeyTP.setState(IPS_ALERT);
        APIKeyTP.apply("API Key Missing");
        return IPS_ALERT;
    }

    time_t now_utc = std::time(nullptr);

    if (!forecastValid || difftime(now_utc, lastFetchTime) >= ASTROSPHERIC_DATA_REFRESH_INTERVAL_SEC)
    {
        LOG_INFO("Fetching new weather data...");
        std::string responseBody;

        if (fetchDataFromAPI(responseBody) && parseJSONResponse(responseBody))
        {
            lastFetchTime = now_utc;
            forecastValid = true;
            LOG_INFO("Weather data updated successfully.");
        }
        else
        {
            forecastValid = false;
            LOG_ERROR("Failed to update weather data.");
            return IPS_ALERT;
        }
    }

    if (forecastValid && forecastHours > 0)
    {
        double secondsDiff = difftime(now_utc, forecastStartTime);
        int hourIndex = static_cast<int>(secondsDiff / 3600.0);

        if (hourIndex >= 0 && hourIndex < forecastHours)
        {
            setParameterValue("WEATHER_CLOUD_COVER", cloudCover[hourIndex]);
            setParameterValue("WEATHER_TEMPERATURE", temperature[hourIndex] - 273.15);
            setParameterValue("WEATHER_WIND_SPEED", windSpeed[hourIndex] * 3.6);
            setParameterValue("WEATHER_DEW_POINT", dewPoint[hourIndex] - 273.15);
            setParameterValue("WEATHER_WIND_DIRECTION", windDirection[hourIndex]);
            setParameterValue("WEATHER_SEEING", seeing[hourIndex]);
            setParameterValue("WEATHER_TRANSPARENCY", transparency[hourIndex]);
            return IPS_OK;
        }
        else
        {
            LOG_WARN("Current time is outside the forecast range.");
            forecastValid = false;
            return IPS_ALERT;
        }
    }

    LOG_WARN("Forecast data is invalid or empty.");
    return IPS_ALERT;
}

bool AstrosphericWeather::fetchDataFromAPI(std::string &responseBody)
{
    // your existing fetch code (using httplib)
    return false;
}

bool AstrosphericWeather::parseJSONResponse(const std::string &jsonResponse)
{
    // your existing JSON parsing code
    return false;
}

time_t AstrosphericWeather::parseUTCDateTime(const std::string &dateTimeStr)
{
    std::tm tm = {};
    std::istringstream ss(dateTimeStr);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail())
        return static_cast<time_t>(-1);

#if defined(_WIN32)
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

// Clean createDevice, no static pointer
extern "C" INDI::DefaultDevice *createDevice()
{
    return new AstrosphericWeather();
}
