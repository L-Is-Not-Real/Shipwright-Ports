#include "mods.h"
#include <algorithm>
#include <libultraship/bridge.h>
#include "game-interactor/GameInteractor.h"
#include "soh/Enhancements/boss-rush/BossRush.h"
#include "soh/Enhancements/enhancementTypes.h"
#include <soh/Enhancements/item-tables/ItemTableManager.h>

extern "C" {
#include <z64.h>
#include "macros.h"
#include "soh/cvar_prefixes.h"
#include "variables.h"
#include "functions.h"

extern SaveContext gSaveContext;
extern PlayState* gPlayState;
void Player_RequestQuake(PlayState* play, s32 speed, s32 y, s32 countdown);
void Player_RequestRumble(Player* this, s32 sourceStrength, s32 duration, s32 decreaseRate, s32 distSq);
}

/// Switches Link's age and respawns him at the last entrance he entered.
void SwitchAge() {
    if (gPlayState == NULL)
        return;

    Player* player = GET_PLAYER(gPlayState);

    // Hyrule Castle: Very likely to fall through floor, so we force a specific entrance
    if (gPlayState->sceneNum == SCENE_HYRULE_CASTLE || gPlayState->sceneNum == SCENE_OUTSIDE_GANONS_CASTLE) {
        gPlayState->nextEntranceIndex = ENTR_CASTLE_GROUNDS_SOUTH_EXIT;
    } else {
        gSaveContext.respawnFlag = 1;
        gPlayState->nextEntranceIndex = gSaveContext.entranceIndex;

        // Preserve the player's position and orientation
        gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = gPlayState->nextEntranceIndex;
        gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex = gPlayState->roomCtx.curRoom.num;
        gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = player->actor.world.pos;
        gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = player->actor.shape.rot.y;

        if (gPlayState->roomCtx.curRoom.behaviorType2 < 4) {
            gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0x0DFF;
        } else {
            // Scenes with static backgrounds use a special camera we need to preserve
            Camera* camera = GET_ACTIVE_CAM(gPlayState);
            s16 camId = camera->camDataIdx;
            gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0x0D00 | camId;
        }
    }

    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
    gPlayState->linkAgeOnLoad ^= 1;

    // Discover adult/child spawns
    if (gPlayState->linkAgeOnLoad == LINK_AGE_ADULT) {
        Entrance_SetEntranceDiscovered(ENTR_HYRULE_FIELD_10, false);
    } else {
        Entrance_SetEntranceDiscovered(ENTR_LINKS_HOUSE_CHILD_SPAWN, false);
    }

    static HOOK_ID hookId = 0;
    hookId = REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
        *should = false;
        GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(hookId);
    });
}

/// Switches Link's age and respawns him at the last entrance he entered.
void RegisterOcarinaTimeTravel() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOcarinaSongAction>([]() {
        if (!GameInteractor::IsSaveLoaded(true) || !CVarGetInteger(CVAR_ENHANCEMENT("TimeTravel"), 0)) {
            return;
        }

        Actor* player = &GET_PLAYER(gPlayState)->actor;
        Actor* nearbyTimeBlockEmpty =
            Actor_FindNearby(gPlayState, player, ACTOR_OBJ_WARP2BLOCK, ACTORCAT_ITEMACTION, 300.0f);
        Actor* nearbyTimeBlock = Actor_FindNearby(gPlayState, player, ACTOR_OBJ_TIMEBLOCK, ACTORCAT_ITEMACTION, 300.0f);
        Actor* nearbyOcarinaSpot = Actor_FindNearby(gPlayState, player, ACTOR_EN_OKARINA_TAG, ACTORCAT_PROP, 120.0f);
        Actor* nearbyDoorOfTime = Actor_FindNearby(gPlayState, player, ACTOR_DOOR_TOKI, ACTORCAT_BG, 500.0f);
        Actor* nearbyFrogs = Actor_FindNearby(gPlayState, player, ACTOR_EN_FR, ACTORCAT_NPC, 300.0f);
        Actor* nearbyGossipStone = Actor_FindNearby(gPlayState, player, ACTOR_EN_GS, ACTORCAT_NPC, 300.0f);
        bool justPlayedSoT = gPlayState->msgCtx.lastPlayedSong == OCARINA_SONG_TIME;
        bool notNearAnySource = !nearbyTimeBlockEmpty && !nearbyTimeBlock && !nearbyOcarinaSpot && !nearbyDoorOfTime &&
                                !nearbyFrogs && !nearbyGossipStone;
        bool hasOcarinaOfTime = (INV_CONTENT(ITEM_OCARINA_TIME) == ITEM_OCARINA_TIME);
        bool hasMasterSword = CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER);
        int timeTravelSetting = CVarGetInteger(CVAR_ENHANCEMENT("TimeTravel"), 0);
        bool meetsTimeTravelRequirements = false;

        switch (timeTravelSetting) {
            case TIME_TRAVEL_ANY:
                meetsTimeTravelRequirements = true;
                break;
            case TIME_TRAVEL_ANY_MS:
                meetsTimeTravelRequirements = hasMasterSword;
                break;
            case TIME_TRAVEL_OOT_MS:
                meetsTimeTravelRequirements = hasMasterSword && hasOcarinaOfTime;
                break;
            case TIME_TRAVEL_OOT:
            default:
                meetsTimeTravelRequirements = hasOcarinaOfTime;
                break;
        }

        if (justPlayedSoT && notNearAnySource && meetsTimeTravelRequirements) {
            SwitchAge();
        }
    });
}

