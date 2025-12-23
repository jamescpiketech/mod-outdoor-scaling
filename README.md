# mod-outdoor-scaling

Outdoor-only creature HP and Damage scaling for AzerothCore, split by expansion continents (Vanilla, BC, WotLK). Instances, battlegrounds, arenas, and pets/guardians are untouched. The module ignores any `Rate.Creature.*` multipliers in `worldserver.conf` (it normalizes them out) and applies only its own settings.

Design notes: heavily inspired by Autobalance for config/commands, but focused solely on outdoor creature stat multipliers by continent/zone/creature instead of instance scaling. (https://github.com/azerothcore/mod-autobalance)

## Continent scaling
- Per-expansion defaults (Vanilla, BC, WotLK) for outdoor continents only.

## Zone scaling (override)
- Optional per-zone multipliers that trump continent defaults.

## Creature scaling (override)
- Optional per-creature multipliers that trump zone and continent settings.

## Commands
- `.os mapstat` — show active scaling for current map/zone.
- `.os creaturestat` — show active scaling for selected creature.

## Installation
1. Copy `modules/mod-outdoor-scaling` into your `azerothcore-wotlk/modules` directory.
2. Copy `conf/mod_outdoor_scaling.conf.dist` alongside your other module configs and rename to `mod_outdoor_scaling.conf`.
3. Edit the config: set continent defaults, add any zone/creature overrides.
4. Rebuild worldserver.
