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
    // Set the version of the driver.
    setVersion(0, 2);
    // Set the connection type to none, as this is a weather driver without a physical connection.
    setWeatherConnection(CONNECTION_NONE);
    // Initialize forecast-related variables.
    forecastValid = false;
    lastFetchTime = 0;
    forecastHours = 0;
    forecastStartTime = 0;
    apiCreditsUsed = 0;
}

// Method to return the default name of the device.
const char *AstrosphericWeather::getDefaultName()
{
    return "Astrospheric Weather";
}

// Connect method to handle the connection to the device.
bool AstrosphericWeather::Connect()
{
    LOG_INFO("AstrosphericWeather: Connect() called.");
    // Set the device state to connected (IPS_OK).
    setConnected(true);
    // Update properties to define them for the client.
    updateProperties();
    return true;
}

// Disconnect method to handle the disconnection from the device.
bool AstrosphericWeather::Disconnect()
{
    LOG_INFO("AstrosphericWeather: Disconnect() called.");
    // Set the device state to disconnected (IPS_IDLE).
    setConnected(false);
    // Update properties to remove them as needed.
    updateProperties();
    return true;
}

// Method to initialize the properties of the device.
bool AstrosphericWeather::initProperties()
{
    // Call the base class method to initialize standard weather properties.
    INDI::Weather::initProperties();

    // Define the API key property.
    APIKeyTP[0].fill("API_KEY_VALUE", "Key", "");
    APIKeyTP.fill(getDeviceName(), "ASTROSPHERIC_API_KEY", "API Key", "Options", IP_RW, 60, IPS_IDLE);

    // Define the control weather property with six elements.
    ControlWeatherNP[CONTROL_WEATHER].fill("Weather", "Weather", "%.f", 0, 1, 1, 0);
    ControlWeatherNP[CONTROL_TEMPERATURE].fill("Temperature", "Temperature", "%.2f", -50, 70, 10, 15);
    ControlWeatherNP[CONTROL_HUMIDITY].fill("Humidity", "Humidity", "%.f", 0, 100, 5, 0);
    ControlWeatherNP[CONTROL_WIND].fill("Wind", "Wind", "%.2f", 0, 100, 5, 0);
    ControlWeatherNP[CONTROL_GUST].fill("Gust", "Gust", "%.2f", 0, 50, 5, 0);
    ControlWeatherNP[CONTROL_RAIN].fill("Precip", "Precip", "%.f", 0, 100, 10, 0);
    ControlWeatherNP.fill(getDeviceName(), "WEATHER_CONTROL", "Control", MAIN_CONTROL_TAB, IP_RW, 0, IPS_IDLE);

    // Define the mode switch property with two options.
    ModeSP[MODE_API].fill("API_MODE", "API Mode", ISS_OFF);
    ModeSP[MODE_SIMULATED].fill("SIMULATED_MODE", "Simulated Mode", ISS_ON);
    ModeSP.fill(getDeviceName(), "WEATHER_MODE", "Mode", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    // Add weather parameters that will be displayed in the weather tab.
    addParameter("WEATHER_CLOUD_COVER", "Cloud Cover (%)", 0, 100, 50);
    addParameter("WEATHER_TEMPERATURE", "Temperature (C)", -50, 50, 0);
    addParameter("WEATHER_WIND_SPEED", "Wind Speed (kph)", 0, 200, 50);
    addParameter("WEATHER_DEW_POINT", "Dew Point (C)", -50, 50, 0);
    addParameter("WEATHER_WIND_DIRECTION", "Wind Direction (°)", 0, 360, 0);
    addParameter("WEATHER_SEEING", "Seeing (0–5)", 0, 5, 0);
    addParameter("WEATHER_TRANSPARENCY", "Transparency (0–27+)", 0, 30, 0);

    // Set the critical parameter for weather alerts.
    setCriticalParameter("WEATHER_CLOUD_COVER");

    // Add debug control for logging.
    addDebugControl();

    return true;
}

// Method to update properties based on the connection state.
bool AstrosphericWeather::updateProperties()
{
    // Call the base class method to handle standard property updates.
    INDI::Weather::updateProperties();

    if (isConnected())
    {
        // Define properties when connected.
        defineProperty(APIKeyTP);
        defineProperty(ControlWeatherNP);
        defineProperty(ModeSP);
    }
    else
    {
        // Delete properties when disconnected.
        deleteProperty(APIKeyTP);
        deleteProperty(ControlWeatherNP);
        deleteProperty(ModeSP);
    }

    return true;
}

// Method to handle new number property values.
bool AstrosphericWeather::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (ControlWeatherNP.isNameMatch(name))
        {
            // Update the control weather property with new values.
            ControlWeatherNP.update(values, names, n);
            ControlWeatherNP.setState(IPS_OK);
            ControlWeatherNP.apply();
            LOG_INFO("AstrosphericWeather ControlWeatherNP updated.");
            return true;
        }
    }

    // Call the base class method for other number properties.
    return INDI::Weather::ISNewNumber(dev, name, values, names, n);
}