bool IsHyperBossesActive() {
    return CVarGetInteger(CVAR_ENHANCEMENT("HyperBosses"), 0) ||
           (IS_BOSS_RUSH &&
            gSaveContext.ship.quest.data.bossRush.options[BR_OPTIONS_HYPERBOSSES] == BR_CHOICE_HYPERBOSSES_YES);
}

void UpdateHyperBossesState() {
    static uint32_t actorUpdateHookId = 0;
    if (actorUpdateHookId != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnActorUpdate>(actorUpdateHookId);
        actorUpdateHookId = 0;
    }

    if (IsHyperBossesActive()) {
        actorUpdateHookId =
            GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorUpdate>([](void* refActor) {
                // Run the update function a second time to make bosses move and act twice as fast.

                Player* player = GET_PLAYER(gPlayState);
                Actor* actor = static_cast<Actor*>(refActor);

                uint8_t isBossActor = actor->id == ACTOR_BOSS_GOMA ||      // Gohma
                                      actor->id == ACTOR_BOSS_DODONGO ||   // King Dodongo
                                      actor->id == ACTOR_EN_BDFIRE ||      // King Dodongo Fire Breath
                                      actor->id == ACTOR_BOSS_VA ||        // Barinade
                                      actor->id == ACTOR_BOSS_GANONDROF || // Phantom Ganon
                                      actor->id == ACTOR_EN_FHG_FIRE || // Phantom Ganon/Ganondorf Energy Ball/Thunder
                                      actor->id == ACTOR_EN_FHG ||      // Phantom Ganon's Horse
                                      actor->id == ACTOR_BOSS_FD ||
                                      actor->id == ACTOR_BOSS_FD2 ||   // Volvagia (grounded/flying)
                                      actor->id == ACTOR_EN_VB_BALL || // Volvagia Rocks
                                      actor->id == ACTOR_BOSS_MO ||    // Morpha
                                      actor->id == ACTOR_BOSS_SST ||   // Bongo Bongo
                                      actor->id == ACTOR_BOSS_TW ||    // Twinrova
                                      actor->id == ACTOR_BOSS_GANON || // Ganondorf
                                      actor->id == ACTOR_BOSS_GANON2;  // Ganon

                // Don't apply during cutscenes because it causes weird behaviour and/or crashes on some bosses.
                if (IsHyperBossesActive() && isBossActor && !Player_InBlockingCsMode(gPlayState, player)) {
                    // Barinade needs to be updated in sequence to avoid unintended behaviour.
                    if (actor->id == ACTOR_BOSS_VA) {
                        // params -1 is BOSSVA_BODY
                        if (actor->params == -1) {
                            Actor* actorList = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head;
                            while (actorList != NULL) {
                                GameInteractor::RawAction::UpdateActor(actorList);
                                actorList = actorList->next;
                            }
                        }
                    } else {
                        GameInteractor::RawAction::UpdateActor(actor);
                    }
                }
            });
    }
}

void RegisterHyperBosses() {
    UpdateHyperBossesState();
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>(
        [](int16_t fileNum) { UpdateHyperBossesState(); });
}

// ---------------------------------------------------------------------------------------------------------------------
// Hit Impact: purely cosmetic "juice" on melee contact. Every effect has its own CVar (default off) and nothing here
// touches damage, hitboxes, timings of attacks or enemy AI.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
struct HitImpactState {
    Actor* victim = nullptr;     // last struck actor, watched for a kill
    int killWatchFrames = 0;     // frames left to check if the victim died
    int slowmoFrames = 0;        // frames left of the kill slow-mo
    float fovApplied = 0.0f;     // total FOV offset currently applied by us
    float fovRecover = 0.0f;     // per-frame recovery amount
    int fovFrames = 0;           // frames left of FOV recovery
};
HitImpactState sHit;

// 0 = light (basic slash/stab), 1 = medium (finisher, jumpslash, spin), 2 = heavy (hammer / big spin)
int HitWeight(Player* player) {
    if (player->heldItemAction == PLAYER_IA_HAMMER) {
        return 2;
    }
    switch (player->meleeWeaponAnimation) {
        case PLAYER_MWA_BIG_SPIN_1H:
        case PLAYER_MWA_BIG_SPIN_2H:
            return 2;
        case PLAYER_MWA_FLIPSLASH_FINISH:
        case PLAYER_MWA_JUMPSLASH_FINISH:
        case PLAYER_MWA_SPIN_ATTACK_1H:
        case PLAYER_MWA_SPIN_ATTACK_2H:
            return 1;
        default:
            break;
    }
    // Third hit of a combo (finishers run at +2 once the combo counter reaches 3).
    return player->unk_845 >= 3 ? 1 : 0;
}

