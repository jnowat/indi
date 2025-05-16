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

#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>

// Constructor for AstrosphericWeather.
AstrosphericWeather::AstrosphericWeather()
{
    // Set driver version to 0.2 (alpha).
    setVersion(0, 2);
    // Set connection type to none (weather driver, no physical device).
    setWeatherConnection(CONNECTION_NONE);
    // Initialize forecast-related variables.
    forecastValid = false;
    lastFetchTime = 0;
    forecastHours = 0;
    forecastStartTime = 0;
    apiCreditsUsed = 0;
    locationReceived = false;
    timerID = -1;
}

// Returns the default name of the device.
const char *AstrosphericWeather::getDefaultName()
{
    return "Astrospheric Weather";
}

// Handles device connection.
bool AstrosphericWeather::Connect()
{
    LOG_INFO("AstrosphericWeather: Connecting...");
    // Mark device as connected.
    setConnected(true);
    // Attempt to sync location from telescope.
    syncLocationFromSite();
    // Update properties for the client.
    updateProperties();
    // Start the timer for periodic updates.
    timerID = SetTimer(static_cast<int>(WeatherRefreshN[0].value * 1000)); // Convert seconds to milliseconds
    return true;
}

// Handles device disconnection.
bool AstrosphericWeather::Disconnect()
{
    LOG_INFO("AstrosphericWeather: Disconnecting...");
    // Stop the timer.
    if (timerID >= 0)
    {
        RemoveTimer(timerID);
        timerID = -1;
    }
    // Mark device as disconnected.
    setConnected(false);
    // Update properties for the client.
    updateProperties();
    return true;
}

// Initializes all device properties.
bool AstrosphericWeather::initProperties()
{
    // Initialize base weather properties.
    INDI::Weather::initProperties();

    // Define API key property in Options tab.
    APIKeyTP[0].fill("API_KEY_VALUE", "Key", "");
    APIKeyTP.fill(getDeviceName(), "ASTROSPHERIC_API_KEY", "API Key", "Options", IP_RW, 60, IPS_IDLE);
    defineProperty(APIKeyTP);

    // Define location property in Options tab.
    LocationNP[LOCATION_LATITUDE].fill("LATITUDE", "Latitude (deg)", "%.4f", -90.0, 90.0, 0.0, 0.0);
    LocationNP[LOCATION_LONGITUDE].fill("LONGITUDE", "Longitude (deg)", "%.4f", -180.0, 360.0, 0.0, 0.0);
    LocationNP.fill(getDeviceName(), "LOCATION", "Location", "Options", IP_RW, 0, IPS_IDLE);
    defineProperty(LocationNP);

    // Define telescope name property for snooping location.
    TelescopeNameTP[0].fill("TELESCOPE_NAME", "Telescope", "Telescope Simulator");
    TelescopeNameTP.fill(getDeviceName(), "TELESCOPE_NAME", "Snoop Telescope", "Options", IP_RW, 60, IPS_IDLE);
    defineProperty(TelescopeNameTP);

    // Define mode switch property in Options tab (API vs Simulated).
    ModeSP[0].fill("API_MODE", "API Mode", ISS_OFF);
    ModeSP[1].fill("SIMULATED_MODE", "Simulated Mode", ISS_ON);
    ModeSP.fill(getDeviceName(), "WEATHER_MODE", "Mode", "Options", IP_RW, ISR_1OFMANY, 0, IPS_IDLE);
    defineProperty(ModeSP);

    // Define weather parameters for the Parameters tab.
    addParameter("WEATHER_CLOUD_COVER", "Cloud Cover (%)", 0, 100, 50);
    addParameter("WEATHER_TEMPERATURE", "Temperature (C)", -50, 50, 0);
    addParameter("WEATHER_WIND_SPEED", "Wind Speed (kph)", 0, 200, 50);
    addParameter("WEATHER_DEW_POINT", "Dew Point (C)", -50, 50, 0);
    addParameter("WEATHER_WIND_DIRECTION", "Wind Direction (°)", 0, 360, 0);
    addParameter("WEATHER_SEEING", "Seeing (0–5)", 0, 5, 0);
    addParameter("WEATHER_TRANSPARENCY", "Transparency (0–27+)", 0, 30, 0);

    // Set cloud cover as the critical parameter for weather alerts.
    setCriticalParameter("WEATHER_CLOUD_COVER");

    // Define custom refresh period: max 3600 seconds (1 hour), default 1800 seconds (30 minutes).
    IUFillNumber(&WeatherRefreshN[0], "PERIOD", "Period (s)", "%.f", 0, 3600, 0, 1800);
    IUFillNumberVector(&WeatherRefreshNP, WeatherRefreshN, 1, getDeviceName(), "WEATHER_UPDATE_PERIOD", "Refresh Period", MAIN_CONTROL_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&WeatherRefreshNP);

    // Define weather summary property in the Main Control tab.
    IUFillText(&WeatherSummaryT[0], "SUMMARY", "Weather Summary", "N/A");
    IUFillTextVector(&WeatherSummaryTP, WeatherSummaryT, 1, getDeviceName(), "WEATHER_SUMMARY", "Status", MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);
    defineProperty(&WeatherSummaryTP);

    // Add debug control for logging.
    addDebugControl();

    // Load saved configuration for API key, location, telescope name, and mode.
    loadConfig(true, "ASTROSPHERIC_API_KEY");
    loadConfig(true, "LOCATION");
    loadConfig(true, "TELESCOPE_NAME");
    loadConfig(true, "WEATHER_MODE");

    // Start snooping on the telescope for location data.
    if (TelescopeNameTP[0].getText() && strlen(TelescopeNameTP[0].getText()) > 0)
    {
        IDSnoopDevice(TelescopeNameTP[0].getText(), "GEOGRAPHIC_COORD");
        LOGF_INFO("Snooping on telescope %s for location data.", TelescopeNameTP[0].getText());
    }

    return true;
}

