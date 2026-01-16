#include "plugin.hpp"
#include "hyprWindowDecorator.hpp"
#include "util.hpp"
#include <hyprland/src/config/ConfigManager.hpp>
#include <librsvg/rsvg.h>
#include <filesystem>

static std::string resolveTexturePath(const std::string &base, const std::vector<std::string> &suffixes, const std::string &fallback = "")
{
    for (const auto &suffix : suffixes)
    {
        std::string path = base + suffix;
        if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path))
            return path;
        if (!path.ends_with(".png") && !path.ends_with(".svg"))
        {
            if (std::filesystem::exists(path + ".png") && std::filesystem::is_regular_file(path + ".png"))
                return path + ".png";
            if (std::filesystem::exists(path + ".svg") && std::filesystem::is_regular_file(path + ".svg"))
                return path + ".svg";
        }
    }
    return fallback;
}

Hyprlang::CParseResult onNewButtonTextured(const char *K, const char *V)
{
    std::string v = V;
    CVarList vars(v);

    Hyprlang::CParseResult result;

    // hyprdecor-button = size, action, active_tex, [inactive_tex], [hover_tex], [pressed_tex]

    if (vars[0].empty() || vars[1].empty() || vars[2].empty())
    {
        result.setError("size, action and active_tex cannot be empty");
        return result;
    }

    float size = 10;
    try
    {
        size = std::stof(vars[0]);
    }
    catch (std::exception &e)
    {
        result.setError("failed to parse size");
        return result;
    }

    SHyprButton button;
    button.size = {size, size};
    button.cmd = vars[1];

    if (vars.size() == 3)
    {
        std::string base = vars[2];
        button.pathActive = resolveTexturePath(base, {"", "_normal", "_active", "_focused"});
        button.pathInactive = resolveTexturePath(base, {"_unfocused", "_inactive"}, button.pathActive);
        button.pathHover = resolveTexturePath(base, {"_hover"}, button.pathActive);
        button.pathPressed = resolveTexturePath(base, {"_pressed", "_clicked"}, button.pathHover);
    }
    else
    {
        button.pathActive = vars[2];
        button.pathInactive = vars.size() > 3 ? vars[3] : vars[2];
        button.pathHover = vars.size() > 4 ? vars[4] : button.pathActive;
        button.pathPressed = vars.size() > 5 ? vars[5] : button.pathHover;
    }

    gPlugin->m_vButtons.push_back(button);

    for (auto bar : gPlugin->m_vBars)
    {
        if (bar)
            bar->m_bButtonsDirty = true;
    }

    return result;
}

void CPlugin::loadAllTextures()
{
    // load textures if they exist and are not loaded
    for (auto &button : m_vButtons)
    {
        if (!button.pathActive.empty() && button.texActive->m_texID == 0)
        {
            loadTexture(button.pathActive, button.texActive, ninepatch_linear_filtering);
            if (button.size.x <= 0 && button.texActive->m_texID != 0)
                button.size = button.texActive->m_size;
        }
        if (!button.pathInactive.empty() && button.texInactive->m_texID == 0)
            loadTexture(button.pathInactive, button.texInactive, ninepatch_linear_filtering);
        if (!button.pathHover.empty() && button.texHover->m_texID == 0)
            loadTexture(button.pathHover, button.texHover, ninepatch_linear_filtering);
        if (!button.pathPressed.empty() && button.texPressed->m_texID == 0)
            loadTexture(button.pathPressed, button.texPressed, ninepatch_linear_filtering);

        if (button.size.x <= 0)
            button.size = {20, 20};
    }
}

