#define WLR_USE_UNSTABLE

#include <unistd.h>

#include <any>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>

#include <algorithm>

#include "hyprWindowDecorator.hpp"
#include "plugin.hpp"

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

static void onNewWindow(void *self, std::any data)
{
    // data is guaranteed
    const auto PWINDOW = std::any_cast<PHLWINDOW>(data);

    if (!PWINDOW->m_X11DoesntWantBorders)
    {
        if (std::ranges::any_of(PWINDOW->m_windowDecorations, [](const auto &d)
                                { return d->getDisplayName() == "Hyprdecor"; }))
            return;

        auto bar = makeUnique<CHyprWindowDecorator>(PWINDOW);
        auto barRaw = bar.get();
        barRaw->m_self = barRaw;
        gPlugin->m_vBars.push_back(barRaw);
        HyprlandAPI::addWindowDecoration(gPlugin->m_pHandle, PWINDOW, std::move(bar));
    }
}

static void onCloseWindow(void *self, std::any data)
{
    // data is guaranteed
    const auto PWINDOW = std::any_cast<PHLWINDOW>(data);

    const auto BARIT = std::find_if(gPlugin->m_vBars.begin(), gPlugin->m_vBars.end(), [PWINDOW](const auto &bar)
                                    { return bar && bar->getOwner() == PWINDOW; });

    if (BARIT == gPlugin->m_vBars.end())
        return;

    // we could use the API but this is faster + it doesn't matter here that much.
    PWINDOW->removeWindowDeco(*BARIT);
}

static void onPreConfigReload()
{
    gPlugin->m_vButtons.clear();
}

static void onUpdateWindowRules(PHLWINDOW window)
{
    const auto BARIT = std::find_if(gPlugin->m_vBars.begin(), gPlugin->m_vBars.end(), [window](CHyprWindowDecorator *bar)
                                    { return bar && bar->getOwner() == window; });

    if (BARIT == gPlugin->m_vBars.end())
        return;

    if (*BARIT)
    {
        (*BARIT)->updateRules();
        window->updateWindowDecos();
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    gPlugin = std::make_unique<CPlugin>(handle);

    const std::string HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH)
    {
        HyprlandAPI::addNotification(gPlugin->m_pHandle, "[hyprdecor] Failure in initialization: Version mismatch (headers ver is not equal to running hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hb] Version mismatch");
    }

    gPlugin->m_nobarRuleIdx = Desktop::Rule::windowEffects()->registerEffect("hyprdecor:no_bar");
    gPlugin->m_barColorRuleIdx = Desktop::Rule::windowEffects()->registerEffect("hyprdecor:bar_color");
    gPlugin->m_titleColorRuleIdx = Desktop::Rule::windowEffects()->registerEffect("hyprdecor:title_color");

    static auto P = HyprlandAPI::registerCallbackDynamic(gPlugin->m_pHandle, "openWindow", [&](void *self, SCallbackInfo &info, std::any data)
                                                         { onNewWindow(self, data); });
    // static auto P2 = HyprlandAPI::registerCallbackDynamic(g_pConfig->m_pHandle, "closeWindow", [&](void* self, SCallbackInfo& info, std::any data) { onCloseWindow(self, data); });
    static auto P3 = HyprlandAPI::registerCallbackDynamic(gPlugin->m_pHandle, "windowUpdateRules",
                                                          [&](void *self, SCallbackInfo &info, std::any data)
                                                          { onUpdateWindowRules(std::any_cast<PHLWINDOW>(data)); });

    static auto P4 = HyprlandAPI::registerCallbackDynamic(gPlugin->m_pHandle, "preConfigReload", [&](void *self, SCallbackInfo &info, std::any data)
                                                          { onPreConfigReload(); });
    static auto P5 = HyprlandAPI::registerCallbackDynamic(gPlugin->m_pHandle, "configReloaded", [&](void *self, SCallbackInfo &info, std::any data)
                                                          { gPlugin->update(); });

    // add deco to existing windows
    for (auto &w : g_pCompositor->m_windows)
    {
        if (w->isHidden() || !w->m_isMapped)
            continue;

        onNewWindow(nullptr /* unused */, std::any(w));
    }

    HyprlandAPI::reloadConfig();

    gPlugin->update();

    return {"hyprdecor", "A plugin to add title bars to windows.", "Vaxry", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    for (auto &m : g_pCompositor->m_monitors)
        m->m_scheduledRecalc = true;

    g_pHyprRenderer->m_renderPass.removeAllOfType("CRenderPassElement");

    Desktop::Rule::windowEffects()->unregisterEffect(gPlugin->m_barColorRuleIdx);
    Desktop::Rule::windowEffects()->unregisterEffect(gPlugin->m_titleColorRuleIdx);
    Desktop::Rule::windowEffects()->unregisterEffect(gPlugin->m_nobarRuleIdx);

    gPlugin.reset();
}
