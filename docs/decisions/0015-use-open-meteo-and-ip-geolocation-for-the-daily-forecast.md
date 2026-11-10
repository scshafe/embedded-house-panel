<!-- @canonical daily-forecast-sources-decision -->

# ADR 0015: Use Open-Meteo and an IP-Geolocation Service for the Daily Forecast

**Status:** accepted. The owner accepted the five recommended answers of the
weather plan (`.claude/plans/2026-10-18-daily-weather-and-sun-times.md`,
"Owner decisions") on 2026-11-10, relayed by the oversight session.

**Date:** 2026-11-10

## Context

The Home bar draws a day's high and low, but only from scenario fixtures.
The panel has no forecast source, no location of its own for weather, and no
text input. It may not invent a plausible value (AGENTS.md). The supervisor
already has a configured location for day and night, which it treats as
sensitive. The UI may not change supervisor policy (ADR 0004).

## Decision

1. **Forecast and place search: Open-Meteo, with no key.**
   - The forecast is `api.open-meteo.com/v1/forecast`:
     - the day's maximum and minimum temperature, sunrise and sunset;
     - `timezone=auto`, so the times are in the place's own local time.
   - The search is `geocoding-api.open-meteo.com/v1/search`, at most five
     results. Every result is a coordinate the forecast serves.
   - Their terms name personal home automation as free use. The data is CC BY
     4.0, so the weather menu shows "Weather data by Open-Meteo.com".
2. **Automatic location: IP geolocation through `free.freeipapi.com`.**
   - Pinned on 2026-11-10 by the probe in
     `hardware_tests/results/weather/source-probe-20261110/`:
     - HTTPS, no key or account;
     - "a free service for commercial and non-commercial uses";
     - 60 requests a minute.
   - The two others were not pinned:
     - ipapi.co's free tier is a trial "not meant for use in production";
     - ipinfo.io's free plan is country-level only and needs a token.
   - The lookup sends the panel's public IP address to that service. It runs
     only while "Automatic" is selected: at start and every 6 h.
   - The menu calls it "Automatic (approximate)".
3. **Day and night stay on the supervisor's configured location.** The
   weather location is a display preference only.
   - If the owner picks another city, its sunrise and sunset can differ from
     when the panel turns red.
   - Moving the supervisor's location would need its own reviewed path.
4. **Search input: the renderer's own on-screen QWERTY keyboard,** drawn from
   scene state like every other surface. LVGL's keyboard widget is not used,
   so the goldens stay deterministic. Keys are at least about 64 px.
5. **Defaults:**
   - °F, high and low, Automatic;
   - refresh every 60 min and on any settings change;
   - "stale" after 3 h without a refresh;
   - blank (em dash) once the cached forecast is not for today in the
     place's own time zone, or is older than 12 h.

## Privacy

- **Rounding:** coordinates are rounded to 0.01° (about 1 km) before any
  request or write.
- **Never recorded:** coordinates and the public IP never appear in logs,
  diagnostics, status output, fixtures or Git. Logs carry an error code and
  a host name only.
- **The place name** is shown, never logged.
- **Fixtures** use a public place (Boulder, Colorado) and the documentation
  address `192.0.2.1`.

## Consequences

- **Network:** the panel makes up to one forecast request an hour, one IP
  lookup every 6 h and searches on demand, all over HTTPS to three
  third-party hosts. A free service's terms may change; the
  automatic-location service is pinned in one place, and choosing a place
  works without it.
- **The bar** shows live values only from a parsed, fresh answer. Anything
  else is an em dash or the "Weather stale" label, with a `WEATHER.*` error
  code.
- **The bar's clock** remains the preview clock until AGENDA-011 is decided.
  Sun times are in the place's local time.
