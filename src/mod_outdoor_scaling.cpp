/*
 * Outdoor scaling module:
 * - Per-continent defaults (Vanilla/BC/WotLK) for outdoor creatures only.
 * - Optional per-zone and per-creature overrides that trump continent values.
 * - Commands: .os mapstat / .os creaturestat to inspect current multipliers.
 */

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DataMap.h"
#include "Map.h"
#include "ScriptMgr.h"
#include "Unit.h"
#include "World.h"

#include <cmath>
#include <limits>
#include <array>
#include <sstream>
#include <string>
#include <unordered_map>

namespace
{
    struct OutdoorScalingInfo : public DataMap::Base
    {
        float healthMult = 1.0f;
        float damageMult = 1.0f;
        uint32 zoneId = 0;
        uint8 expansion = 0;
        enum class Source
        {
            None,
            Continent,
            ZoneOverride,
            CreatureOverride,
            Disabled,
            NotOutdoor,
            PetOrGuardian
        } source = Source::None;
    };

    struct OutdoorScalingConfig
    {
        bool enabled = true;
        std::array<float, 3> continentHealth{ {1.0f, 1.0f, 1.0f} };
        std::array<float, 3> continentDamage{ {1.0f, 1.0f, 1.0f} };
        std::unordered_map<uint32, std::pair<float, float>> zoneOverrides;
        std::unordered_map<uint32, std::pair<float, float>> creatureOverrides;
    };

    OutdoorScalingConfig gConfig;

    bool IsOutdoorContinent(Map const* map)
    {
        if (!map)
            return false;

        MapEntry const* entry = map->GetEntry();
        if (!entry)
            return false;

        return !map->Instanceable() && entry->IsContinent();
    }

    uint8 ClampExpansion(uint8 expansion)
    {
        return expansion > 2 ? 2 : expansion;
    }

    std::unordered_map<uint32, std::pair<float, float>> ParseOverrideString(std::string const& input)
    {
        std::unordered_map<uint32, std::pair<float, float>> overrides;

        std::string chunk;
        std::stringstream ss(input);

        while (std::getline(ss, chunk, ','))
        {
            std::stringstream line(chunk);
            std::string idStr;
            std::string hpStr;
            std::string dmgStr;

            line >> idStr >> hpStr >> dmgStr;

            if (idStr.empty() || hpStr.empty())
                continue;

            uint32 id = 0;
            float hp = 0.0f;
            float dmg = 0.0f;

            try
            {
                id = uint32(std::stoul(idStr));
                hp = std::stof(hpStr);
                dmg = dmgStr.empty() ? hp : std::stof(dmgStr);
            }
            catch (...)
            {
                continue;
            }

            if (hp <= 0.0f || dmg <= 0.0f)
                continue;

            overrides[id] = { hp, dmg };
        }

        return overrides;
    }

    struct ScalingResult
    {
        float healthMult = 1.0f;
        float damageMult = 1.0f;
        OutdoorScalingInfo::Source source = OutdoorScalingInfo::Source::None;
        uint32 zoneId = 0;
        uint8 expansion = 0;
    };

    ScalingResult ComputeScaling(Map* map, uint32 zoneId, uint32 creatureEntry, bool isPetOrGuardian)
    {
        ScalingResult result;
        result.zoneId = zoneId;

        if (!gConfig.enabled)
        {
            result.source = OutdoorScalingInfo::Source::Disabled;
            return result;
        }

        if (!map || !IsOutdoorContinent(map))
        {
            result.source = OutdoorScalingInfo::Source::NotOutdoor;
            return result;
        }

        if (isPetOrGuardian)
        {
            result.source = OutdoorScalingInfo::Source::PetOrGuardian;
            return result;
        }

        MapEntry const* entry = map->GetEntry();
        result.expansion = entry ? ClampExpansion(entry->Expansion()) : 0;

        auto creatureIt = gConfig.creatureOverrides.find(creatureEntry);
        if (creatureIt != gConfig.creatureOverrides.end())
        {
            result.healthMult = creatureIt->second.first;
            result.damageMult = creatureIt->second.second;
            result.source = OutdoorScalingInfo::Source::CreatureOverride;
            return result;
        }

        auto zoneIt = gConfig.zoneOverrides.find(zoneId);
        if (zoneIt != gConfig.zoneOverrides.end())
        {
            result.healthMult = zoneIt->second.first;
            result.damageMult = zoneIt->second.second;
            result.source = OutdoorScalingInfo::Source::ZoneOverride;
            return result;
        }

        result.healthMult = gConfig.continentHealth[result.expansion];
        result.damageMult = gConfig.continentDamage[result.expansion];
        result.source = OutdoorScalingInfo::Source::Continent;

        return result;
    }