CPlugin::CPlugin(HANDLE handle)
{
    m_pHandle = handle;
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:bar_color", Hyprlang::INT{*configStringToInt("rgba(33333388)")});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_top", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:col.text", Hyprlang::INT{*configStringToInt("rgba(ffffffff)")});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_size", Hyprlang::INT{10});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:bar_blur", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:bar_text_font", Hyprlang::STRING{"Sans"});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_align", Hyprlang::FLOAT{0.5});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_placement", Hyprlang::STRING{"top"});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:bar_part_of_window", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:bar_precedence_over_border", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:bar_buttons_alignment", Hyprlang::STRING{"right"});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_padding", Hyprlang::INT{7});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:bar_button_padding", Hyprlang::INT{5});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:on_double_click", Hyprlang::STRING{""});

    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_texture", Hyprlang::STRING{""});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_active", Hyprlang::STRING{""});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_inactive", Hyprlang::STRING{""});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_middle_alpha", Hyprlang::FLOAT{0.0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_repeat", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_linear_filtering", Hyprlang::INT{1});

    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_inset", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_left", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_right", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_bottom", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_appicon_enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_render_above", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(m_pHandle, "plugin:hyprdecor:decoration_appicon_offset", Hyprlang::VEC2{0, 0});

    HyprlandAPI::addConfigKeyword(m_pHandle, "plugin:hyprdecor:hyprdecor-button", onNewButtonTextured, Hyprlang::SHandlerOptions{});
}

CPlugin::~CPlugin()
{
    if (activeSurface)
        cairo_surface_destroy(activeSurface);
    if (inactiveSurface && inactiveSurface != activeSurface)
        cairo_surface_destroy(inactiveSurface);
}

