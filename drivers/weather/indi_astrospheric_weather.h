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

#ifndef INDI_ASTROSPHERIC_WEATHER_H
#define INDI_ASTROSPHERIC_WEATHER_H

#include <indiweather.h>
#include <vector>
#include <string>
#include <ctime>

// AstrosphericWeather class inherits from INDI::Weather, providing the base functionality for weather drivers in INDI.
class AstrosphericWeather : public INDI::Weather
{
public:
    // Constructor for the AstrosphericWeather class.
    AstrosphericWeather();
    // Virtual destructor (default implementation).
    virtual ~AstrosphericWeather() = default;

    // Override of the getDefaultName method to return the name of the device.
    virtual const char *getDefaultName() override;

    // Override of initProperties to initialize the properties of the device.
    virtual bool initProperties() override;
    // Override of updateProperties to manage the properties when the device is connected or disconnected.
    virtual bool updateProperties() override;

    // Override of ISNewText to handle changes to text properties (e.g., API key).
    virtual bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;
    // Override of ISNewNumber to handle changes to number properties (e.g., control weather values).
    virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;
    // Override of ISNewSwitch to handle changes to switch properties (e.g., mode selection).
    virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;

    // Override of saveConfigItems to save the configuration of the device.
    virtual bool saveConfigItems(FILE *fp) override;

    // Override of Connect to handle the connection to the device.
    virtual bool Connect() override;
    // Override of Disconnect to handle the disconnection from the device.
    virtual bool Disconnect() override;

protected:
    // Override of updateWeather to update the weather data based on the current mode (API or simulated).
    virtual IPState updateWeather() override;

private:
    // Property for the API key, which is a text input.
    INDI::PropertyText APIKeyTP{1};
    // Property for controlling weather parameters in simulated mode, with 6 number elements.
    INDI::PropertyNumber ControlWeatherNP{6};
    // Property for selecting the mode (API or simulated), with 2 switch elements.
    INDI::PropertySwitch ModeSP{2};

    // Enumeration for the indices of the ControlWeatherNP property.
    enum
    {
        CONTROL_WEATHER,      // Weather condition
        CONTROL_TEMPERATURE,  // Temperature
        CONTROL_HUMIDITY,     // Humidity
        CONTROL_WIND,         // Wind speed
        CONTROL_GUST,         // Wind gust
        CONTROL_RAIN          // Precipitation
    };

    // Enumeration for the indices of the ModeSP property.
    enum
    {
        MODE_API,             // API mode for real data
        MODE_SIMULATED        // Simulated mode for manual control
    };

    // Vectors to store forecast data (not currently used in the provided code).
    std::vector<double> cloudCover, temperature, windSpeed, dewPoint, windDirection, seeing, transparency;
    time_t forecastStartTime;  // Start time of the forecast
    int forecastHours;         // Number of hours in the forecast
    bool forecastValid;        // Flag indicating if the forecast is valid
    time_t lastFetchTime;      // Last time data was fetched
    int apiCreditsUsed;        // Number of API credits used

    // Method to fetch data from the Astrospheric API (stubbed).
    bool fetchDataFromAPI(std::string &responseBody);
    // Method to parse the JSON response from the API (stubbed).
    bool parseJSONResponse(const std::string &jsonResponse);
    // Static method to parse UTC date-time strings.
    static time_t parseUTCDateTime(const std::string &dateTimeStr);
};

#endif // INDI_ASTROSPHERIC_WEATHER_H