    float GetWorldHealthRate(Creature const* creature)
    {
        if (!creature)
            return 1.0f;

        switch (creature->GetCreatureTemplate()->rank)
        {
            case CREATURE_ELITE_NORMAL:      return sWorld->getRate(RATE_CREATURE_NORMAL_HP);
            case CREATURE_ELITE_ELITE:       return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP);
            case CREATURE_ELITE_RAREELITE:   return sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_HP);
            case CREATURE_ELITE_WORLDBOSS:   return sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_HP);
            case CREATURE_ELITE_RARE:        return sWorld->getRate(RATE_CREATURE_ELITE_RARE_HP);
            default:                         return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP);
        }
    }

    float GetWorldDamageRate(Creature const* creature)
    {
        if (!creature)
            return 1.0f;

        switch (creature->GetCreatureTemplate()->rank)
        {
            case CREATURE_ELITE_NORMAL:      return sWorld->getRate(RATE_CREATURE_NORMAL_DAMAGE);
            case CREATURE_ELITE_ELITE:       return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_DAMAGE);
            case CREATURE_ELITE_RAREELITE:   return sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_DAMAGE);
            case CREATURE_ELITE_WORLDBOSS:   return sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE);
            case CREATURE_ELITE_RARE:        return sWorld->getRate(RATE_CREATURE_ELITE_RARE_DAMAGE);
            default:                         return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_DAMAGE);
        }
    }

    float GetWorldSpellDamageRate(Creature const* creature)
    {
        if (!creature)
            return 1.0f;

        switch (creature->GetCreatureTemplate()->rank)
        {
            case CREATURE_ELITE_NORMAL:      return sWorld->getRate(RATE_CREATURE_NORMAL_SPELLDAMAGE);
            case CREATURE_ELITE_ELITE:       return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE);
            case CREATURE_ELITE_RAREELITE:   return sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE);
            case CREATURE_ELITE_WORLDBOSS:   return sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE);
            case CREATURE_ELITE_RARE:        return sWorld->getRate(RATE_CREATURE_ELITE_RARE_SPELLDAMAGE);
            default:                         return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE);
        }
    }

    std::string SourceToString(OutdoorScalingInfo::Source source)
    {
        switch (source)
        {
            case OutdoorScalingInfo::Source::Continent:       return "Continent default";
            case OutdoorScalingInfo::Source::ZoneOverride:    return "Zone override";
            case OutdoorScalingInfo::Source::CreatureOverride:return "Creature override";
            case OutdoorScalingInfo::Source::Disabled:        return "Module disabled";
            case OutdoorScalingInfo::Source::NotOutdoor:      return "Not outdoor continent";
            case OutdoorScalingInfo::Source::PetOrGuardian:   return "Pet/guardian excluded";
            default:                                          return "Not scaled";
        }
    }

    class OutdoorScaling_WorldScript : public WorldScript
    {
    public:
        OutdoorScaling_WorldScript() : WorldScript("OutdoorScaling_WorldScript") { }

        void OnBeforeConfigLoad(bool /*reload*/) override
        {
            gConfig.enabled = sConfigMgr->GetOption<bool>("OutdoorScaling.Enable", true);

            gConfig.continentHealth[0] = sConfigMgr->GetOption<float>("OutdoorScaling.Continent.0.Health", 1.0f);
            gConfig.continentHealth[1] = sConfigMgr->GetOption<float>("OutdoorScaling.Continent.1.Health", 1.0f);
            gConfig.continentHealth[2] = sConfigMgr->GetOption<float>("OutdoorScaling.Continent.2.Health", 1.0f);

            gConfig.continentDamage[0] = sConfigMgr->GetOption<float>("OutdoorScaling.Continent.0.Damage", 1.0f);
            gConfig.continentDamage[1] = sConfigMgr->GetOption<float>("OutdoorScaling.Continent.1.Damage", 1.0f);
            gConfig.continentDamage[2] = sConfigMgr->GetOption<float>("OutdoorScaling.Continent.2.Damage", 1.0f);

            gConfig.zoneOverrides = ParseOverrideString(sConfigMgr->GetOption<std::string>("OutdoorScaling.ZoneOverrides", "", false));
            gConfig.creatureOverrides = ParseOverrideString(sConfigMgr->GetOption<std::string>("OutdoorScaling.CreatureOverrides", "", false));
        }
    };

    class OutdoorScaling_AllCreatureScript : public AllCreatureScript
    {
    public:
        OutdoorScaling_AllCreatureScript() : AllCreatureScript("OutdoorScaling_AllCreatureScript") { }

        void ApplyDamageScale(Creature* creature, float mult) const
        {
            if (std::abs(mult - 1.0f) < std::numeric_limits<float>::epsilon())
                return;

            float worldDamageRate = GetWorldDamageRate(creature);
            if (worldDamageRate <= 0.0f)
                worldDamageRate = 1.0f;

            float baseMin = creature->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
            float baseMax = creature->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);
            float offMin  = creature->GetWeaponDamageRange(OFF_ATTACK, MINDAMAGE);
            float offMax  = creature->GetWeaponDamageRange(OFF_ATTACK, MAXDAMAGE);
            float rngMin  = creature->GetWeaponDamageRange(RANGED_ATTACK, MINDAMAGE);
            float rngMax  = creature->GetWeaponDamageRange(RANGED_ATTACK, MAXDAMAGE);

            float baseMinNorm = baseMin / worldDamageRate;
            float baseMaxNorm = baseMax / worldDamageRate;
            float offMinNorm  = offMin  / worldDamageRate;
            float offMaxNorm  = offMax  / worldDamageRate;
            float rngMinNorm  = rngMin  / worldDamageRate;
            float rngMaxNorm  = rngMax  / worldDamageRate;

            creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, baseMinNorm * mult);
            creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, baseMaxNorm * mult);
            creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, offMinNorm * mult);
            creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, offMaxNorm * mult);
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, rngMinNorm * mult);
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, rngMaxNorm * mult);
        }

        void OnCreatureSelectLevel(const CreatureTemplate* /*cinfo*/, Creature* creature) override
        {
            if (!creature)
                return;

            OutdoorScalingInfo* info = creature->CustomData.GetDefault<OutdoorScalingInfo>("OutdoorScalingInfo");
            ScalingResult scaling = ComputeScaling(creature->GetMap(), creature->GetZoneId(), creature->GetEntry(), creature->IsPet() || creature->IsGuardian());

            info->healthMult = scaling.healthMult;
            info->damageMult = scaling.damageMult;
            info->zoneId = scaling.zoneId;
            info->expansion = scaling.expansion;
            info->source = scaling.source;

            if (scaling.source != OutdoorScalingInfo::Source::Continent &&
                scaling.source != OutdoorScalingInfo::Source::ZoneOverride &&
                scaling.source != OutdoorScalingInfo::Source::CreatureOverride)
            {
                return;
            }

            float worldHealthRate = GetWorldHealthRate(creature);
            if (worldHealthRate <= 0.0f)
                worldHealthRate = 1.0f;

            if (std::abs(scaling.healthMult - 1.0f) >= std::numeric_limits<float>::epsilon())
            {
                float baseHealth = float(creature->GetMaxHealth()) / worldHealthRate;
                uint32 newMaxHealth = uint32(baseHealth * scaling.healthMult);
                if (!newMaxHealth)
                    newMaxHealth = 1;

                creature->SetCreateHealth(newMaxHealth);
                creature->SetMaxHealth(newMaxHealth);
                creature->SetHealth(newMaxHealth);
                creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(newMaxHealth));
            }

            ApplyDamageScale(creature, scaling.damageMult);
        }
    };

    class OutdoorScaling_UnitScript : public UnitScript
    {
    public:
        OutdoorScaling_UnitScript() : UnitScript("OutdoorScaling_UnitScript", true) { }

        void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
        {
            if (!attacker || damage == 0 || !attacker->IsCreature())
                return;

            Creature* creature = attacker->ToCreature();
            ScalingResult scaling = ComputeScaling(creature->GetMap(), creature->GetZoneId(), creature->GetEntry(), creature->IsPet() || creature->IsGuardian());

            OutdoorScalingInfo* info = creature->CustomData.GetDefault<OutdoorScalingInfo>("OutdoorScalingInfo");
            info->healthMult = scaling.healthMult;
            info->damageMult = scaling.damageMult;
            info->zoneId = scaling.zoneId;
            info->expansion = scaling.expansion;
            info->source = scaling.source;

            if (scaling.source != OutdoorScalingInfo::Source::Continent &&
                scaling.source != OutdoorScalingInfo::Source::ZoneOverride &&
                scaling.source != OutdoorScalingInfo::Source::CreatureOverride)
            {
                return;
            }

            if (std::abs(scaling.damageMult - 1.0f) < std::numeric_limits<float>::epsilon())
                return;

            float worldSpellRate = GetWorldSpellDamageRate(creature);
            if (worldSpellRate <= 0.0f)
                worldSpellRate = 1.0f;

            damage = int32((float(damage) / worldSpellRate) * scaling.damageMult);
            if (damage == 0)
                damage = (scaling.damageMult > 0.0f ? 1 : 0);
        }
    };

    class OutdoorScaling_CommandScript : public CommandScript
    {
    public:
        OutdoorScaling_CommandScript() : CommandScript("OutdoorScaling_CommandScript") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable osCommandTable =
            {
                { "mapstat",      HandleOSMapStat,      SEC_PLAYER, Console::Yes },
                { "creaturestat", HandleOSCreatureStat, SEC_PLAYER, Console::Yes }
            };

            static ChatCommandTable rootTable =
            {
                { "outdoorscaling", osCommandTable },
                { "os",             osCommandTable },
            };

            return rootTable;
        }

        static bool HandleOSMapStat(ChatHandler* handler, char const* /*args*/)
        {
            Player* player = handler->GetPlayer();
            Map* map = player->GetMap();
            uint32 zoneId = player->GetZoneId();
            ScalingResult scaling = ComputeScaling(map, zoneId, 0, false);

            handler->PSendSysMessage("---");
            handler->PSendSysMessage("{} (Map {}), Zone {}", map->GetMapName(), map->GetId(), zoneId);

            if (scaling.source == OutdoorScalingInfo::Source::Disabled)
            {
                handler->PSendSysMessage("Outdoor scaling is disabled.");
                return true;
            }

            if (scaling.source == OutdoorScalingInfo::Source::NotOutdoor)
            {
                handler->PSendSysMessage("Outdoor scaling not active on this map.");
                return true;
            }

            handler->PSendSysMessage("Continent base (exp {}): HP x{:.2f}, Damage x{:.2f}",
                scaling.expansion,
                gConfig.continentHealth[scaling.expansion],
                gConfig.continentDamage[scaling.expansion]);

            auto zoneIt = gConfig.zoneOverrides.find(zoneId);
            if (zoneIt != gConfig.zoneOverrides.end())
            {
                handler->PSendSysMessage("Zone override: HP x{:.2f}, Damage x{:.2f}",
                    zoneIt->second.first, zoneIt->second.second);
            }
            else
            {
                handler->PSendSysMessage("Zone override: none");
            }

            handler->PSendSysMessage("Active outdoor scaling: HP x{:.2f}, Damage x{:.2f} ({})",
                scaling.healthMult, scaling.damageMult, SourceToString(scaling.source));

            return true;
        }

        static bool HandleOSCreatureStat(ChatHandler* handler, char const* /*args*/)
        {
            Creature* creature = handler->getSelectedCreature();
            if (!creature)
            {
                handler->SendSysMessage(LANG_SELECT_CREATURE);
                handler->SetSentErrorMessage(true);
                return false;
            }

            Map* map = creature->GetMap();
            if (map->Instanceable())
            {
                handler->PSendSysMessage("Outdoor scaling not active inside instances.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            OutdoorScalingInfo* info = creature->CustomData.GetDefault<OutdoorScalingInfo>("OutdoorScalingInfo");
            ScalingResult scaling = ComputeScaling(map, creature->GetZoneId(), creature->GetEntry(), creature->IsPet() || creature->IsGuardian());

            info->healthMult = scaling.healthMult;
            info->damageMult = scaling.damageMult;
            info->zoneId = scaling.zoneId;
            info->expansion = scaling.expansion;
            info->source = scaling.source;

            handler->PSendSysMessage("---");
            handler->PSendSysMessage("{} (Entry {}), Zone {}, Map {}",
                creature->GetName(), creature->GetEntry(), creature->GetZoneId(), map->GetId());

            handler->PSendSysMessage("Continent base (exp {}): HP x{:.2f}, Damage x{:.2f}",
                scaling.expansion,
                gConfig.continentHealth[scaling.expansion],
                gConfig.continentDamage[scaling.expansion]);

            auto zoneIt = gConfig.zoneOverrides.find(creature->GetZoneId());
            if (zoneIt != gConfig.zoneOverrides.end())
            {
                handler->PSendSysMessage("Zone override: HP x{:.2f}, Damage x{:.2f}",
                    zoneIt->second.first, zoneIt->second.second);
            }
            else
            {
                handler->PSendSysMessage("Zone override: none");
            }

            auto creatureIt = gConfig.creatureOverrides.find(creature->GetEntry());
            if (creatureIt != gConfig.creatureOverrides.end())
            {
                handler->PSendSysMessage("Creature override: HP x{:.2f}, Damage x{:.2f}",
                    creatureIt->second.first, creatureIt->second.second);
            }
            else
            {
                handler->PSendSysMessage("Creature override: none");
            }

            handler->PSendSysMessage("Active outdoor scaling: HP x{:.2f}, Damage x{:.2f} ({})",
                info->healthMult, info->damageMult, SourceToString(info->source));

            return true;
        }
    };
}

void AddOutdoorScalingScripts()
{
    new OutdoorScaling_WorldScript();
    new OutdoorScaling_AllCreatureScript();
    new OutdoorScaling_UnitScript();
    new OutdoorScaling_CommandScript();
}
