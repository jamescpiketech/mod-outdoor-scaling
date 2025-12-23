# mod-outdoor-scaling

Outdoor-only HP/Damage scaling for AzerothCore continents (Vanilla, BC, WotLK) with per-zone and per-creature overrides plus commands `.os mapstat` and `.os creaturestat` to inspect active multipliers.

## Installing
1. Copy `modules/mod-outdoor-scaling` into your `azerothcore-wotlk/modules` directory.
2. Copy `conf/mod_outdoor_scaling.conf.dist` next to your other module configs and rename to `mod_outdoor_scaling.conf`.
3. Edit the config to set continent defaults and any zone/creature overrides.
4. Rebuild worldserver.

## Commands
- `.os mapstat` — shows scaling for your current map/zone.
- `.os creaturestat` — shows scaling for the selected creature.