// Updates properties based on connection state.
bool AstrosphericWeather::updateProperties()
{
    // Let the base class handle standard property updates.
    INDI::Weather::updateProperties();
    return true;
}

// Handles updates to number properties (e.g., location, snooped telescope data).
bool AstrosphericWeather::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0)
    {
        // Handle updates to the LocationNP property.
        if (LocationNP.isNameMatch(name))
        {
            LocationNP.update(values, names, n);
            LocationNP.setState(IPS_OK);
            LocationNP.apply();
            LOGF_INFO("Location updated: Latitude=%.4f, Longitude=%.4f",
                      LocationNP[LOCATION_LATITUDE].getValue(),
                      LocationNP[LOCATION_LONGITUDE].getValue());
            // Invalidate forecast when location changes.
            forecastValid = false;
            locationReceived = true; // Consider manually set location as received
            return true;
        }
        // Handle updates to the custom refresh period.
        if (strcmp(name, "WEATHER_UPDATE_PERIOD") == 0)
        {
            IUUpdateNumber(&WeatherRefreshNP, values, names, n);
            WeatherRefreshNP.s = IPS_OK;
            IDSetNumber(&WeatherRefreshNP, nullptr);
            LOGF_INFO("Refresh period updated to %.f seconds", WeatherRefreshN[0].value);
            // Restart the timer with the new period.
            if (timerID >= 0)
            {
                RemoveTimer(timerID);
                timerID = SetTimer(static_cast<int>(WeatherRefreshN[0].value * 1000)); // Convert seconds to milliseconds
            }
            return true;
        }
    }
    // Handle snooped GEOGRAPHIC_COORD data from the telescope.
    if (strcmp(name, "GEOGRAPHIC_COORD") == 0)
    {
        double lat = 0.0, lon = 0.0;
        bool latFound = false, lonFound = false;

        for (int i = 0; i < n; i++)
        {
            if (strcmp(names[i], "LAT") == 0)
            {
                lat = values[i];
                latFound = true;
            }
            else if (strcmp(names[i], "LONG") == 0)
            {
                lon = values[i];
                lonFound = true;
            }
        }

        if (latFound && lonFound)
        {
            LocationNP[LOCATION_LATITUDE].value = lat;
            LocationNP[LOCATION_LONGITUDE].value = lon;
            LocationNP.setState(IPS_OK);
            LocationNP.apply();
            locationReceived = true;
            LOGF_INFO("Snooped location from %s: Latitude=%.4f, Longitude=%.4f", dev, lat, lon);
            // Invalidate forecast when location updates.
            forecastValid = false;
        }
        else
        {
            LOGF_WARN("Snooped GEOGRAPHIC_COORD from %s incomplete: LAT=%s, LONG=%s",
                      dev, latFound ? "found" : "missing", lonFound ? "found" : "missing");
        }
        return true;
    }
    return INDI::Weather::ISNewNumber(dev, name, values, names, n);
}

