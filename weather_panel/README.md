# Weather Panel

<!-- @canonical weather_panel -->

The Home bar's daily forecast, the sun times and the weather menu's
settings.
- **The decision:**
  [ADR 0015](../docs/decisions/0015-use-open-meteo-and-ip-geolocation-for-the-daily-forecast.md).
- **The plan:** `.claude/plans/2026-10-18-daily-weather-and-sun-times.md`.
- **Where it sits:** a feature domain beneath `panel_ui`, beside
  `vocabulary_panel`. It runs inside the UI process, off the LVGL thread.

**Status (2026-11-10):** the decisions and the fixtures are recorded (plan
Steps 1–2). The models, parsers, store, client and screens are not built
yet.

## Data contract

- **`WeatherSettings`:**
  - **unit:** `fahrenheit`, `celsius` or `kelvin`; default `fahrenheit`;
  - **bar:** `temperatures` or `sun_times`; default `temperatures`;
  - **location:** `automatic` (the default), or
    `place{name, region, country, latitude, longitude}`, with coordinates
    rounded to 0.01°.
- **`DailyForecast`:**
  - the local date, as `YYYY-MM-DD` in the place's time zone;
  - the IANA time zone;
  - the high and low in °C, to one decimal;
  - sunrise and sunset as local `HH:MM`;
  - the fetch time in UTC.
- **Display:**
  - The colors follow `temperature_color()` on the Celsius value in every
    unit.
  - Kelvin shows as `295 K`.
  - Sun times show as `↑ 7:12` / `↓ 18:21`.
  - Night mode hides the bar.

## Freshness

| Age, or date | The bar |
|---|---|
| Under 3 h, and today in the place's zone | The values |
| 3 h to 12 h, and still today | The values and "Weather stale" |
| Over 12 h, or a different local date | Em dash (blank) |

**Refresh:** every 60 min, on any settings change, and when the place's local
date changes. After a failure, the wait backs off from 1 min to 30 min.
Automatic location is looked up at start and every 6 h, only while selected.

## Sources (pinned)

| Use | Endpoint | Key |
|---|---|---|
| Forecast | `https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=auto&forecast_days=1` | none |
| Search | `https://geocoding-api.open-meteo.com/v1/search?name=..&count=5&language=en` | none |
| Automatic | `https://free.freeipapi.com/api/v1/json` | none |

The fixtures in `tests/fixtures/` are the recorded answers. No test reaches
the internet.

## Error codes (planned)

- `WEATHER.FORECAST_INVALID.<FIELD>`, `WEATHER.SEARCH_INVALID.<FIELD>` and
  `WEATHER.LOCATION_INVALID.<FIELD>`: an answer that failed the strict
  parser. A missing field, a wrong type, an out-of-range value or an array of
  the wrong length all fail, and no value is guessed.
- `WEATHER.NETWORK_UNAVAILABLE`, `WEATHER.TIMEOUT`, `WEATHER.HTTP_STATUS`
  and `WEATHER.TLS_FAILED`: transport failures, with the host name only.
- `WEATHER.STORE_INVALID`: a settings or forecast file that is oversized,
  corrupt or of an unknown schema.

## Privacy

- Coordinates are rounded before any request or write.
- Coordinates and the public IP never appear in logs, diagnostics, status
  output or fixtures.
- The place name is shown, never logged.
