# Weather fixtures

Recorded on 2026-11-10 for weather plan Step 2. No test reaches the internet;
the fixture client serves these.

- `open-meteo-forecast-boulder.json`: the daily forecast for Boulder, Colorado
  (40.01, -105.27), from `api.open-meteo.com` (CC BY 4.0, "Weather data by
  Open-Meteo.com").
- `open-meteo-search-boulder.json` / `open-meteo-search-none.json`: place
  searches from `geocoding-api.open-meteo.com`. With no match the answer has
  no `results` key at all.
- `freeipapi-boulder.json`: the shape of the pinned automatic-location
  answer (`free.freeipapi.com/api/v1/json`), written by hand. The address is
  the documentation address `192.0.2.1`, and the place is Boulder; no real
  answer from the panel's network is stored. `timeZones` lists the country's
  zones, so the panel takes the zone from the forecast instead.
