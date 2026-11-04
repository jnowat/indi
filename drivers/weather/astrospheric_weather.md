# Astrospheric Weather driver (`indi_astrospheric_weather`)

INDI weather driver that pulls astronomy forecasts (cloud cover, seeing,
transparency, wind, temperature, dew point) from the Astrospheric API and
turns them into INDI weather parameters and a `WEATHER_STATUS` safety state.

## Setup

1. Get an Astrospheric API key (Astrospheric Pro account).
2. Provide the key through the environment of the process that runs
   `indiserver`:

   ```sh
   export ASTROSPHERIC_API_KEY=...   # set in your service/unit file, not in a repo
   indiserver indi_astrospheric_weather
   ```

   The driver reads `ASTROSPHERIC_API_KEY` from the environment at runtime.
   A key from the environment is masked in the client UI (shown as
   "set (from env)") and is never saved to the driver config file. If both an
   env key and a key typed into **Options > API Key** exist, the env key wins.
   Never commit a key to a repo, script or config that is shared.
3. Set the location: **Options > Location** (latitude/longitude), or enter a
   mount name in **Options > Snoop Telescope** to follow its
   `GEOGRAPHIC_COORD`. Ekos also pushes the location when a mount/GPS is active.
4. **Options > Mode**: *API Mode* for live data, *Simulated Mode* for testing
   (synthetic values; never use for real observatory safety).
5. Connect. **Refresh Now** forces an immediate fetch.

## API endpoint and credits

- Endpoint: `POST https://v2-api-public.astrospheric.com/api/GetForecastData`
  (HTTPS via libcurl; the old v1 host is disabled).
- Each call costs **15 credits**; the monthly allowance is **100,000 credits**,
  i.e. about 6,666 calls/month, or roughly one call every 6.7 minutes on
  average.
- The driver caches each forecast for **6 hours** (plus an early refresh near
  the end of the forecast window), and retries failed fetches at most every
  **5 minutes**. Normal use is therefore about 4 calls/day (~1,800 credits per
  month). The **Update Period** (default 1800 s) only re-evaluates cached data;
  it does not call the API each time.
- Repeated **Refresh Now** presses each cost 15 credits.
- After each call the driver logs credits used and remaining.

## Weather parameters

| Parameter | Units | Critical | Default OK range |
|---|---|---|---|
| `WEATHER_CLOUD_COVER` | % | yes | OK < 60, warning 60–80, Alert > 80 |
| `WEATHER_WIND_SPEED` | kph | yes | OK < 25, warning 25–40, Alert > 40 |
| `WEATHER_TEMPERATURE` | °C | no | -50 … 50 |
| `WEATHER_DEW_POINT` | °C | no | -50 … 50 |
| `WEATHER_WIND_DIRECTION` | ° | no | 0 … 360 |
| `WEATHER_SEEING` | 0–5 | no | 0 … 5 |
| `WEATHER_TRANSPARENCY` | 0–27+ | no | 0 … 30 |

Only the critical parameters drive the safety status. Thresholds are
adjustable per parameter on the **Parameters** tab (`MIN_OK`, `MAX_OK`,
`PERC_WARN`; the warning band is `(max-min)*perc/100` below `MAX_OK`).
Values saved in an existing config override these defaults.

The **Forecast** tab shows cloud cover, temperature, wind, seeing and
transparency for the next 24 h in 3-hour steps; the Main tab shows a summary,
the last fetch time and when the forecast expires.

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| "No API key configured" | `ASTROSPHERIC_API_KEY` not in the `indiserver` environment (check the service unit / Web Manager environment), or no key entered. |
| HTTP 403 | Invalid key or monthly credits exhausted. |
| "No location configured" | Set Options > Location or a Snoop Telescope. |
| "missing or empty 'HourlyForecast'" / parse errors | Unexpected API response; the driver keeps the last good forecast and retries after 5 min. Enable debug logging for details. |
| Status Alert with no data | Fetch failed and no cached forecast exists; check network/DNS/TLS from the INDI host. |
| Values look synthetic | Simulated Mode is selected. |
| Credits draining fast | Avoid repeated Refresh Now; check for several driver instances sharing one key. |