// Handles updates to text properties (e.g., API key, telescope name).
bool AstrosphericWeather::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0)
    {
        // Handle updates to the API key.
        if (APIKeyTP.isNameMatch(name))
        {
            APIKeyTP.update(texts, names, n);
            APIKeyTP.setState(IPS_OK);
            APIKeyTP.apply();
            saveConfig(true, APIKeyTP.getName());
            forecastValid = false;
            LOGF_INFO("API Key updated: %s", APIKeyTP[0].getText());
            return true;
        }
        // Handle updates to the telescope name for snooping.
        if (TelescopeNameTP.isNameMatch(name))
        {
            TelescopeNameTP.update(texts, names, n);
            TelescopeNameTP.setState(IPS_OK);
            TelescopeNameTP.apply();
            saveConfig(true, TelescopeNameTP.getName());
            if (TelescopeNameTP[0].getText() && strlen(TelescopeNameTP[0].getText()) > 0)
            {
                IDSnoopDevice(TelescopeNameTP[0].getText(), "GEOGRAPHIC_COORD");
                LOGF_INFO("Now snooping on telescope %s for location data.", TelescopeNameTP[0].getText());
            }
            return true;
        }
    }
    return INDI::Weather::ISNewText(dev, name, texts, names, n);
}

// Handles updates to switch properties (e.g., mode switch).
bool AstrosphericWeather::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0)
    {
        if (ModeSP.isNameMatch(name))
        {
            ModeSP.update(states, names, n);
            ModeSP.setState(IPS_OK);
            ModeSP.apply();
            LOGF_INFO("Mode updated to: %s",
                      ModeSP[0].getState() == ISS_ON ? "API Mode" : "Simulated Mode");
            // Invalidate forecast when mode changes.
            forecastValid = false;
            return true;
        }
    }
    return INDI::Weather::ISNewSwitch(dev, name, states, names, n);
}

// Saves configuration items to a file.
bool AstrosphericWeather::saveConfigItems(FILE *fp)
{
    INDI::Weather::saveConfigItems(fp);
    APIKeyTP.save(fp);
    LocationNP.save(fp);
    TelescopeNameTP.save(fp);
    ModeSP.save(fp);
    return true;
}

// Syncs location by checking snooped data.
void AstrosphericWeather::syncLocationFromSite()
{
    if (locationReceived)
    {
        LOGF_INFO("Using location: Latitude=%.4f, Longitude=%.4f",
                  LocationNP[LOCATION_LATITUDE].getValue(),
                  LocationNP[LOCATION_LONGITUDE].getValue());
    }
    else
    {
        LOG_INFO("Waiting for location data to be received...");
    }
}

// Fetches data from the Astrospheric API.
bool AstrosphericWeather::fetchDataFromAPI(std::string &responseBody)
{
    LOG_INFO("Fetching data from Astrospheric API...");
    const std::string endpoint = "/api/GetForecastData_V1";
    const std::string host = "astrosphericpublicaccess.azurewebsites.net";

    // Get latitude and longitude from LocationNP
    double lat = LocationNP[LOCATION_LATITUDE].getValue();
    double lon = LocationNP[LOCATION_LONGITUDE].getValue();

    // Convert longitude from [0, 360] to [-180, 180] for Astrospheric API
    if (lon > 180.0)
    {
        lon -= 360.0;
    }

    LOGF_DEBUG("Sending coordinates to API: Latitude=%.4f, Longitude=%.4f", lat, lon);

    json payload;
    payload["Latitude"] = lat;
    payload["Longitude"] = lon;
    payload["APIKey"] = APIKeyTP[0].getText();
    std::string jsonDataString = payload.dump();

    httplib::Client client(host.c_str());
    auto res = client.Post(endpoint.c_str(), jsonDataString, "application/json");
    if (!res || res->status != 200)
    {
        LOGF_ERROR("API request failed: %d - %s", res ? res->status : -1, res ? res->body.c_str() : "No response");
        return false;
    }

    responseBody = res->body;
    LOGF_DEBUG("API response: %s", responseBody.c_str());
    return true;
}