void ApplyFovKick(PlayState* play, float amount) {
    Camera* cam = GET_ACTIVE_CAM(play);
    if (cam == nullptr) {
        return;
    }
    cam->fov -= amount;
    sHit.fovApplied += amount;
    sHit.fovFrames = 6;
    sHit.fovRecover = sHit.fovApplied / 6.0f;
}

void RegisterHitImpact() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerMeleeHit>([](void* refPlayer, void* refActor) {
        if (gPlayState == nullptr) {
            return;
        }
        Player* player = static_cast<Player*>(refPlayer);
        Actor* actor = static_cast<Actor*>(refActor);
        int w = HitWeight(player);

        // Hitstop: briefly freezes actors. Proportional to attack weight.
        if (CVarGetInteger(CVAR_ENHANCEMENT("HitImpact.Hitstop"), 0)) {
            // Vanilla sets 1. Values are kept small (see notes: semantics of freezeFlashTimer to be verified).
            static const int kFreeze[] = { 2, 3, 4 };
            if (gPlayState->actorCtx.freezeFlashTimer < kFreeze[w]) {
                gPlayState->actorCtx.freezeFlashTimer = kFreeze[w];
            }
        }

        if (CVarGetInteger(CVAR_ENHANCEMENT("HitImpact.Shake"), 0)) {
            static const s32 kY[] = { 2, 4, 7 };
            static const s32 kTime[] = { 4, 7, 12 };
            Player_RequestQuake(gPlayState, 27767, kY[w], kTime[w]);
        }

        if (CVarGetInteger(CVAR_ENHANCEMENT("HitImpact.Rumble"), 0)) {
            static const u8 kStrength[] = { 90, 150, 255 };
            static const u8 kDuration[] = { 6, 10, 16 };
            Player_RequestRumble(player, kStrength[w], kDuration[w], 120, 0);
        }

        if (CVarGetInteger(CVAR_ENHANCEMENT("HitImpact.Particles"), 0)) {
            Vec3f pos = actor->focus.pos;
            CollisionCheck_SpawnShieldParticles(gPlayState, &pos);
            if (w >= 1) {
                CollisionCheck_SpawnShieldParticles(gPlayState, &pos);
            }
            static const u16 kSfx[] = { NA_SE_IT_WALL_HIT_SOFT, NA_SE_IT_WALL_HIT_HARD, NA_SE_IT_HAMMER_HIT };
            Audio_PlaySoundGeneral(kSfx[w], &actor->projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }

        if (CVarGetInteger(CVAR_ENHANCEMENT("HitImpact.FovZoom"), 0)) {
            static const float kFov[] = { 1.0f, 2.0f, 3.5f };
            ApplyFovKick(gPlayState, kFov[w]);
        }

        if (CVarGetInteger(CVAR_ENHANCEMENT("HitImpact.KillSlowmo"), 0) && actor->category == ACTORCAT_ENEMY) {
            sHit.victim = actor;
            sHit.killWatchFrames = 8;
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        if (gPlayState == nullptr) {
            return;
        }

        // FOV: give back what we took, one slice per frame.
        if (sHit.fovFrames > 0) {
            Camera* cam = GET_ACTIVE_CAM(gPlayState);
            if (cam != nullptr) {
                float give = std::min(sHit.fovRecover, sHit.fovApplied);
                cam->fov += give;
                sHit.fovApplied -= give;
            }
            if (--sHit.fovFrames == 0) {
                sHit.fovApplied = 0.0f;
            }
        }

        // Kill detection: the victim's health reaches 0 within a few frames of the hit.
        if (sHit.killWatchFrames > 0 && sHit.victim != nullptr) {
            if (sHit.victim->colChkInfo.health <= 0) {
                sHit.slowmoFrames = 12;
                sHit.killWatchFrames = 0;
                sHit.victim = nullptr;
            } else if (--sHit.killWatchFrames == 0) {
                sHit.victim = nullptr;
            }
        }

        // Slow-mo: freeze actors every other frame => roughly half speed for a fraction of a second.
        if (sHit.slowmoFrames > 0) {
            if ((sHit.slowmoFrames & 1) && gPlayState->actorCtx.freezeFlashTimer == 0) {
                gPlayState->actorCtx.freezeFlashTimer = 1;
            }
            sHit.slowmoFrames--;
        }
    });
}
} // namespace

void InitMods() {
    RegisterOcarinaTimeTravel();
    RegisterHyperBosses();
    RegisterHitImpact();
}
