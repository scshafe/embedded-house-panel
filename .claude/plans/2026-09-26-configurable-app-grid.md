# Plan: Configurable app grid

**Goal:** configure the Home app catalog and automatically lay out/paginate future apps.
**Out of scope:** implementing new apps, arbitrary executable loading, live providers or hardware evidence.

## Context

The current two-app Home is approved. Preserve that presentation while replacing
its two-item branches with a catalog-driven grid. A startup JSON catalog supplies
order, labels, visibility, icon/style and grid dimensions. Launch destinations
remain canonical intent-registry metadata; configuration cannot invent routes.
Use built-in book/calendar icons or a generic monogram for future applications.
A paged grid preserves touch target sizes and the permanent daytime bar.

## Steps

- [x] **Step 1: Define the catalog and grid contract**
  - Add closed bounded configuration, canonical navigation destinations and
    automatic page geometry. Reject unknown routes and invalid catalogs.
  - Verify: `ctest --test-dir .build/panel-headless -R panel_application_grid --output-on-failure`

- [x] **Step 2: Render and navigate catalog pages**
  - Wire the catalog into all simulation entry points; render icons and hit
    targets from one layout. Preserve study/lifecycle behavior and document
    how to add a future feature at the existing shell extension points.
  - Verify: `make verify`

- [ ] **Step 3: Verify all display tiers and publish evidence**
  - Confirm the approved default images remain unchanged, test a populated
    multi-page grid, and leave updated previews running. Make local commits.
  - Verify: `make verify-panel-simulator`
  - Verify: `make verify-panel-vm`
  - Verify: `make verify-docs`

## Done criteria

Future app metadata and grid shape are declarative. Reordering/hiding existing
apps needs only configuration; an implemented new app needs registration, not
launcher rendering edits. Multi-page routing and safe lifecycle behavior pass.
No pushes or hardware claims.