// Parses the JSON response from the API.
bool AstrosphericWeather::parseJSONResponse(const std::string &jsonResponse)
{
    LOG_INFO("Parsing JSON response...");
    try
    {
        json j = json::parse(jsonResponse);
        std::string utcStartTimeStr = j["UTCStartTime"].get<std::string>();
        forecastStartTime = parseUTCDateTime(utcStartTimeStr);
        if (forecastStartTime == static_cast<time_t>(-1))
        {
            LOG_ERROR("Failed to parse UTCStartTime.");
            return false;
        }

        apiCreditsUsed = j["APICreditUsedToday"].get<int>();
        LOGF_INFO("API credits used today: %d", apiCreditsUsed);

        cloudCover.clear();
        temperature.clear();
        windSpeed.clear();
        dewPoint.clear();
        windDirection.clear();
        seeing.clear();
        transparency.clear();

        for (const auto &hour : j["RDPS_CloudCover"]) cloudCover.push_back(hour["Value"]["ActualValue"].get<double>());
        for (const auto &hour : j["RDPS_Temperature"]) temperature.push_back(hour["Value"]["ActualValue"].get<double>() - 273.15);
        for (const auto &hour : j["RDPS_WindVelocity"]) windSpeed.push_back(hour["Value"]["ActualValue"].get<double>() * 3.6);
        for (const auto &hour : j["RDPS_DewPoint"]) dewPoint.push_back(hour["Value"]["ActualValue"].get<double>() - 273.15);
        for (const auto &hour : j["RDPS_WindDirection"]) windDirection.push_back(hour["Value"]["ActualValue"].get<double>());
        for (const auto &hour : j["Astrospheric_Seeing"]) seeing.push_back(hour["Value"]["ActualValue"].get<double>());
        for (const auto &hour : j["Astrospheric_Transparency"]) transparency.push_back(hour["Value"]["ActualValue"].get<double>());

        forecastHours = cloudCover.size();
        if (forecastHours != 82 || temperature.size() != 82 || windSpeed.size() != 82 ||
            dewPoint.size() != 82 || windDirection.size() != 82 || seeing.size() != 82 ||
            transparency.size() != 82)
        {
            LOGF_ERROR("Forecast data length mismatch: %d hours.", forecastHours);
            return false;
        }

        forecastValid = true;
        lastFetchTime = time(nullptr);
        LOGF_INFO("Parsed forecast for %d hours starting at %s.", forecastHours, utcStartTimeStr.c_str());
        return true;
    }
    catch (const json::exception &e)
    {
        LOGF_ERROR("JSON parsing error: %s", e.what());
        return false;
    }
}

// Parses UTC date-time strings into time_t.
time_t AstrosphericWeather::parseUTCDateTime(const std::string &dateTimeStr)
{
    std::tm tm = {};
    std::istringstream ss(dateTimeStr);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail()) return static_cast<time_t>(-1);
#if defined(_WIN32)
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