void CPlugin::update()
{
    auto *const PENABLED = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:enabled")->getDataStaticPtr();
    enabled = **PENABLED;
    if (!enabled)
        return;

    auto *const PBARCOLOR = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:bar_color")->getDataStaticPtr();
    auto *const PHEIGHT = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_top")->getDataStaticPtr();
    auto *const PTEXTCOL = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:col.text")->getDataStaticPtr();
    auto *const PTEXTSIZE = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_size")->getDataStaticPtr();
    auto *const PTITLEENABLED = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_enabled")->getDataStaticPtr();
    auto *const PBARBLUR = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:bar_blur")->getDataStaticPtr();
    auto *const PTEXTFONT = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:bar_text_font")->getDataStaticPtr();
    auto *const PTEXTALIGN = (Hyprlang::FLOAT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_align")->getDataStaticPtr();
    auto *const PTEXTPLACE = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_title_placement")->getDataStaticPtr();
    auto *const PPARTOW = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:bar_part_of_window")->getDataStaticPtr();
    auto *const PPRECEDENCE = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:bar_precedence_over_border")->getDataStaticPtr();
    auto *const PALIGNBUTTONS = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:bar_buttons_alignment")->getDataStaticPtr();
    auto *const PPADDING = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_padding")->getDataStaticPtr();
    auto *const PBUTPADDING = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:bar_button_padding")->getDataStaticPtr();
    auto *const PONDOUBLECLICK = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:on_double_click")->getDataStaticPtr();

    auto *const PTEXTURE = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_texture")->getDataStaticPtr();
    auto *const PTEXACTIVE = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_active")->getDataStaticPtr();
    auto *const PTEXINACTIVE = (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_inactive")->getDataStaticPtr();
    auto *const PMIDDLEALPHA = (Hyprlang::FLOAT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_middle_alpha")->getDataStaticPtr();
    auto *const PREPEAT = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_repeat")->getDataStaticPtr();
    auto *const PINSET = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_inset")->getDataStaticPtr();
    auto *const PLEFTWIDTH = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_left")->getDataStaticPtr();
    auto *const PRIGHTWIDTH = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_right")->getDataStaticPtr();
    auto *const PBOTTOMHT = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_offset_bottom")->getDataStaticPtr();
    auto *const PLINEAR = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:ninepatch_linear_filtering")->getDataStaticPtr();
    auto *const PSHOWAPPICON = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_appicon_enabled")->getDataStaticPtr();
    auto *const PBARABOVE = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_render_above")->getDataStaticPtr();
    auto *const PAPPICONOFFSET = (Hyprlang::VEC2 *const *)HyprlandAPI::getConfigValue(m_pHandle, "plugin:hyprdecor:decoration_appicon_offset")->getDataStaticPtr();

    bar_color = CHyprColor(**PBARCOLOR);
    decoration_offset_top = **PHEIGHT;
    col_text = CHyprColor(**PTEXTCOL);
    decoration_title_size = **PTEXTSIZE;
    decoration_title_enabled = **PTITLEENABLED;
    bar_blur = **PBARBLUR;
    bar_text_font = *PTEXTFONT;
    decoration_title_align = **PTEXTALIGN;
    decoration_title_placement = *PTEXTPLACE;
    bar_part_of_window = **PPARTOW;
    bar_precedence_over_border = **PPRECEDENCE;
    bar_buttons_alignment = *PALIGNBUTTONS;
    decoration_padding = **PPADDING;
    bar_button_padding = **PBUTPADDING;
    enabled = **PENABLED;
    on_double_click = *PONDOUBLECLICK;

    const auto PTEXTURE_STR = PTEXTURE ? *PTEXTURE : nullptr;
    const auto PTEXACT = PTEXACTIVE ? *PTEXACTIVE : nullptr;
    const auto PTEXINACT = PTEXINACTIVE ? *PTEXINACTIVE : nullptr;

    std::string NINEPATCHACTIVE = PTEXACT ? PTEXACT : "";
    std::string NINEPATCHINACTIVE = (PTEXINACT && !std::string(PTEXINACT).empty()) ? PTEXINACT : NINEPATCHACTIVE;

    if (PTEXTURE_STR && !std::string(PTEXTURE_STR).empty())
    {
        std::string base = PTEXTURE_STR;
        if (NINEPATCHACTIVE.empty())
            NINEPATCHACTIVE = resolveTexturePath(base, {"_active", "_focused", "active", ""});
        if (NINEPATCHINACTIVE.empty() || NINEPATCHINACTIVE == NINEPATCHACTIVE)
            NINEPATCHINACTIVE = resolveTexturePath(base, {"_inactive", "_unfocused", "inactive"}, NINEPATCHACTIVE);
    }

    // Refresh surfaces
    SNinePatchInfo newActiveNPI, newInactiveNPI;
    auto newActiveSurface = loadSurface(NINEPATCHACTIVE, &newActiveNPI);
    auto newInactiveSurface = (NINEPATCHINACTIVE == NINEPATCHACTIVE || NINEPATCHINACTIVE.empty()) ? nullptr : loadSurface(NINEPATCHINACTIVE, &newInactiveNPI);

    if (activeSurface)
        cairo_surface_destroy(activeSurface);
    if (inactiveSurface && inactiveSurface != activeSurface)
        cairo_surface_destroy(inactiveSurface);

    activeSurface = nullptr;
    inactiveSurface = nullptr;

    ninepatch_active = NINEPATCHACTIVE;
    ninepatch_inactive = NINEPATCHINACTIVE;
    ninepatch_texture = PTEXTURE_STR ? PTEXTURE_STR : "";
    activeSurface = newActiveSurface;
    inactiveSurface = newInactiveSurface ? newInactiveSurface : newActiveSurface;
    activeNinepatch = newActiveNPI;
    inactiveNinepatch = newInactiveSurface ? newInactiveNPI : newActiveNPI;

    ninepatch_middle_alpha = **PMIDDLEALPHA;
    decoration_inset = **PINSET;
    decoration_offset_left = **PLEFTWIDTH;
    decoration_offset_right = **PRIGHTWIDTH;
    decoration_offset_bottom = **PBOTTOMHT;
    ninepatch_linear_filtering = **PLINEAR;
    ninepatch_repeat = **PREPEAT;
    decoration_appicon_enabled = **PSHOWAPPICON;
    decoration_render_above = **PBARABOVE;
    decoration_appicon_offset = {(*PAPPICONOFFSET)->x, (*PAPPICONOFFSET)->y};

    // Damage and update windows after all bars are invalidated
    for (auto bar : m_vBars)
    {
        if (bar)
        {
            bar->invalidateTextures();
            g_pDecorationPositioner->repositionDeco(bar);

            if (auto pWindow = bar->getOwner())
            {
                pWindow->updateWindowDecos();
                g_pHyprRenderer->damageWindow(pWindow);
            }
            bar->damageEntire();
        }
    }

    loadAllTextures();
}
