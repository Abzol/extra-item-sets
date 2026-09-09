/* ニブルヘンネ */
#include "mods/svc/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/save.h"
#include "mods/svc/config.h"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

// Game includes
#include "d/d_menu_ring.h"
#include "d/d_menu_window.h"
#include "m_Do/m_Do_controller_pad.h"
#include "d/actor/d_a_alink.h"
#include <stdio.h>
DEFINE_MOD();

// Just name all the services you will use.
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(SaveService, svc_save);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);

/* Open a mod-specific namespace so you won't cause any collisions */
namespace {

/* Create config variables */
ConfigVarHandle g_cvarExtraItemSets = 0;

/* This struct holds all the data that will be stored along with the save file. */
/* The save service saves anonymous blobs of data, which is exactly what structs are made for. */
struct mod_save_data{
    // array order:
    // 0: itemx 1: itemy 2: itemz 3: itemw
    // 5-8 unused for now
    u8 mod_equipItems[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    u8 mod_comboItems[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
} mod_data;

/* unused for now */
int get_int_option(ConfigVarHandle handle, int64_t fallback) {
    int64_t value = fallback;
    if (handle == 0 || svc_config->get_int(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}
ModResult register_int_option(
    const char* name, int64_t defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_INT;
    cvarDesc.default_int = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register option");
    }
    return MOD_OK;
}

void add_control(UiElementHandle pane, const UiControlDesc& desc) {
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

/* GAME LOGIC */

// You need to call DEFINE_HOOK to set up a name for the thing you want to hook onto.
// The second name is just an internal name to refer to the hook later.
DEFINE_HOOK(&dMenu_Ring_c::stick_wait_proc, StickWaitProc); // while item menu open
DEFINE_HOOK(&dMenu_Ring_c::setSelectItemForce, SetSelectItemForce); // fixes a bug
DEFINE_HOOK(&dMw_c::key_wait_proc, KeyWaitProc); // while control of link
DEFINE_HOOK(&daAlink_c::midnaTalkTrigger, MidnaTalk); // trigger for talking to midna

/* functions that run before the original game code need to return a HookAction */
static HookAction on_set_select_item_force_pre(ModContext*, void* args, void*, void*) {
    dMenu_Ring_c *pMenuRing = static_cast<dMenu_Ring_c*>(args); 
    if (pMenuRing->field_0x6b8[0] == 0xFF) {
        pMenuRing->field_0x6b8[0] = dComIfGs_getMixItemIndex(0);
    }
    if (pMenuRing->field_0x6b8[1] == 0xFF) {
        pMenuRing->field_0x6b8[1] = dComIfGs_getMixItemIndex(1);
    }
    if (pMenuRing->field_0x6b4[0] == 0xFF) {
        pMenuRing->field_0x6b4[0] = dComIfGs_getSelectItemIndex(0);
    }
    if (pMenuRing->field_0x6b4[1] == 0xFF) {
        pMenuRing->field_0x6b4[1] = dComIfGs_getSelectItemIndex(1);
    }
    return HOOK_CONTINUE; // means continue with the original function
}
 
/* functions that run after the original function return void */
static void on_set_select_item_force_post(ModContext*, void* args, void*, void*) {
    dMenu_Ring_c *pMenuRing = static_cast<dMenu_Ring_c*>(args); 
    pMenuRing->field_0x6b4[0] = 0xFF;
    pMenuRing->field_0x6b8[1] = 0xFF;
    pMenuRing->field_0x6b4[0] = 0xFF;
    pMenuRing->field_0x6b8[1] = 0xFF;
}

/* this is the core function that actually performs the swap */
void swapItems() {
    u8 selitem;
    u8 mixitem;
    for (int i = 0; i < 4; i++) {
        selitem = dComIfGs_getSelectItemIndex(i);
        mixitem = dComIfGs_getMixItemIndex(i);
        dComIfGs_setSelectItemIndex(i, mod_data.mod_equipItems[i]);
        dComIfGs_setMixItemIndex(i, mod_data.mod_comboItems[i]);
        mod_data.mod_equipItems[i] = selitem;
        mod_data.mod_comboItems[i] = mixitem;
    }
    Z2GetAudioMgr()->seStart(Z2SE_SY_ITEM_SET_X, NULL, 0, 0, 1.0f, 1.0f, -1.0f, -1.0f, 0);
}
/* the first argument to args is a pointer to the caller. */
/* static_cast it to whatever you know it is lets you access internal members */
static void ring_checkDoSwapItems(ModContext*, void* args, void*, void*) {
    dMenu_Ring_c *pMenuRing = static_cast<dMenu_Ring_c*>(args); 
    if (mDoCPd_c::getHoldR(PAD_1) && mDoCPd_c::getTrigZ(PAD_1)) {
        /* mSelectItemSlideElapsed informs us if an equip animation is playing */
        /* swapping sets while an item is moving can cause issues, so we cancel if it is. */
        for (int i = 0; i < 4; i++) {
            if (pMenuRing->mSelectItemSlideElapsed[i] > 0.000001f) { 
                return;
            }
        }
        swapItems();
    }
}

/* this code is for when you have control of link */
static HookAction checkDoSwapItems(ModContext*, void* args, void*, void*) {
    if (mDoCPd_c::getHoldR(PAD_1) && mDoCPd_c::getTrigZ(PAD_1)) {
        swapItems();
    }
    return HOOK_CONTINUE;
}

/* since our shortcut is R+Z, we override this function in Link's actor. */
/* normally this handles checking if you can talk to Midna. */
static HookAction on_midnaTalkTrigger_pre(ModContext*, void* args, void*, void*) {
    if (mDoCPd_c::getHoldR(PAD_1)) { /* If you're holding R... */
        return HOOK_SKIP_ORIGINAL;   /* SKIP_ORIGINAL */
    }
    return HOOK_CONTINUE; /* Otherwise, play the original function. */
}

/* UI FOR MOD OPTIONS */

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Extra Item Set Enabled";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarExtraItemSets;
    add_control(panel, control);

    return MOD_OK;
}
} //namespace
 
/* These bits are what dusklight runs */
extern "C" {
/* mod_initialize runs when the game starts or the mod is enabled by the user */
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    svc_log->info(mod_ctx, "ExtraButtons initialized");

    ModResult result;
    result = register_int_option("extraItemSets", 0, g_cvarExtraItemSets, error);
    if (result != MOD_OK) {
        return result;
    }
    // mods::hook::add_pre<>() actually makes the game accept the hook and calls a named function.
    result = mods::hook::add_post<StickWaitProc>(svc_hook, ring_checkDoSwapItems);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to install stick_wait_proc!");
        return result;
    }
    result = mods::hook::add_pre<SetSelectItemForce>(svc_hook, on_set_select_item_force_pre);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to install on_select_item_force_pre!");
        return result;
    }
    result = mods::hook::add_post<SetSelectItemForce>(svc_hook, on_set_select_item_force_post);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to install on_select_item_force_post!");
        return result;
    }
    result = mods::hook::add_pre<KeyWaitProc>(svc_hook, checkDoSwapItems);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to install key_wait_proc!");
        return result;
    }
    result = mods::hook::add_pre<MidnaTalk>(svc_hook, on_midnaTalkTrigger_pre);
    if (result != MOD_OK) {
        svc_log->error(mod_ctx, "failed to install key_wait_proc!");
        return result;
    }
    UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
    panelDesc.build = build_panel;
    svc_ui->register_mods_panel(mod_ctx, &panelDesc);

    return MOD_OK;
}
/* mod_update runs every frame */
MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}
/* mod_shutdown runs when the game exits or the mod is disabled by the user */
/* if you dont disable your code here it will *continue* to run even when disabled */
MOD_EXPORT ModResult mod_shutdown(ModError*) {
    return MOD_OK;
}
}
