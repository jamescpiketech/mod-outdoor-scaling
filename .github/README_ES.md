# mod-outdoor-scaling

Escalado solo exterior de salud/daño para AzerothCore por continente (Vanilla, BC, WotLK), con overrides por zona y criatura y comandos `.os mapstat` y `.os creaturestat` para ver los multiplicadores activos.

## Instalación
1. Copia `modules/mod-outdoor-scaling` en tu carpeta `azerothcore-wotlk/modules`.
2. Copia `conf/mod_outdoor_scaling.conf.dist` junto a tus otros configs de módulos y renómbralo a `mod_outdoor_scaling.conf`.
3. Edita el config para establecer valores por continente y overrides de zona/criatura.
4. Recompila worldserver.

## Comandos
- `.os mapstat` — muestra el escalado del mapa/zona actual.
- `.os creaturestat` — muestra el escalado de la criatura seleccionada.
