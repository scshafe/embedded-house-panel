# Plan: Live Daily Weather, Sun Times and a Weather Menu

<!-- @canonical daily-weather-plan -->

**Status (2026-10-18):** Proposed; waits for the owner's answers under "Owner
decisions".

**Goal:**
- **Live forecast:** the Home bar shows today's high and low from a live
  forecast, color-coded by value.
- **Menu:** tapping them opens a menu that can:
  - switch units between °F, °C and K;
  - switch the bar between high/low and sunrise/sunset;
  - set the location automatically (approximate, from the panel's internet
    address);
  - pick a named place by searching.
- **Free sources:** every place the search offers can be synced freely,
  with no account or key.

**Out of scope:**
- the household clock and timezone (AGENDA-011; the bar's clock stays the
  preview clock);
- hourly or multi-day forecasts;
- weather icons and alerts;
- changing the supervisor's day/night location (see decision 3);
- the production system image and its services.

## Starting point

- **Rendering exists.** The Home bar already draws High and Low at the upper
  right (`panel_ui/src/lvgl_panel_display.cpp`).
  - Colors come from `temperature_color()`, by Celsius value: blue ≤ 0,
    cyan ≤ 10, green ≤ 20, amber ≤ 30, coral above.
  - It converts to °F in `temperature_text()`.
  - Missing data shows an em dash, and stale data carries a "Weather stale"
    label.
- **The data is fixture-only.** `PanelScene` has `weather_available`,
  `weather_stale`, `weather_fixture`, `weather_high_celsius`,
  `weather_low_celsius` and `weather_temperature_unit`. Values come from
  scenario fixtures such as `simulator/scenarios/home-weather.json`.
  - The README states: "Weather values are explicit fixtures, not a live
    forecast."
  - The weather provider and location were left as an open question
    (`.claude/plans/2026-09-26-panel-home-and-reel-clock.md`).
- **The HTTP pieces exist.**
  - `shared/http_runtime/` (libcurl) provides the process-wide HTTP runtime.
  - `vocabulary_panel/` shows the pattern for a UI-side feature domain: a
    worker off the LVGL thread (`study_worker.cpp`), a bounded atomic store
    (`study_store.cpp`), and a fixture client for the simulator.
- **The board can do it.** The vendor image has `libcurl.so.4`, `libssl.so.3`
  and `/etc/ssl/certs/ca-certificates.crt`. A request from the board to
  `api.open-meteo.com` returned HTTP 200 (2026-10-18).
- **There is no text input yet.** `panel_ui/lv_conf.h` disables LVGL's
  keyboard, text area, list and button matrix. Every surface is drawn from
  scene state, so the goldens stay deterministic.
- **The supervisor's location is separate.** It computes day/night from
  `BHP_PANEL_SUPERVISOR_LATITUDE/LONGITUDE`, which it treats as sensitive and
  never logs.
  - The UI cannot change supervisor policy, and its channel is
    acknowledgement-only (ADR 0004).

## Sources

- **Forecast: Open-Meteo, no key.**
  - Request:
    `https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=auto&forecast_days=1`.
  - Terms: free for non-commercial use, and "personal home automation" is
    named as an example. Limits are 600 calls a minute and 10,000 a day; the
    data is CC BY 4.0.
  - It covers every coordinate on Earth, and `timezone=auto` returns sunrise
    and sunset in the place's own local time.
- **Place search: Open-Meteo geocoding, no key.**
  - Request:
    `https://geocoding-api.open-meteo.com/v1/search?name=..&count=5&language=en`.
  - It returns name, region, country and coordinates.
  - Every result it returns is a coordinate the forecast API serves, which
    meets the "only places we can sync" requirement.
- **Automatic location: an HTTPS IP-geolocation service with no key.**
  - Step 2 probes the candidates and pins one: ipapi.co, ipinfo.io and
    freeipapi.com.
  - ip-api.com is excluded, because its free tier is HTTP only.
  - Sources disagree on which of these still need a key in 2026, so the
    probe decides.
  - It is city-level and can be wrong behind a VPN or a carrier.
    The menu labels it "Automatic (approximate)".

## Owner decisions (recommended defaults; reply to change any)

1. **Forecast and search provider:** Open-Meteo, with its attribution in the
   menu ("Weather data by Open-Meteo.com", CC BY 4.0).
