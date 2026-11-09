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
*******************************************************************************/

#include "astrospheric_forecast.h"

#ifdef _USE_SYSTEM_JSONLIB
#include <nlohmann/json.hpp>
#else
#include <indijson.hpp>
#endif

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

using json = nlohmann::json;

namespace astrospheric
{

time_t parseUTCDateTime(const std::string &dateTimeStr)
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

bool parseForecast(const std::string &body, Forecast &out, std::string &error)
{
    out = Forecast();
    try
    {
        json j = json::parse(body);

        if (j.contains("ErrorInfo") && j["ErrorInfo"].is_string())
        {
            error = "Astrospheric API error: " + j["ErrorInfo"].get<std::string>();
            return false;
        }
        if (!j.contains("HourlyForecast") || !j["HourlyForecast"].is_array() || j["HourlyForecast"].empty())
        {
            error = "Unexpected API response: missing or empty 'HourlyForecast' array.";
            return false;
        }
        const auto &hourly = j["HourlyForecast"];

        std::string utcStartTimeStr = hourly[0].at("UTCForecastHour").get<std::string>();
        const time_t newStartTime = parseUTCDateTime(utcStartTimeStr);
        if (newStartTime == static_cast<time_t>(-1))
        {
            error = "Failed to parse UTCForecastHour.";
            return false;
        }

        // Informational only; must not fail the whole parse if absent.
        if (j.contains("APICreditsRemaining") && j["APICreditsRemaining"].is_number())
        {
            out.hasCredits       = true;
            out.creditCost       = j.value("APICreditCostOfCall", 0);
            out.creditsRemaining = j["APICreditsRemaining"].get<int>();
        }

        std::vector<double> newCloudCover, newTemperature, newWindSpeed,
            newDewPoint, newWindDirection, newSeeing, newTransparency;

        // The driver indexes forecasts by whole hours from the first entry. v2 returns
        // hourly steps first and then 3-hourly steps, so stop at the first gap.
        auto val = [](const json &hour, const char *key)
        {
            return hour.at(key).at("ActualValue").get<double>();
        };
        for (size_t i = 0; i < hourly.size(); i++)
        {
            const auto &hour = hourly[i];
            time_t t = parseUTCDateTime(hour.at("UTCForecastHour").get<std::string>());
            if (t != newStartTime + static_cast<time_t>(i) * 3600)
                break;
            newCloudCover.push_back(val(hour, "Cloud"));
            newTemperature.push_back(val(hour, "Temperature") - 273.15); // K -> C
            newWindSpeed.push_back(val(hour, "Wind") * 3.6);             // m/s -> kph
            newDewPoint.push_back(val(hour, "DewPoint") - 273.15);       // K -> C
            newWindDirection.push_back(val(hour, "WindDirection"));
            newSeeing.push_back(val(hour, "Seeing"));
            newTransparency.push_back(val(hour, "Transparency"));
        }

        // Accept the shortest vector length so all arrays index consistently.
        size_t actualHours = newCloudCover.size();
        for (size_t sz : {newTemperature.size(), newWindSpeed.size(), newDewPoint.size(),
                          newWindDirection.size(), newSeeing.size(), newTransparency.size()})
            actualHours = std::min(actualHours, sz);

        if (actualHours == 0)
        {
            error = "API returned empty forecast vectors.";
            return false;
        }

        newCloudCover.resize(actualHours);
        newTemperature.resize(actualHours);
        newWindSpeed.resize(actualHours);
        newDewPoint.resize(actualHours);
        newWindDirection.resize(actualHours);
        newSeeing.resize(actualHours);
        newTransparency.resize(actualHours);

        out.startTime     = newStartTime;
        out.startTimeStr  = std::move(utcStartTimeStr);
        out.cloudCover    = std::move(newCloudCover);
        out.temperature   = std::move(newTemperature);
        out.windSpeed     = std::move(newWindSpeed);
        out.dewPoint      = std::move(newDewPoint);
        out.windDirection = std::move(newWindDirection);
        out.seeing        = std::move(newSeeing);
        out.transparency  = std::move(newTransparency);
        return true;
    }
    catch (const json::exception &e)
    {
        error = std::string("JSON parsing error: ") + e.what();
        return false;
    }
}

} // namespace astrospheric
