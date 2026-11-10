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
Steps 1–2). The domain is built and tested (Steps 3–4). The bar and the menu
are drawn and routed (Step 5). Wiring the live source into the panel's
processes is Step 6.

## On the panel (Step 5)

- **The day bar.** It shows the high and low in °F, °C or K, or the sunrise
  and sunset (`↑ 6:40` / `↓ 16:48`, with drawn arrows). A tap on it opens
  the `weather` surface from any day surface.
- **The menu rows:**
  - Units: °F, °C, K;
  - Show: High & low, Sunrise & sunset;
  - Location: Automatic, or Choose a place….

  Then Done, and the attribution "Weather data by Open-Meteo.com".
- **The search.**
  - A drawn QWERTY keyboard with 84 × 64 px keys, plus hyphen and
    apostrophe.
  - Up to five places, one row each.
  - The other states: no results, offline (the search failed), and
    Automatic unavailable.
- **Intents:** all are `weather.*`, besides `shell.open_weather`.
  - `weather.location.automatic` and `weather.search.submit` are
    `ExternalSubmission`: the first sends the panel's address to the pinned
    service, the second the typed name.
  - The others are local navigation.

## Pieces

- **`weather_codec.h`** (`@canonical weather_codec`):
  - strict parsers for the three answers;
  - the display rules: °F/°C/K, `↑ 7:12`;
  - freshness, judged in the place's own date through the forecast's UTC
    offset;
  - the store's encodings.
- **`weather_schedule.h`:** when to look up and when to fetch. It is pure,
  with the time injected.
- **`weather_store.h`:** `settings.json` and `forecast.json` through
  `bhp::durable_store::DurableFile` (0600 in 0700, 16 KiB each).
- **`weather_client.h`:**
  - `HttpWeatherClient`, HTTPS only. A test CA is accepted only with
    `https://127.0.0.1` endpoints. It logs the host and a code.
  - `FixtureWeatherClient`, which serves the recorded answers.
- **`weather_worker.h`:** the weather's own thread.
  - The UI reads `snapshot()` and `take_search()`, and calls
    `update_settings()` and `search()`. None of them waits on the network.
  - A forecast is tied to the rounded coordinates it was fetched for, so it
    is never shown for another place.

## Tests

`ctest --test-dir .build/host-dev -R weather`:
- **The codec:** every fixture, each mutated field with its exact code, the
  rounding edges (−0.4, 0.5, −40), and freshness at 3 h, at a new local date,
  at UTC+14 and from the future.
- **The store:** round trip, modes, corrupt and oversized files.
- **The schedule:** 60 min, 6 h, a new date, and the backoff 1, 2, 4, 8, 16
  and 30 min with jitter.
- **The worker:** Automatic then a place, the newest search, failures and
  backoff, a restart, Automatic unavailable, and a log free of coordinates,
  the IP and names.
- **The HTTPS client:** against a loopback TLS server, covering success,
  timeout, 429, 500, 404, malformed JSON, an untrusted certificate and the
  endpoint rules, with the same log check.

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
  - Sun times show as `↑ 7:12` / `↓ 18:21`. The renderer draws the
    arrows, because the panel's font subset has none; `sun_time_text` gives
    `7:12`.
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