2. **Automatic location:** IP geolocation through the Step 2 service. It
   sends the panel's public IP address to that service, and the lookup happens
   only while "Automatic" is selected: at start and every 6 h.
3. **Day/night stays on the supervisor's configured location.** The weather
   location is a display preference only. If you pick another city, its
   sunrise and sunset can differ from when the panel turns red. Moving the
   supervisor's location would need a reviewed path, because the UI may not
   change policy.
4. **Search input:** a simple on-screen QWERTY keyboard that the renderer
   draws like the other surfaces, not LVGL's keyboard widget, so screens stay
   deterministic.
5. **Defaults:**
   - °F, high/low and Automatic;
   - refresh every 60 min and on any settings change;
   - "stale" after 3 h without a refresh;
   - blank (em dash) once the cached forecast is not for today in the place's
     own timezone, or is older than 12 h.

## Design

- **New feature domain `weather_panel/`, beneath `panel_ui`, a sibling of
  `vocabulary_panel`.** It adds a capability registry row: "Daily forecast,
  sun times and weather location preference". It owns:
  - `WeatherSettings`:
    - unit `fahrenheit|celsius|kelvin`;
    - bar `temperatures|sun_times`;
    - location `automatic` or `place{name, region, country, latitude,
      longitude}`.
  - `DailyForecast`:
    - local date, timezone;
    - high and low in °C (one decimal);
    - sunrise and sunset as local `HH:MM`;
    - fetch time.
  - Strict parsers for the three responses. Each rejects missing fields,
    wrong types, out-of-range values and arrays of the wrong length, and
    returns a typed `WEATHER.*` error. No plausible value is ever invented.
  - A store for the settings file and the last good forecast.
    - Bounded size, mode 0600.
    - Written with the vocabulary store's pattern: temporary file, fsync,
      rename, directory fsync.
  - A worker on `shared/http_runtime`, off the LVGL thread.
    - Timeouts and cancellation.
    - Exponential backoff from 1 to 30 min on failure.
    - Refresh at 60 min, on a settings change, and when the place's local
      date changes.
- **Privacy:**
  - Coordinates are rounded to 0.01° (about 1 km) before any request or
    write.
  - Coordinates and the public IP never appear in logs, diagnostics or
    status output. Logs carry only the error code and host.
  - The place name is shown, not logged.
- **Display:**
  - High/low keep `temperature_color()` on the Celsius value in every unit.
  - Kelvin shows as `295 K`.
  - Sun times show as `↑ 7:12` / `↓ 18:21`, in neutral text, in the place's
    local time.
  - Night mode still hides the bar.
