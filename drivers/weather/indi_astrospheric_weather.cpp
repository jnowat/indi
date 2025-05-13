/*******************************************************************************
 Copyright(c) 2025 Jacob Nowatzke. All rights reserved.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License version 2 as published by the Free Software Foundation.
 .
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.
 .
 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.
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
#define ASTROSPHERIC_EXPECTED_HOURS 82 // 0–81 hours
#define ASTROSPHERIC_CREDIT_LIMIT 100
#define ASTROSPHERIC_CREDIT_THRESHOLD 90

AstrosphericWeather::AstrosphericWeather()
{
    setVersion(1, 2);
    forecastValid = false;
    lastFetchTime = 0;
    forecastHours = 0;
    forecastStartTime = 0;
    apiCreditsUsed = 0;

    APIKeyT[0].fill("API_KEY_VALUE", "Key", "");
    APIKeyTP.fill(getDeviceName(), "ASTROSPHERIC_API_KEY", "API Key", "Options", IP_RW, 60, IPS_IDLE);
    APIKeyTP.add(APIKeyT);
}

const char *AstrosphericWeather::getDefaultName()
{
    return "Astrospheric Weather";
}

bool AstrosphericWeather::initProperties()
{
    INDI::Weather::initProperties();
    defineProperty(&APIKeyTP);

    addParameter("WEATHER_CLOUD_COVER", "Cloud Cover (%)", 0, 100, 0, IPS_IDLE, "%.0f %%");
    addParameter("WEATHER_TEMPERATURE", "Temperature (C)", -50, 50, 0, IPS_IDLE, "%.1f C");
    addParameter("WEATHER_WIND_SPEED", "Wind Speed (kph)", 0, 200, 0, IPS_IDLE, "%.1f kph");
    addParameter("WEATHER_DEW_POINT", "Dew Point (C)", -50, 50, 0, IPS_IDLE, "%.1f C");
    addParameter("WEATHER_WIND_DIRECTION", "Wind Direction (°)", 0, 360, 0, IPS_IDLE, "%.0f °");
    addParameter("WEATHER_SEEING", "Seeing (0–5)", 0, 5, 0, IPS_IDLE, "%.0f");
    addParameter("WEATHER_TRANSPARENCY", "Transparency (0–27+)", 0, 30, 0, IPS_IDLE, "%.0f");

    setCriticalParameter("WEATHER_CLOUD_COVER");
    loadConfig(true); // Load config silently
    LOG_INFO("Astrospheric Weather driver initialized.");
    return true;
}

bool AstrosphericWeather::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (APIKeyTP.isNameMatch(name))
        {
            APIKeyTP.update(texts, names, n);
            APIKeyTP.setState(IPS_OK);
            APIKeyTP.apply();
            saveConfig(true, APIKeyTP.getName());
            LOGF_INFO("API Key updated: %s", APIKeyT[0].getText());
            forecastValid = false;
            return true;
        }
    }
    return INDI::Weather::ISNewText(dev, name, texts, names, n);
}

bool AstrosphericWeather::loadConfig(bool silent, const char *property)
{
    bool rc = INDI::DefaultDevice::loadConfig(silent, property);
    if (rc)
    {
        LOGF_INFO("Configuration loaded, including API Key: %s", APIKeyT[0].getText() ? APIKeyT[0].getText() : "EMPTY");
    }
    return rc;
}

bool AstrosphericWeather::saveConfig(bool silent, const char *property)
{
    bool rc = INDI::DefaultDevice::saveConfig(silent, property);
    if (rc)
    {
        LOGF_INFO("Configuration saved, including API Key: %s", APIKeyT[0].getText());
    }
    return rc;
}

time_t AstrosphericWeather::parseUTCDateTime(const std::string& dateTimeStr)
{
    std::tm tm = {};
    std::istringstream ss(dateTimeStr);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail())
    {
        LOGF_ERROR("Failed to parse UTC date/time string: %s", dateTimeStr.c_str());
        return static_cast<time_t>(-1);
    }

    #if defined(_WIN32)
        return _mkgmtime(&tm);
    #elif defined(__GNUC__) || defined(__clang__)
        return timegm(&tm);
    #else
        char *original_tz = getenv("TZ");
        std::string tz_backup;
        if (original_tz) tz_backup = original_tz;
        setenv("TZ", "UTC", 1);
        tzset();
        time_t t = std::mktime(&tm);
        if (original_tz) setenv("TZ", tz_backup.c_str(), 1);
        else unsetenv("TZ");
        tzset();
        return t;
    #endif
}

bool AstrosphericWeather::fetchDataFromAPI(std::string &responseBody)
{
    INumberVectorProperty *locationNP = getNumber("GEOGRAPHIC_COORD");
    if (!locationNP || locationNP->ncount < 2 || locationNP->s == IPS_IDLE || locationNP->s == IPS_ALERT)
    {
        LOG_ERROR("GEOGRAPHIC_COORD not set. Please configure location.");
        return false;
    }
    double lat = locationNP->n[IND_LATITUDE].value;
    double lon = locationNP->n[IND_LONGITUDE].value;

    json payload;
    payload["Latitude"] = lat;
    payload["Longitude"] = lon;
    payload["APIKey"] = APIKeyT[0].getText() ? APIKeyT[0].getText() : "";

    std::string jsonData = payload.dump();

    httplib::SSLClient cli(ASTROSPHERIC_API_HOST);
    cli.set_connection_timeout(ASTROSPHERIC_CONNECTION_TIMEOUT_SEC, 0);
    cli.set_read_timeout(ASTROSPHERIC_READ_TIMEOUT_SEC, 0);
    cli.set_follow_location(true);

    LOGF_DEBUG("Fetching data from: https://%s%s with payload: %s", ASTROSPHERIC_API_HOST, ASTROSPHERIC_API_PATH, jsonData.c_str());
    auto res = cli.Post(ASTROSPHERIC_API_PATH, jsonData, "application/json");

    if (res)
    {
        LOGF_DEBUG("API Response Status: %d", res->status);
        responseBody = res->body;
        if (res->status == 200)
        {
            LOG_DEBUG("API Response received successfully.");
            return true;
        }
        else
        {
            try
            {
                json errorInfo = json::parse(responseBody);
                std::string errorMsg = errorInfo.contains("ErrorInfo") ? errorInfo["ErrorInfo"].get<std::string>() : "Unknown error";
                LOGF_ERROR("API request failed. HTTP status: %d, Error: %s", res->status, errorMsg.c_str());
            }
            catch (const json::exception &e)
            {
                LOGF_ERROR("API request failed. HTTP status: %d, Body: %s", res->status, responseBody.c_str());
            }
            return false;
        }
    }
    else
    {
        auto err = res.error();
        LOGF_ERROR("API connection failed. httplib error code: %d", static_cast<int>(err));
        return false;
    }
}

bool AstrosphericWeather::parseJSONResponse(const std::string &jsonResponse)
{
    try
    {
        if (jsonResponse.empty())
        {
            LOG_ERROR("JSON response body is empty.");
            return false;
        }

        json root = json::parse(jsonResponse);

        if (!root.contains("UTCStartTime") || !root["UTCStartTime"].is_string())
        {
            LOG_ERROR("UTCStartTime not found or not a string.");
            return false;
        }
        forecastStartTime = parseUTCDateTime(root["UTCStartTime"].get<std::string>());
        if (forecastStartTime == static_cast<time_t>(-1))
        {
            LOG_ERROR("Failed to parse UTCStartTime.");
            return false;
        }

        if (root.contains("APICreditUsedToday") && root["APICreditUsedToday"].is_number_integer())
        {
            apiCreditsUsed = root["APICreditUsedToday"].get<int>();
            if (apiCreditsUsed >= ASTROSPHERIC_CREDIT_THRESHOLD)
            {
                LOGF_WARN("API credits used today: %d. Approaching daily limit of %d.", apiCreditsUsed, ASTROSPHERIC_CREDIT_LIMIT);
            }
        }

        cloudCover.clear();
        temperature.clear();
        windSpeed.clear();
        dewPoint.clear();
        windDirection.clear();
        seeing.clear();
        transparency.clear();

        const char* dataKeys[] = {
            "RDPS_CloudCover", "RDPS_Temperature", "RDPS_WindVelocity",
            "RDPS_DewPoint", "RDPS_WindDirection", "Astrospheric_Seeing", "Astrospheric_Transparency"
        };
        std::vector<double>* dataVectors[] = {
            &cloudCover, &temperature, &windSpeed,
            &dewPoint, &windDirection, &seeing, &transparency
        };

        for (size_t i = 0; i < sizeof(dataKeys) / sizeof(dataKeys[0]); ++i)
        {
            const char* dataKey = dataKeys[i];
            std::vector<double>* currentVector = dataVectors[i];

            if (root.contains(dataKey) && root[dataKey].is_array())
            {
                const json &array = root[dataKey];
                for (const auto &hourData : array)
                {
                    if (hourData.contains("Value") && hourData["Value"].contains("ActualValue") && hourData["Value"]["ActualValue"].is_number())
                    {
                        currentVector->push_back(hourData["Value"]["ActualValue"].get<double>());
                    }
                    else
                    {
                        LOGF_WARN("Missing or invalid 'ActualValue' in %s array. Using 0.0.", dataKey);
                        currentVector->push_back(0.0);
                    }
                }
            }
            else
            {
                LOGF_ERROR("%s data not found or not an array.", dataKey);
                return false;
            }
        }

        if (!cloudCover.empty())
        {
            forecastHours = cloudCover.size();
        }
        else
        {
            LOG_ERROR("Cloud cover data is empty.");
            forecastHours = 0;
            return false;
        }

        if (forecastHours != ASTROSPHERIC_EXPECTED_HOURS)
        {
            LOGF_ERROR("Unexpected forecast length: %d hours (expected %d).", forecastHours, ASTROSPHERIC_EXPECTED_HOURS);
            return false;
        }

        for (size_t i = 0; i < sizeof(dataVectors) / sizeof(dataVectors[0]); ++i)
        {
            if (dataVectors[i]->size() != static_cast<size_t>(forecastHours))
            {
                LOGF_ERROR("Mismatch in forecast data sizes for %s: %zu vs %d.", dataKeys[i], dataVectors[i]->size(), forecastHours);
                return false;
            }
        }

        LOGF_INFO("Parsed %d hours of forecast data starting from %s UTC.", forecastHours, INDI::DefaultDevice::timetoutc(forecastStartTime).c_str());
        return true;
    }
    catch (const json::parse_error &e)
    {
        LOGF_ERROR("JSON parse error: %s (id: %d, byte: %zu).", e.what(), e.id, e.byte);
        return false;
    }
    catch (const std::exception &e)
    {
        LOGF_ERROR("Exception during JSON processing: %s", e.what());
        return false;
    }
}

IPState AstrosphericWeather::updateWeather()
{
    if (APIKeyT[0].getText() == nullptr || strlen(APIKeyT[0].getText()) == 0)
    {
        LOG_ERROR("API Key is not set.");
        APIKeyTP.setState(IPS_ALERT);
        IDSetText(&APIKeyTP, "API Key Missing");
        if (getWeatherParametersNP())
        {
            getWeatherParametersNP()->setState(IPS_ALERT);
            IDSetNumber(getWeatherParametersNP(), "API Key Missing");
        }
        return IPS_ALERT;
    }

    INumberVectorProperty *locationNP = getNumber("GEOGRAPHIC_COORD");
    if (!locationNP || locationNP->s == IPS_IDLE || locationNP->s == IPS_ALERT)
    {
        LOG_INFO("Location not set. Waiting for client.");
        if (getWeatherParametersNP())
        {
            getWeatherParametersNP()->setState(IPS_IDLE);
            IDSetNumber(getWeatherParametersNP(), "Location not set");
        }
        return IPS_IDLE;
    }

    time_t now_utc = INDI::DefaultDevice::getUTCTime();

    if (!forecastValid || difftime(now_utc, lastFetchTime) >= ASTROSPHERIC_DATA_REFRESH_INTERVAL_SEC || lastFetchTime == 0)
    {
        LOG_INFO("Fetching new weather data...");
        std::string responseBody;

        if (getWeatherParametersNP())
        {
            getWeatherParametersNP()->setState(IPS_BUSY);
            IDSetNumber(getWeatherParametersNP(), "Fetching data...");
        }

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
            if (getWeatherParametersNP())
            {
                getWeatherParametersNP()->setState(IPS_ALERT);
                IDSetNumber(getWeatherParametersNP(), "API Fetch/Parse Error");
            }
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

            if (getWeatherParametersNP())
            {
                getWeatherParametersNP()->setState(IPS_OK);
                IDSetNumber(getWeatherParametersNP(), nullptr);
            }
            return IPS_OK;
        }
        else
        {
            LOG_WARN("Current time is outside the forecast range.");
            forecastValid = false;
            if (getWeatherParametersNP())
            {
                getWeatherParametersNP()->setState(IPS_ALERT);
                IDSetNumber(getWeatherParametersNP(), "Data out of range");
            }
            return IPS_ALERT;
        }
    }

    LOG_WARN("Forecast data is invalid or empty.");
    if (getWeatherParametersNP())
    {
        getWeatherParametersNP()->setState(IPS_ALERT);
        IDSetNumber(getWeatherParametersNP(), "Forecast invalid");
    }
    return IPS_ALERT;
}

extern "C" INDI::DefaultDevice *createDevice()
{
    return new AstrosphericWeather();
}