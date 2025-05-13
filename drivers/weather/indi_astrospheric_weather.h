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

#ifndef INDI_ASTROSPHERIC_WEATHER_H
#define INDI_ASTROSPHERIC_WEATHER_H

#include <indiweather.h>
#include <indipropertytext.h>
#include <vector>
#include <string>
#include <ctime>

class AstrosphericWeather : public INDI::Weather
{
public:
    AstrosphericWeather();
    virtual ~AstrosphericWeather() = default;

    virtual const char *getDefaultName() override;
    virtual bool initProperties() override;
    virtual bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;
    virtual bool loadConfig(bool silent = false, const char *property = nullptr) override;
    virtual bool saveConfig(bool silent = false, const char *property = nullptr) override;

protected:
    virtual IPState updateWeather() override;

private:
    INDI::PropertyText APIKeyT[1];
    INDI::PropertyTextVectorProperty APIKeyTP;

    std::vector<double> cloudCover, temperature, windSpeed, dewPoint, windDirection, seeing, transparency;
    time_t forecastStartTime;
    int forecastHours;
    bool forecastValid;
    time_t lastFetchTime;
    int apiCreditsUsed;

    bool fetchDataFromAPI(std::string &responseBody);
    bool parseJSONResponse(const std::string &jsonResponse);
    static time_t parseUTCDateTime(const std::string& dateTimeStr);
};

#endif // INDI_ASTROSPHERIC_WEATHER_H