- **Menu:** a modal sheet over Home, opened by a tap on the right side of the
  bar, under the shell's modal precedence. It contains:
  - a unit row (°F, °C, K);
  - a bar row (Temperatures, Sunrise & sunset);
  - a location row: Automatic (approximate), or "Choose a place…", which opens
    search with the keyboard and up to five results ("Boulder, Colorado,
    United States");
  - Done;
  - the attribution line.
  - The menu's actions are new namespaced `weather.*` intents in the closed
    registry.
- **Sources at run time:**
  - `bhp-panel-ui` gains `--weather-source fixture|live` and
    `--weather-state-directory DIR`.
  - The simulator and the VM guest use `fixture`. The fixture client serves
    the recorded responses from Step 2, so no test touches the internet.

## Steps

- [ ] **Step 1: Record the decisions.**
  - Add ADR NNNN "Use Open-Meteo and an IP-geolocation service for the daily
    forecast" with decisions 1–5, the privacy rules and the CC BY attribution.
    Allocate the number when the ADR is created.
  - Add the registry row in `docs/architecture/application-capability-ownership.md`.
  - Add `weather_panel/README.md` with the data contract, freshness bounds and
    error codes.
  - Amend the "explicit fixtures, not a live forecast" paragraph in
    `panel_ui/README.md`.
  - Verify:
    - `make verify` exit 0.
- [ ] **Step 2: Pin the automatic-location service and record fixtures.**
  - From the PC, `curl` each candidate's HTTPS JSON endpoint without a key.
    Record:
    - status;
    - fields (city, region, country, latitude, longitude);
    - the terms page.
  - Pin the first that is HTTPS, keyless and allows personal use.
  - Record sanitized response fixtures under `weather_panel/tests/fixtures/`.
    - The IP fixture's coordinates are replaced with a public city's, and
      its IP with `192.0.2.1`.
    - One Open-Meteo forecast and one geocoding search for a public place.
  - Verify:
    - the probe record under `hardware_tests/results/weather/source-probe-<UTC>/`
      names the pinned service;
    - `rg -n "<the PC's public IP>" weather_panel` finds nothing.
- [ ] **Step 3: Models, parsers, units and the store.**
  - Tests:
    - every parser accepts its fixture and rejects each mutated field with
      the exact `WEATHER.*` code;
    - °C/°F/K formatting and rounding at the edges (−0.4, 0.5, −40);
    - color bands unchanged in every unit;
    - freshness: fresh, stale at 3 h, unavailable at 12 h and after the
      place's local date changes;
    - the store round-trips, rejects files that are oversized or corrupt,
      and rounds coordinates.
  - Verify:
    - `ctest --test-dir .build/host-dev -R weather` exit 0.
- [ ] **Step 4: The live client and the worker.**
  - Add the libcurl client on `shared/http_runtime`, with the backoff and
    refresh schedule. Test it against a local TLS fixture server, as the
    vocabulary client's tests do. Cover:
    - success;
    - timeout;
    - HTTP 429 and 500;
    - malformed JSON;
    - TLS failure.
  - A log test proves coordinates and the IP never appear.
  - Verify:
    - `ctest --test-dir .build/host-dev -R weather` exit 0;
    - `make verify` exit 0.
- [ ] **Step 5: The bar and the menu.**
  - The bar renders temperatures or sun times from `WeatherSettings`. The tap
    target opens the sheet.
  - Add the unit, bar and location rows, the search screen with the drawn
    keyboard and results, and Automatic.
  - Add the `weather.*` intents.
  - New scenarios and goldens:
    - the menu;
    - Kelvin;
    - sun times;
    - search with results;
    - search with no results;
    - search offline;
    - Automatic unavailable.
  - Interaction tests: tapping each row changes the settings and the bar;
    picking a result stores the place and starts a refresh.
  - Verify:
    - `make verify-panel-goldens` exit 0;
    - `ctest --test-dir .build/host-dev -R "panel_ui|simulat|weather"` exit 0;
    - `make verify` exit 0.
- [ ] **Step 6: Wire it into the demo.**
  - `bhp-panel-ui` and `--simulation-drm` gain `--weather-source` and
    `--weather-state-directory`.
  - The VM guest passes `fixture`.
  - The FRDM demo passes `live` with `/tmp/bhp-demo/state/weather`.
  - Add the pinned libcurl development package to
    `scripts/build/frdm_vendor_sysroot.sh`, and `libcurl.so.4` to the demo's
    allowed libraries.
  - Verify:
    - `scripts/deploy/frdm_panel_demo.sh build --execute` exit 0;
    - `python3 -m unittest discover -s scripts/deploy/tests -p test_*.py` exit 0;
    - `make verify` exit 0.
- [ ] **Step 7: Check it on the panel with the owner.**
  - `start 180` with the live source. The owner checks, in order:
    1. high and low appear, in the right colors;
    2. a tap opens the menu;
    3. °C, K and °F change the bar;
    4. Sunrise & sunset swaps the bar;
    5. Automatic names your area;
    6. searching a city and picking it changes the values.
  - Compare the values with open-meteo.com for the same place.
  - Then `stop`.
  - Verify:
    - the owner's yes or no for each check;
    - the record under `hardware_tests/results/frdm/panel-demo-<UTC>/`.

## Risks

- **Automatic location can be wrong** (VPN, carrier routing), and the free
  services' terms change. Choosing a place is always available, and the
  service is pinned in one place.
- **Typing on a 7-inch panel is fiddly.** The keyboard keys must be at least
  about 64 px. Step 7 shows whether it is usable.
- **The rate limit is shared per IP.** At one request an hour plus searches,
  the panel uses a tiny share of 10,000 a day.
- **The sun times and the red night mode can disagree** when the weather
  place is not the panel's own location (decision 3).
- **The bar's clock is still the preview clock.** Sunrise and sunset show the
  place's real local time, which will not match it until AGENDA-011 is
  decided.
