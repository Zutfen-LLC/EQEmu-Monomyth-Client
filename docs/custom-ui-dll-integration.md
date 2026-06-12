# Custom UI Integration Guide

This document captures the UI contract required by the Monomyth custom DLL so other skins can adopt the same features without rediscovering the seam work.

The two surfaces that matter today are:

- `PlayerWindow` for multiclass mana visibility
- `PetInfoWindow` for the multi-pet rows

If a skin omits the required child IDs, the client may still run, but it will emit UI errors and some features will not initialize correctly.

## Required XML Contract

### `PlayerWindow`

Keep both sets of controls:

- visible multiclass controls:
  - `MM_Player_Mana`
  - `MM_Player_ManaLabel`
  - `MM_Player_ManaPercLabel`
- stock compatibility controls:
  - `PlayerMana`
  - `ManaLabel`
  - `ManPercLabel`

Rules:

- Do not rename the `ScreenID` values above.
- Keep `EQType 2` on the mana gauge.
- Keep `EQType 6668` on the custom mana label.
- The stock compatibility controls can be hidden or parked offscreen, but they must exist so the client can resolve them during UI init.
- The `MM_*` controls remain the visible multiclass mana surface.

### `PetInfoWindow`

Keep the stock pet window plus the multi-pet additions:

- `MMPIW_ExtraPet0_Gauge`
- `MMPIW_ExtraPet1_Gauge`

Also preserve the existing stock pet child IDs used by the window:

- `Pet0_Button`

Rules:

- Do not rename the extra gauge `ScreenID` values.
- Keep the extra gauges in the `PetInfoWindow` pieces list.
- Leave the stock pet rows intact so the custom DLL can bind the extra rows to the existing pet window lifecycle.

## Skin File Layout

Custom skins should include the same XML files the client expects from the active UI folder:

- `EQUI.xml`
- `EQUI_PlayerWindow.xml`
- `EQUI_PetInfoWindow.xml`

If your skin is derived from `default_new_pet_window`, make sure those files are present in that folder and that `EQUI.xml` includes them in the composite list.

If you build a skin from an older base, copy the current `Default` versions of `PlayerWindow` and `PetInfoWindow` instead of trying to reconstruct the pieces from memory.

## What Not To Change

- Do not remove the `MM_*` mana controls just to satisfy the stock child lookup.
- Do not rename the `MMPIW_ExtraPet0_Gauge` and `MMPIW_ExtraPet1_Gauge` ScreenIDs.
- Do not rely on the old `No Pet` placeholder text for the extra pet rows.
- Do not treat `PetInfoWindow` as item inspection or `/who` related UI. This guide is only about the mana and pet window seams.

## Validation Checklist

After copying the XML into a custom skin:

1. Launch the client with the custom skin active.
2. Reload the UI with `/loadskin <skinname> 1`.
3. Check `UIErrors.txt` and confirm there are no missing-child errors for:
   - `PlayerMana`
   - `ManaLabel`
   - `ManPercLabel`
4. Open `PetInfoWindow` and confirm the two extra pet rows appear.
5. Check `monomyth-client.log` for the expected multi-pet traces:
   - `MultiPetExtraPetGaugeTextUpdate`
   - `MultiPetExtraPetGaugeEqTypeTrace`
   - `ServerAuthStats valid=true`
6. Damage the non-focused pets and confirm the auxiliary bars move.

## Troubleshooting

- If the client shows the red XML compatibility warning, the active skin is missing one of the stock `PlayerWindow` child IDs.
- If the extra pet rows disappear, the active skin is missing the `MMPIW_ExtraPet0_Gauge` or `MMPIW_ExtraPet1_Gauge` pieces.
- If the bars exist but names do not update, check `monomyth-client.log` for the `MultiPetExtraPetGaugeTextUpdate` lines and confirm the skin is loading the intended `EQUI_PetInfoWindow.xml`.
- If you are unsure which folder the client is loading, inspect the `Reading UI data from UIFiles\... directory` line in the game log.

## Reference Files

- [`uifiles/default/EQUI_PlayerWindow.xml`](../uifiles/default/EQUI_PlayerWindow.xml)
- [`uifiles/default/EQUI_PetInfoWindow.xml`](../uifiles/default/EQUI_PetInfoWindow.xml)
- [`HANDOFF.md`](../HANDOFF.md)
- [`docs/multiclass-negative-results.md`](./multiclass-negative-results.md)