// Method to handle new text property values.
bool AstrosphericWeather::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0)
    {
        if (APIKeyTP.isNameMatch(name))
        {
            // Update the API key property with the new value.
            APIKeyTP.update(texts, names, n);
            APIKeyTP.setState(IPS_OK);
            APIKeyTP.apply();
            // Save the configuration.
            saveConfig(true, APIKeyTP.getName());
            forecastValid = false;
            return true;
        }
    }

    // Call the base class method for other text properties.
    return INDI::Weather::ISNewText(dev, name, texts, names, n);
}

// Method to handle new switch property values.
bool AstrosphericWeather::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0)
    {
        if (ModeSP.isNameMatch(name))
        {
            // Update the mode switch property.
            ModeSP.update(states, names, n);
            ModeSP.setState(IPS_OK);
            ModeSP.apply();
            LOG_INFO("AstrosphericWeather ModeSP updated.");
            return true;
        }
    }

    // Call the base class method for other switch properties.
    return INDI::Weather::ISNewSwitch(dev, name, states, names, n);
}

// Method to save configuration items to a file.
bool AstrosphericWeather::saveConfigItems(FILE *fp)
{
    // Call the base class method to save standard configuration items.
    INDI::Weather::saveConfigItems(fp);
    // Save the API key, control weather, and mode properties.
    APIKeyTP.save(fp);
    ControlWeatherNP.save(fp);
    ModeSP.save(fp);
    return true;
}

// Method to update the weather data.
IPState AstrosphericWeather::updateWeather()
{
    if (ModeSP[MODE_API].getState() == ISS_ON)
    {
        // API Mode: Fetch real data (to be implemented).
        LOG_INFO("AstrosphericWeather: updateWeather() called in API mode (to be implemented).");
        return IPS_BUSY;
    }
    else
    {
        // Simulated Mode: Use control values.
        LOG_INFO("AstrosphericWeather: updateWeather() called in simulated mode.");
        // Set weather parameter values based on control property.
        setParameterValue("WEATHER_CLOUD_COVER", ControlWeatherNP[CONTROL_WEATHER].getValue());
        setParameterValue("WEATHER_TEMPERATURE", ControlWeatherNP[CONTROL_TEMPERATURE].getValue());
        setParameterValue("WEATHER_HUMIDITY", ControlWeatherNP[CONTROL_HUMIDITY].getValue());
        setParameterValue("WEATHER_WIND_SPEED", ControlWeatherNP[CONTROL_WIND].getValue());
        setParameterValue("WEATHER_WIND_GUST", ControlWeatherNP[CONTROL_GUST].getValue());
        setParameterValue("WEATHER_RAIN_HOUR", ControlWeatherNP[CONTROL_RAIN].getValue());
        return IPS_OK;
    }
}

// Stubbed method to fetch data from the Astrospheric API.
bool AstrosphericWeather::fetchDataFromAPI(std::string &responseBody)
{
    (void)responseBody;
    LOG_INFO("AstrosphericWeather::fetchDataFromAPI called (stubbed).");
    return false;
}

// Stubbed method to parse the JSON response from the API.
bool AstrosphericWeather::parseJSONResponse(const std::string &jsonResponse)
{
    (void)jsonResponse;
    LOG_INFO("AstrosphericWeather::parseJSONResponse called (stubbed).");
    return false;
}

// Static method to parse UTC date-time strings into time_t.
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

// Global unique pointer to the AstrosphericWeather instance.
static std::unique_ptr<AstrosphericWeather> weather(new AstrosphericWeather());

// Factory function to create the device instance.
extern "C" INDI::DefaultDevice *createDevice()
{
    return weather.get();
}
