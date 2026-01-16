#pragma once

#include <hyprlang.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <cairo/cairo.h>

struct SHyprButton
{
    std::string cmd = "";
    Vector2D size = {10, 10};

    // textured states
    SP<CTexture> texActive = makeShared<CTexture>();
    SP<CTexture> texInactive = makeShared<CTexture>();
    SP<CTexture> texHover = makeShared<CTexture>();
    SP<CTexture> texPressed = makeShared<CTexture>();

    std::string pathActive = "";
    std::string pathInactive = "";
    std::string pathHover = "";
    std::string pathPressed = "";
};

class CHyprWindowDecorator;

struct SNinePatchInfo
{
    float border[4] = {0, 0, 0, 0};  // L, T, R, B
    float padding[4] = {0, 0, 0, 0}; // L, T, R, B (content)
    bool defined = false;
};

class CPlugin
{
public:
    CPlugin(HANDLE handle);
    ~CPlugin();

    void update();
    void loadAllTextures();

    CHyprColor bar_color;
    int decoration_offset_top;
    CHyprColor col_text;
    int decoration_title_size;
    bool decoration_title_enabled;
    bool bar_blur;
    std::string bar_text_font;
    float decoration_title_align;
    std::string decoration_title_placement;
    bool bar_part_of_window;
    bool bar_precedence_over_border;
    std::string bar_buttons_alignment;
    int decoration_padding;
    int bar_button_padding;
    bool enabled;
    std::string on_double_click;

    std::string ninepatch_texture;
    std::string ninepatch_active;
    std::string ninepatch_inactive;
    SNinePatchInfo activeNinepatch;
    SNinePatchInfo inactiveNinepatch;
    float ninepatch_middle_alpha;
    int decoration_inset;
    int decoration_offset_left;
    int decoration_offset_right;
    int decoration_offset_bottom;
    bool ninepatch_linear_filtering;
    bool ninepatch_repeat;
    bool decoration_appicon_enabled;
    bool decoration_render_above;
    Vector2D decoration_appicon_offset;

    // Parsed results
    cairo_surface_t *activeSurface = nullptr;
    cairo_surface_t *inactiveSurface = nullptr;

    HANDLE m_pHandle = nullptr;
    std::vector<SHyprButton> m_vButtons;
    std::vector<CHyprWindowDecorator *> m_vBars;
    uint32_t m_nobarRuleIdx = 0;
    uint32_t m_barColorRuleIdx = 0;
    uint32_t m_titleColorRuleIdx = 0;
};

inline std::unique_ptr<CPlugin> gPlugin;

#define DEBUG_LOG(fmt, ...) Log::logger->log(Log::DEBUG, "[HYPRDECOR] " fmt, ##__VA_ARGS__)