// Updates weather data based on mode (API or Simulated).
IPState AstrosphericWeather::updateWeather()
{
    LOG_INFO("Updating weather...");
    if (!isConnected())
    {
        LOG_ERROR("Not connected. Please connect the device first.");
        return IPS_ALERT;
    }

    // Wait for location data before proceeding
    if (!locationReceived)
    {
        LOG_INFO("Waiting for location data...");
        return IPS_BUSY;
    }

    if (ModeSP[0].getState() == ISS_ON) // API Mode
    {
        if (APIKeyTP[0].getText() == nullptr || strlen(APIKeyTP[0].getText()) == 0)
        {
            LOG_ERROR("API key is not set. Set it in the Options tab.");
            return IPS_ALERT;
        }
        if (LocationNP[LOCATION_LATITUDE].getValue() == 0.0 && LocationNP[LOCATION_LONGITUDE].getValue() == 0.0)
        {
            LOG_ERROR("Location is not set. Set it in the Options tab or ensure a telescope is providing location data.");
            return IPS_ALERT;
        }

        time_t currentTime = time(nullptr);
        if (!forecastValid || (currentTime - lastFetchTime) > (6 * 3600))
        {
            LOG_INFO("Fetching new forecast data...");
            std::string responseBody;
            if (!fetchDataFromAPI(responseBody) || !parseJSONResponse(responseBody))
            {
                LOG_ERROR("Failed to fetch or parse forecast data.");
                return IPS_ALERT;
            }
        }

        time_t now = time(nullptr);
        int hourOffset = (now - forecastStartTime) / 3600;
        if (hourOffset < 0 || hourOffset >= forecastHours)
        {
            LOGF_ERROR("Current time outside forecast range. Offset: %d", hourOffset);
            forecastValid = false;
            return IPS_ALERT;
        }

        setParameterValue("WEATHER_CLOUD_COVER", cloudCover[hourOffset]);
        setParameterValue("WEATHER_TEMPERATURE", temperature[hourOffset]);
        setParameterValue("WEATHER_WIND_SPEED", windSpeed[hourOffset]);
        setParameterValue("WEATHER_DEW_POINT", dewPoint[hourOffset]);
        setParameterValue("WEATHER_WIND_DIRECTION", windDirection[hourOffset]);
        setParameterValue("WEATHER_SEEING", seeing[hourOffset]);
        setParameterValue("WEATHER_TRANSPARENCY", transparency[hourOffset]);

        // Update the weather summary in the Main Control tab
        char summary[128];
        snprintf(summary, sizeof(summary),
                 "Cloud: %.2f%%, Temp: %.2fC, Wind: %.2fkph, Dew: %.2fC, Dir: %.2f°, See: %.2f, Trans: %.2f",
                 cloudCover[hourOffset], temperature[hourOffset], windSpeed[hourOffset],
                 dewPoint[hourOffset], windDirection[hourOffset], seeing[hourOffset], transparency[hourOffset]);
        IUSaveText(&WeatherSummaryT[0], summary);
        WeatherSummaryTP.s = IPS_OK;
        IDSetText(&WeatherSummaryTP, nullptr);

        LOGF_INFO("Weather updated for hour %d: Cloud=%.2f%%, Temp=%.2fC, Wind=%.2fkph",
                  hourOffset, cloudCover[hourOffset], temperature[hourOffset], windSpeed[hourOffset]);

        // Notify the client of updated weather parameters
        ParametersNP.setState(IPS_OK);
        ParametersNP.apply();

        return IPS_OK;
    }
    else // Simulated Mode
    {
        LOG_INFO("Updating weather in simulated mode...");
        setParameterValue("WEATHER_CLOUD_COVER", 50.0);
        setParameterValue("WEATHER_TEMPERATURE", 20.0);
        setParameterValue("WEATHER_WIND_SPEED", 10.0);
        setParameterValue("WEATHER_DEW_POINT", 10.0);
        setParameterValue("WEATHER_WIND_DIRECTION", 180.0);
        setParameterValue("WEATHER_SEEING", 2.5);
        setParameterValue("WEATHER_TRANSPARENCY", 15.0);

        // Update the weather summary in the Main Control tab
        char summary[128];
        snprintf(summary, sizeof(summary),
                 "Cloud: 50.0%%, Temp: 20.0C, Wind: 10.0kph, Dew: 10.0C, Dir: 180.0°, See: 2.5, Trans: 15.0");
        IUSaveText(&WeatherSummaryT[0], summary);
        WeatherSummaryTP.s = IPS_OK;
        IDSetText(&WeatherSummaryTP, nullptr);

        // Notify the client of updated weather parameters
        ParametersNP.setState(IPS_OK);
        ParametersNP.apply();

        return IPS_OK;
    }
}

// Timer callback for periodic updates.
void AstrosphericWeather::TimerHit()
{
    if (!isConnected())
        return;

    updateWeather();

    // Reschedule the next update.
    timerID = SetTimer(static_cast<int>(WeatherRefreshN[0].value * 1000)); // Convert seconds to milliseconds
}

// Global unique pointer to the AstrosphericWeather instance.
static std::unique_ptr<AstrosphericWeather> weather(new AstrosphericWeather());

// Factory function to create the device instance.
extern "C" INDI::DefaultDevice *createDevice()
{
    return weather.get();
}
