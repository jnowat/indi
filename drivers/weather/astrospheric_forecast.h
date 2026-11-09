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

#pragma once

// Pure (no INDI, no network) parsing of the Astrospheric v2 GetForecastData
// response. Split out of indi_astrospheric_weather.cpp so it can be unit tested.

#include <ctime>
#include <string>
#include <vector>

namespace astrospheric
{

struct Forecast
{
    time_t startTime {static_cast<time_t>(-1)};
    std::string startTimeStr;           // UTCForecastHour of the first entry, as sent

    // Converted to driver units: cloud %, temperature/dew point C, wind kph,
    // wind direction deg, seeing 0-5, transparency 0-27+.
    std::vector<double> cloudCover, temperature, windSpeed, dewPoint,
        windDirection, seeing, transparency;

    // Informational credit fields; hasCredits is true only when the response
    // carries a numeric APICreditsRemaining.
    bool hasCredits {false};
    int  creditCost {0};
    int  creditsRemaining {0};

    size_t hours() const { return cloudCover.size(); }
};

// Parses "YYYY-MM-DDTHH:MM:SSZ" as UTC. Returns (time_t)-1 on failure.
time_t parseUTCDateTime(const std::string &dateTimeStr);

// Parses a GetForecastData response body. On success fills `out` (all vectors
// the same, non-zero length, covering the leading run of consecutive whole
// hours) and returns true. On failure returns false, sets `error` to a
// human-readable message, and leaves the forecast vectors of `out` empty.
// Credit fields may be filled even on failure.
bool parseForecast(const std::string &body, Forecast &out, std::string &error);

} // namespace astrospheric
