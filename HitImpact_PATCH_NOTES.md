# Hit Impact — pedaços que faltam aplicar (não tive acesso a esses arquivos)

## 1. Declarar o hook (GameInteractor_HookTable / GameInteractor.h)
Junto dos outros hooks de jogador:
    DEFINE_HOOK(OnPlayerMeleeHit, (void* player, void* hitActor));
e no bloco extern "C" (para o z_player.c):
    void GameInteractor_ExecuteOnPlayerMeleeHit(void* player, void* hitActor);
Implementação (GameInteractor_HookTable.cpp), no mesmo padrão dos outros:
    void GameInteractor_ExecuteOnPlayerMeleeHit(void* player, void* hitActor) {
        GameInteractor::Instance->ExecuteHooks<GameInteractor::OnPlayerMeleeHit>(player, hitActor);
    }

## 2. Menu (Enhancements) — um checkbox por CVar, prefixo CVAR_ENHANCEMENT
HitImpact.Hitstop, HitImpact.Shake, HitImpact.Rumble, HitImpact.Particles, HitImpact.FovZoom, HitImpact.KillSlowmo

## 3. Não verificado (precisa testar)
- Semântica de actorCtx.freezeFlashTimer (usei 2–4 frames; ajuste em kFreeze).
- FOV: a câmera pode reescrever cam->fov no mesmo frame; se não surtir efeito, aplicar no hook pós-câmera.
- Kill-slowmo detecta morte por colChkInfo.health <= 0 em até 8 frames; slow-mo = congelar atores em frames alternados.
- Nada foi compilado.
