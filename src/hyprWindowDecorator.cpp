#include "hyprWindowDecorator.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <pango/pangocairo.h>
#include <librsvg/rsvg.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "renderPassElement.hpp"
#include "plugin.hpp"
#include "util.hpp"

CHyprWindowDecorator::CHyprWindowDecorator(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow)
{
    m_pWindow = pWindow;

    const auto PMONITOR = pWindow->m_monitor.lock();
    PMONITOR->m_scheduledRecalc = true;

    m_pAppIconTex = makeShared<CTexture>();

    // button events
    m_pMouseButtonCallback = HyprlandAPI::registerCallbackDynamic(
        gPlugin->m_pHandle, "mouseButton", [&](void *self, SCallbackInfo &info, std::any param)
        { onMouseButton(info, std::any_cast<IPointer::SButtonEvent>(param)); });
    m_pTouchDownCallback = HyprlandAPI::registerCallbackDynamic(
        gPlugin->m_pHandle, "touchDown", [&](void *self, SCallbackInfo &info, std::any param)
        { onTouchDown(info, std::any_cast<ITouch::SDownEvent>(param)); });
    m_pTouchUpCallback = HyprlandAPI::registerCallbackDynamic( //
        gPlugin->m_pHandle, "touchUp", [&](void *self, SCallbackInfo &info, std::any param)
        { onTouchUp(info, std::any_cast<ITouch::SUpEvent>(param)); });

    // move events
    m_pTouchMoveCallback = HyprlandAPI::registerCallbackDynamic(
        gPlugin->m_pHandle, "touchMove", [&](void *self, SCallbackInfo &info, std::any param)
        { onTouchMove(info, std::any_cast<ITouch::SMotionEvent>(param)); });
    m_pMouseMoveCallback = HyprlandAPI::registerCallbackDynamic( //
        gPlugin->m_pHandle, "mouseMove", [&](void *self, SCallbackInfo &info, std::any param)
        { onMouseMove(std::any_cast<Vector2D>(param)); });

    m_pTextTex = makeShared<CTexture>();
    m_pButtonsTex = makeShared<CTexture>();

    m_pAppIconTex = makeShared<CTexture>();
    m_pBarFinalTex = makeShared<CTexture>();

    g_pAnimationManager->createAnimation(gPlugin->bar_color, m_cRealBarColor, g_pConfigManager->getAnimationPropertyConfig("border"), pWindow, AVARDAMAGE_NONE);
    m_cRealBarColor->setUpdateCallback([&](auto)
                                       { damageEntire(); });
}

CHyprWindowDecorator::~CHyprWindowDecorator()
{
    // Callbacks are automatically cleaned up when the shared pointers are destroyed
    if (gPlugin)
        std::erase(gPlugin->m_vBars, this);
}

SDecorationPositioningInfo CHyprWindowDecorator::getPositioningInfo()
{
    SDecorationPositioningInfo info;
    info.policy = m_hidden ? DECORATION_POSITION_ABSOLUTE : DECORATION_POSITION_STICKY;
    info.edges = DECORATION_EDGE_TOP | DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT;
    info.priority = gPlugin->bar_precedence_over_border ? 10005 : 5000;
    info.reserved = !gPlugin->decoration_inset;

    const auto &NPI = gPlugin->activeNinepatch.defined ? gPlugin->activeNinepatch : gPlugin->inactiveNinepatch;

    const auto P = gPlugin->decoration_padding;

    info.desiredExtents = {
        Vector2D(0, 0),
        Vector2D(0, 0)};

    if (gPlugin->enabled && !m_hidden)
    {
        info.desiredExtents.topLeft.x = (double)(gPlugin->decoration_offset_left + NPI.padding[0] + P);
        info.desiredExtents.topLeft.y = (double)(gPlugin->decoration_offset_top + NPI.padding[1] + P);
        info.desiredExtents.bottomRight.x = (double)(gPlugin->decoration_offset_right + NPI.padding[2] + P);
        info.desiredExtents.bottomRight.y = (double)(gPlugin->decoration_offset_bottom + NPI.padding[3] + P);
    }

    return info;
}

void CHyprWindowDecorator::onPositioningReply(const SDecorationPositioningReply &reply)
{
    if (reply.assignedGeometry.size() != m_bAssignedBox.size())
        m_bWindowSizeChanged = true;

    m_bAssignedBox = reply.assignedGeometry;
}

std::string CHyprWindowDecorator::getDisplayName()
{
    return "Hyprdecor";
}

bool CHyprWindowDecorator::inputIsValid()
{
    if (!gPlugin->enabled)
        return false;

    if (!m_pWindow->m_workspace || !m_pWindow->m_workspace->isVisible() || !g_pInputManager->m_exclusiveLSes.empty() ||
        (g_pSeatManager->m_seatGrab && !g_pSeatManager->m_seatGrab->accepts(m_pWindow->wlSurface()->resource())))
        return false;

    const auto WINDOWATCURSOR = g_pCompositor->vectorToWindowUnified(g_pInputManager->getMouseCoordsInternal(), Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING);

    auto focusState = Desktop::focusState();
    auto window = focusState->window();
    auto monitor = focusState->monitor();

    if (WINDOWATCURSOR != m_pWindow && m_pWindow != window)
        return false;

    // check if input is on top or overlay shell layers
    auto PMONITOR = monitor;
    PHLLS foundSurface = nullptr;
    Vector2D surfaceCoords;

    // check top layer
    g_pCompositor->vectorToLayerSurface(g_pInputManager->getMouseCoordsInternal(), &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], &surfaceCoords, &foundSurface);

    if (foundSurface)
        return false;
    // check overlay layer
    g_pCompositor->vectorToLayerSurface(g_pInputManager->getMouseCoordsInternal(), &PMONITOR->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &surfaceCoords,
                                        &foundSurface);

    if (foundSurface)
        return false;

    return true;
}

void CHyprWindowDecorator::onMouseButton(SCallbackInfo &info, IPointer::SButtonEvent e)
{
    if (!inputIsValid())
        return;

    // Only process if mouse is actually on the bar decoration
    if (!isMouseOnBar() && !m_bDragPending && !m_bDraggingThis)
        return;

    if (e.state != WL_POINTER_BUTTON_STATE_PRESSED)
    {
        handleUpEvent(info);
        return;
    }

    handleDownEvent(info, std::nullopt);
}

void CHyprWindowDecorator::onTouchDown(SCallbackInfo &info, ITouch::SDownEvent e)
{
    // Don't do anything if you're already grabbed a window with another finger
    if (!inputIsValid() || e.touchID != 0)
        return;

    handleDownEvent(info, e);
}

void CHyprWindowDecorator::onTouchUp(SCallbackInfo &info, ITouch::SUpEvent e)
{
    if (!m_bDragPending || !m_bTouchEv || e.touchID != m_touchId)
        return;

    handleUpEvent(info);
}

void CHyprWindowDecorator::onMouseMove(Vector2D coords)
{
    if (m_bTouchEv || !validMapped(m_pWindow) || m_touchId != 0)
        return;

    if (!gPlugin->m_vButtons.empty())
        damageOnButtonHover();

    if (!m_bDragPending)
        return;

    m_bDragPending = false;
    handleMovement();
}

void CHyprWindowDecorator::onTouchMove(SCallbackInfo &info, ITouch::SMotionEvent e)
{
    if (!m_bDragPending || !m_bTouchEv || !validMapped(m_pWindow) || e.touchID != m_touchId)
        return;

    auto PMONITOR = m_pWindow->m_monitor.lock();
    PMONITOR = PMONITOR ? PMONITOR : Desktop::focusState()->monitor();
    const auto COORDS = Vector2D(PMONITOR->m_position.x + e.pos.x * PMONITOR->m_size.x, PMONITOR->m_position.y + e.pos.y * PMONITOR->m_size.y);

    if (!m_bDraggingThis)
    {
        // Initial setup for dragging a window.
        g_pKeybindManager->m_dispatchers["setfloating"]("activewindow");
        g_pKeybindManager->m_dispatchers["resizewindowpixel"]("exact 50% 50%,activewindow");
        // pin it so you can change workspaces while dragging a window
        g_pKeybindManager->m_dispatchers["pin"]("activewindow");
    }
    g_pKeybindManager->m_dispatchers["movewindowpixel"](std::format("exact {} {},activewindow", (int)(COORDS.x - (assignedBoxGlobal().w / 2)), (int)COORDS.y));
    m_bDraggingThis = true;
}

void CHyprWindowDecorator::handleDownEvent(SCallbackInfo &info, std::optional<ITouch::SDownEvent> touchEvent)
{
    m_bTouchEv = touchEvent.has_value();
    if (m_bTouchEv)
        m_touchId = touchEvent.value().touchID;

    const auto PWINDOW = m_pWindow.lock();

    auto COORDS = cursorRelativeToBar();
    if (m_bTouchEv)
    {
        ITouch::SDownEvent e = touchEvent.value();
        auto PMONITOR = g_pCompositor->getMonitorFromName(!e.device->m_boundOutput.empty() ? e.device->m_boundOutput : "");
        PMONITOR = PMONITOR ? PMONITOR : Desktop::focusState()->monitor();
        COORDS = Vector2D(PMONITOR->m_position.x + e.pos.x * PMONITOR->m_size.x, PMONITOR->m_position.y + e.pos.y * PMONITOR->m_size.y) - assignedBoxGlobal().pos();
    }

    const bool BUTTONSRIGHT = gPlugin->bar_buttons_alignment != "left";
    const std::string ON_DOUBLE_CLICK = gPlugin->on_double_click;

    if (!isPointOnBar(COORDS))
    {
        // Click is outside bar bounds - cleanup if we were dragging
        if (m_bDraggingThis)
        {
            if (m_bTouchEv)
                g_pKeybindManager->m_dispatchers["settiled"]("activewindow");
            g_pKeybindManager->m_dispatchers["mouse"]("0movewindow");
        }

        m_bDraggingThis = false;
        m_bDragPending = false;
        m_bTouchEv = false;
        return;
    }

    // We're clicking on the bar - cancel the event and handle it ourselves
    info.cancelled = true;
    m_bCancelledDown = true;

    if (Desktop::focusState()->window() != PWINDOW)
        Desktop::focusState()->fullWindowFocus(PWINDOW);

    if (PWINDOW->m_isFloating)
        g_pCompositor->changeWindowZOrder(PWINDOW, true);

    m_iButtonPressedIdx = indexToButton(COORDS);
    if (m_iButtonPressedIdx != -1)
    {
        damageEntire();
        return;
    }

    if (!ON_DOUBLE_CLICK.empty() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(Time::steadyNow() - m_lastMouseDown).count() < 400 /* Arbitrary delay I found suitable */)
    {
        g_pKeybindManager->m_dispatchers["exec"](ON_DOUBLE_CLICK);
        m_bDragPending = false;
    }
    else
    {
        m_lastMouseDown = Time::steadyNow();
        m_bDragPending = true;
    }
}

void CHyprWindowDecorator::handleUpEvent(SCallbackInfo &info)
{
    if (m_pWindow.lock() != Desktop::focusState()->window())
        return;

    if (m_bCancelledDown)
        info.cancelled = true;

    m_bCancelledDown = false;

    if (m_bDraggingThis)
    {
        g_pKeybindManager->m_dispatchers["mouse"]("0movewindow");
        m_bDraggingThis = false;
        if (m_bTouchEv)
            g_pKeybindManager->m_dispatchers["settiled"]("activewindow");
    }

    m_bDragPending = false;
    m_bTouchEv = false;
    m_touchId = 0;

    if (m_iButtonPressedIdx != -1)
    {
        if (indexToButton(cursorRelativeToBar()) == m_iButtonPressedIdx)
        {
            g_pKeybindManager->m_dispatchers["exec"](gPlugin->m_vButtons[m_iButtonPressedIdx].cmd);
        }
        m_iButtonPressedIdx = -1;
        damageEntire();
    }
}

void CHyprWindowDecorator::handleMovement()
{
    g_pKeybindManager->m_dispatchers["mouse"]("1movewindow");
    m_bDraggingThis = true;
    return;
}

int CHyprWindowDecorator::indexToButton(Vector2D COORDS)
{
    const bool BUTTONSRIGHT = gPlugin->bar_buttons_alignment != "left";
    const bool VERTICAL = gPlugin->decoration_title_placement == "left" || gPlugin->decoration_title_placement == "right";
    const auto DECOBOX = assignedBoxGlobal();
    const auto &NPI = m_bWindowHasFocus ? gPlugin->activeNinepatch : gPlugin->inactiveNinepatch;
    const auto P = gPlugin->decoration_padding;

    CBox topBarBox;
    if (gPlugin->decoration_title_placement == "top")
    {
        topBarBox = {(double)(gPlugin->decoration_offset_left + NPI.padding[0] + P), 0.0, (double)(DECOBOX.w - (gPlugin->decoration_offset_left + gPlugin->decoration_offset_right + NPI.padding[0] + NPI.padding[2] + 2 * P)), (double)(gPlugin->decoration_offset_top + NPI.padding[1] + P)};
    }
    else if (gPlugin->decoration_title_placement == "bottom")
    {
        topBarBox = {(double)(gPlugin->decoration_offset_left + NPI.padding[0] + P), (double)(DECOBOX.h - (gPlugin->decoration_offset_bottom + NPI.padding[3] + P)), (double)(DECOBOX.w - (gPlugin->decoration_offset_left + gPlugin->decoration_offset_right + NPI.padding[0] + NPI.padding[2] + 2 * P)), (double)(gPlugin->decoration_offset_bottom + NPI.padding[3] + P)};
    }
    else if (gPlugin->decoration_title_placement == "left")
    {
        topBarBox = {0.0, (double)(gPlugin->decoration_offset_top + NPI.padding[1] + P), (double)(gPlugin->decoration_offset_left + NPI.padding[0] + P), (double)(DECOBOX.h - (gPlugin->decoration_offset_top + gPlugin->decoration_offset_bottom + NPI.padding[1] + NPI.padding[3] + 2 * P))};
    }
    else
    {
        topBarBox = {(double)(DECOBOX.w - (gPlugin->decoration_offset_right + NPI.padding[2] + P)), (double)(gPlugin->decoration_offset_top + NPI.padding[1] + P), (double)(gPlugin->decoration_offset_right + NPI.padding[2] + P), (double)(DECOBOX.h - (gPlugin->decoration_offset_top + gPlugin->decoration_offset_bottom + NPI.padding[1] + NPI.padding[3] + 2 * P))};
    }

    if (!VECINRECT(COORDS, topBarBox.x, topBarBox.y, topBarBox.x + topBarBox.w, topBarBox.y + topBarBox.h))
        return -1;

    const auto relativeCOORDS = COORDS - topBarBox.pos();
    const auto BARBUF = VERTICAL ? Vector2D(topBarBox.h, topBarBox.w) : Vector2D(topBarBox.w, topBarBox.h);

    const float iconReserved = gPlugin->decoration_appicon_enabled ? (VERTICAL ? topBarBox.w : topBarBox.h) : 0;
    float offset = gPlugin->decoration_padding + (BUTTONSRIGHT ? 0 : iconReserved);
    int idx = 0;
    for (auto &b : gPlugin->m_vButtons)
    {
        const float alongSize = VERTICAL ? b.size.y : b.size.x;
        const float acrossSize = VERTICAL ? b.size.x : b.size.y;
        Vector2D currentPos = Vector2D((double)(BUTTONSRIGHT ? BARBUF.x - gPlugin->bar_button_padding - alongSize - offset : offset), (double)(BARBUF.y - acrossSize) / 2.0).floor();

        if (VERTICAL)
        {
            if (VECINRECT(relativeCOORDS, currentPos.y, currentPos.x, currentPos.y + b.size.x, currentPos.x + b.size.y + gPlugin->bar_button_padding))
                return idx;
        }
        else
        {
            if (VECINRECT(relativeCOORDS, currentPos.x, currentPos.y, currentPos.x + b.size.x + gPlugin->bar_button_padding, currentPos.y + b.size.y))
                return idx;
        }

        offset += gPlugin->bar_button_padding + alongSize;
        idx++;
    }

    return -1;
}

void CHyprWindowDecorator::renderText(SP<CTexture> out, const std::string &text, const CHyprColor &color, const Vector2D &bufferSize, const float scale, const int fontSize)
{
    const auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bufferSize.x, bufferSize.y);
    const auto CAIRO = cairo_create(CAIROSURFACE);

    // clear the pixmap
    cairo_save(CAIRO);
    cairo_set_operator(CAIRO, CAIRO_OPERATOR_CLEAR);
    cairo_paint(CAIRO);
    cairo_restore(CAIRO);

    // draw title using Pango
    PangoLayout *layout = pango_cairo_create_layout(CAIRO);
    pango_layout_set_text(layout, text.c_str(), -1);

    PangoFontDescription *fontDesc = pango_font_description_from_string("sans");
    pango_font_description_set_size(fontDesc, fontSize * scale * PANGO_SCALE);
    pango_layout_set_font_description(layout, fontDesc);
    pango_font_description_free(fontDesc);

    const int maxWidth = bufferSize.x;

    pango_layout_set_width(layout, maxWidth * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);

    cairo_set_source_rgba(CAIRO, color.r, color.g, color.b, color.a);

    PangoRectangle ink_rect, logical_rect;
    pango_layout_get_extents(layout, &ink_rect, &logical_rect);

    const int layoutWidth = ink_rect.width;
    const int layoutHeight = logical_rect.height;

    const double xOffset = (bufferSize.x / 2.0 - layoutWidth / PANGO_SCALE / 2.0);
    const double yOffset = (bufferSize.y / 2.0 - layoutHeight / PANGO_SCALE / 2.0);

    cairo_move_to(CAIRO, xOffset, yOffset);
    pango_cairo_show_layout(CAIRO, layout);

    g_object_unref(layout);

    cairo_surface_flush(CAIROSURFACE);

    // copy the data to an OpenGL texture we have
    const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
    out->allocate();
    glBindTexture(GL_TEXTURE_2D, out->m_texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

#ifndef GLES2
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferSize.x, bufferSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, DATA);

    // delete cairo
    cairo_destroy(CAIRO);
    cairo_surface_destroy(CAIROSURFACE);
}

void CHyprWindowDecorator::renderBarTitle(const Vector2D &bufferSize, const float scale)
{
    const bool BUTTONSRIGHT = gPlugin->bar_buttons_alignment != "left";
    const bool VERTICAL = gPlugin->decoration_title_placement == "left" || gPlugin->decoration_title_placement == "right";

    const auto PWINDOW = m_pWindow.lock();

    const auto BORDERSIZE = PWINDOW->getRealBorderSize();

    float buttonSizes = gPlugin->bar_button_padding;
    for (auto &b : gPlugin->m_vButtons)
    {
        buttonSizes += (VERTICAL ? b.size.y : b.size.x) + gPlugin->bar_button_padding;
    }

    const auto scaledSize = gPlugin->decoration_title_size * scale;
    const auto scaledBorderSize = BORDERSIZE * scale;
    const auto scaledButtonsSize = buttonSizes * scale;
    const auto scaledBarPadding = gPlugin->decoration_padding * scale;

    const CHyprColor COLOR = m_bForcedTitleColor.value_or(gPlugin->col_text);

    const auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bufferSize.x, bufferSize.y);
    const auto CAIRO = cairo_create(CAIROSURFACE);

    // clear the pixmap
    cairo_save(CAIRO);
    cairo_set_operator(CAIRO, CAIRO_OPERATOR_CLEAR);
    cairo_paint(CAIRO);
    cairo_restore(CAIRO);

    if (VERTICAL)
    {
        cairo_translate(CAIRO, bufferSize.x / 2.0, bufferSize.y / 2.0);
        if (gPlugin->decoration_title_placement == "left")
            cairo_rotate(CAIRO, -M_PI / 2.0);
        else
            cairo_rotate(CAIRO, M_PI / 2.0);
        cairo_translate(CAIRO, -bufferSize.y / 2.0, -bufferSize.x / 2.0);
    }

    const float logicalWidth = VERTICAL ? bufferSize.y : bufferSize.x;
    const float logicalHeight = VERTICAL ? bufferSize.x : bufferSize.y;

    // draw title using Pango
    PangoLayout *layout = pango_cairo_create_layout(CAIRO);
    pango_layout_set_text(layout, m_szLastTitle.c_str(), -1);

    PangoFontDescription *fontDesc = pango_font_description_from_string(gPlugin->bar_text_font.c_str());
    pango_font_description_set_size(fontDesc, scaledSize * PANGO_SCALE);
    pango_layout_set_font_description(layout, fontDesc);
    pango_font_description_free(fontDesc);

    PangoContext *context = pango_layout_get_context(layout);
    pango_context_set_base_dir(context, PANGO_DIRECTION_NEUTRAL);

    const float iconReserved = gPlugin->decoration_appicon_enabled ? (VERTICAL ? bufferSize.x : bufferSize.y) : 0;
    const float paddingTotal = scaledBarPadding * 2 + scaledButtonsSize + iconReserved;
    const float maxWidth = std::max(0.0f, logicalWidth - paddingTotal);

    pango_layout_set_width(layout, (int)std::round(maxWidth * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    cairo_set_source_rgba(CAIRO, COLOR.r, COLOR.g, COLOR.b, COLOR.a);

    int layoutWidth, layoutHeight;
    pango_layout_get_size(layout, &layoutWidth, &layoutHeight);

    // Use float alignment (0.0 to 1.0)
    const float align = std::clamp(gPlugin->decoration_title_align, 0.0f, 1.0f);

    const float availableWidth = logicalWidth - paddingTotal;
    const int xOffset = std::round(scaledBarPadding + (BUTTONSRIGHT ? 0 : scaledButtonsSize) + iconReserved + (availableWidth - (float)layoutWidth / PANGO_SCALE) * align);
    const int yOffset = std::round((logicalHeight / 2.0 - layoutHeight / PANGO_SCALE / 2.0));

    cairo_move_to(CAIRO, xOffset, yOffset);
    pango_cairo_show_layout(CAIRO, layout);

    g_object_unref(layout);

    cairo_surface_flush(CAIROSURFACE);

    // copy the data to an OpenGL texture we have
    const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
    m_pTextTex->allocate();
    glBindTexture(GL_TEXTURE_2D, m_pTextTex->m_texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

#ifndef GLES2
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferSize.x, bufferSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, DATA);

    m_pTextTex->m_size = bufferSize;

    // delete cairo
    cairo_destroy(CAIRO);
    cairo_surface_destroy(CAIROSURFACE);
}

size_t CHyprWindowDecorator::getVisibleButtonCount(const Vector2D &bufferSize, const float scale)
{
    const bool VERTICAL = gPlugin->decoration_title_placement == "left" || gPlugin->decoration_title_placement == "right";
    const float iconReserved = gPlugin->decoration_appicon_enabled ? bufferSize.y : 0;
    float availableSpace = bufferSize.x - gPlugin->decoration_padding * scale * 2 - iconReserved;
    size_t count = 0;

    for (const auto &button : gPlugin->m_vButtons)
    {
        const float buttonSpace = ((VERTICAL ? button.size.y : button.size.x) + gPlugin->bar_button_padding) * scale;
        if (availableSpace >= buttonSpace)
        {
            count++;
            availableSpace -= buttonSpace;
        }
        else
            break;
    }

    return count;
}

bool CHyprWindowDecorator::renderBarButtons(const Vector2D &bufferSize, const float scale)
{
    const bool BUTTONSRIGHT = gPlugin->bar_buttons_alignment != "left";
    const auto visibleCount = getVisibleButtonCount(bufferSize, scale);

    const auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bufferSize.x, bufferSize.y);
    const auto CAIRO = cairo_create(CAIROSURFACE);

    // clear the pixmap
    cairo_save(CAIRO);
    cairo_set_operator(CAIRO, CAIRO_OPERATOR_CLEAR);
    cairo_paint(CAIRO);
    cairo_restore(CAIRO);

    // draw buttons
    const bool VERTICAL = gPlugin->decoration_title_placement == "left" || gPlugin->decoration_title_placement == "right";
    double offset = gPlugin->decoration_padding * scale;
    bool texturesLoaded = false;
    for (size_t i = 0; i < visibleCount; ++i)
    {
        auto &button = gPlugin->m_vButtons[i];
        const auto alongSize = (VERTICAL ? button.size.y : button.size.x);
        const auto scaledButtonSize = alongSize * scale;
        const auto scaledButtonsPad = gPlugin->bar_button_padding * scale;

        offset += scaledButtonsPad + scaledButtonSize;
    }

    // copy the data to an OpenGL texture we have
    const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
    m_pButtonsTex->allocate();
    glBindTexture(GL_TEXTURE_2D, m_pButtonsTex->m_texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

#ifndef GLES2
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferSize.x, bufferSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, DATA);

    m_pButtonsTex->m_size = bufferSize;

    // delete cairo
    cairo_destroy(CAIRO);
    cairo_surface_destroy(CAIROSURFACE);

    // Return true if textures were loaded, so caller knows to keep rendering
    return texturesLoaded;
}

void CHyprWindowDecorator::renderBarButtonsText(CBox *barBox, const float scale, const float a)
{
    const bool BUTTONSRIGHT = gPlugin->bar_buttons_alignment != "left";
    const bool VERTICAL = gPlugin->decoration_title_placement == "left" || gPlugin->decoration_title_placement == "right";
    const auto visibleCount = getVisibleButtonCount(VERTICAL ? Vector2D((double)barBox->height, (double)barBox->width) : Vector2D((double)barBox->width, (double)barBox->height), scale);
    const auto COORDS = cursorRelativeToBar();

    const float iconReserved = gPlugin->decoration_appicon_enabled ? (VERTICAL ? barBox->width : barBox->height) : 0;
    double offset = (gPlugin->decoration_padding * scale) + (BUTTONSRIGHT ? 0 : iconReserved);
    float noScaleOffset = gPlugin->decoration_padding + (BUTTONSRIGHT ? 0 : iconReserved / scale);

    int hoveredIdx = indexToButton(COORDS);

    for (size_t i = 0; i < visibleCount; ++i)
    {
        auto &button = gPlugin->m_vButtons[i];
        const auto scaledButtonSizeX = button.size.x * scale;
        const auto scaledButtonSizeY = button.size.y * scale;
        const auto scaledButtonsPad = gPlugin->bar_button_padding * scale;

        // check if hovering here
        bool hovering = (hoveredIdx == (int)i);

        // DEBUG_LOG("Rendering button {} at offset {}, hovering: {}", i, offset, hovering);

        CBox pos;

        if (!VERTICAL)
        {
            pos = {barBox->x + (int)std::round(BUTTONSRIGHT ? barBox->width - offset - scaledButtonSizeX : offset), (double)std::round(barBox->y + (barBox->height - scaledButtonSizeY) / 2.0), (double)std::round(scaledButtonSizeX), (double)std::round(scaledButtonSizeY)};
        }
        else
        {
            pos = {barBox->x + (int)std::round((barBox->width - scaledButtonSizeX) / 2.0), barBox->y + (int)std::round(BUTTONSRIGHT ? barBox->height - offset - scaledButtonSizeY : offset), (double)std::round(scaledButtonSizeX), (double)std::round(scaledButtonSizeY)};
        }

        noScaleOffset += gPlugin->bar_button_padding + (VERTICAL ? button.size.y : button.size.x);

        // Skip if textured button is not available
        if (button.texActive->m_texID == 0)
            continue;

        SP<CTexture> tex = button.texActive;
        if (!m_bWindowHasFocus)
            tex = button.texInactive;
        if (hovering)
            tex = button.texHover;
        if (m_iButtonPressedIdx == (int)i)
            tex = button.texPressed;

        if (tex->m_texID == 0)
            tex = button.texActive;

        CHyprOpenGLImpl::STextureRenderData data;
        data.a = a;
        g_pHyprOpenGL->renderTexture(tex, pos, data);
        offset += scaledButtonsPad + (VERTICAL ? scaledButtonSizeY : scaledButtonSizeX);

        bool currentBit = (m_iButtonHoverState & (1 << i)) != 0;
        if (hovering != currentBit)
        {
            m_iButtonHoverState ^= (1 << i);
            // damage to get rid of some artifacts when icons are "hidden"
            damageEntire();
        }
    }
}

void CHyprWindowDecorator::draw(PHLMONITOR pMonitor, const float &a)
{
    gPlugin->loadAllTextures();

    if (m_bLastEnabledState != gPlugin->enabled || m_bWindowSizeChanged)
    {
        m_bLastEnabledState = gPlugin->enabled;
        g_pDecorationPositioner->repositionDeco(this);
    }

    if (m_hidden || !validMapped(m_pWindow) || !gPlugin->enabled)
        return;

    const auto PWINDOW = m_pWindow.lock();

    if (!PWINDOW->m_ruleApplicator->decorate().valueOrDefault())
        return;

    auto data = CRenderPassElement::SBarData{this, a};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRenderPassElement>(data));
}

void CHyprWindowDecorator::renderPass(PHLMONITOR pMonitor, const float &a)
{
    const auto PWINDOW = m_pWindow.lock();

    bool windowFocus = PWINDOW == Desktop::focusState()->window();
    bool focusChanged = windowFocus != m_bWindowHasFocus;
    if (focusChanged)
    {
        m_bWindowHasFocus = windowFocus;
        m_bButtonsDirty = true;
        damageEntire();
    }

    const CHyprColor DEST_COLOR = m_bForcedBarColor.value_or(gPlugin->bar_color);
    if (DEST_COLOR != m_cRealBarColor->goal())
        *m_cRealBarColor = DEST_COLOR;

    CHyprColor color = m_cRealBarColor->value();

    color.a *= a;
    const bool BUTTONSRIGHT = gPlugin->bar_buttons_alignment != "left";
    const bool SHOULDBLUR = gPlugin->bar_blur && color.a < 1.F;
    const auto PWORKSPACE = PWINDOW->m_workspace;
    const auto WORKSPACEOFFSET = PWORKSPACE && !PWINDOW->m_pinned ? PWORKSPACE->m_renderOffset->value() : Vector2D();

    const auto ROUNDING = PWINDOW->rounding() + (gPlugin->bar_precedence_over_border ? 0 : PWINDOW->getRealBorderSize());

    const auto scaledRounding = ROUNDING > 0 ? ROUNDING * pMonitor->m_scale - 2 /* idk why but otherwise it looks bad due to the gaps */ : 0;

    m_seExtents = {Vector2D((double)gPlugin->decoration_offset_left, (double)gPlugin->decoration_offset_top),
                   Vector2D((double)gPlugin->decoration_offset_right, (double)gPlugin->decoration_offset_bottom)};

    const auto DECOBOX = assignedBoxGlobal();

    const auto BARBUF = DECOBOX.size() * pMonitor->m_scale;

    CBox titleBarBox = {DECOBOX.x - pMonitor->m_position.x, DECOBOX.y - pMonitor->m_position.y, DECOBOX.w, DECOBOX.h};

    titleBarBox.scale(pMonitor->m_scale).round();

    if (titleBarBox.w < 1 || titleBarBox.h < 1)
    {
        return;
    }

    g_pHyprOpenGL->scissor(titleBarBox);

    const int PBORDERINSET = gPlugin->decoration_inset;

    if (ROUNDING && !PBORDERINSET)
    {
        // the +1 is a shit garbage temp fix until renderRect supports an alpha matte
        CBox windowBox = {PWINDOW->m_realPosition->value().x - pMonitor->m_position.x + 1,
                          PWINDOW->m_realPosition->value().y - pMonitor->m_position.y + 1, PWINDOW->m_realSize->value().x - 2,
                          PWINDOW->m_realSize->value().y - 2};

        if (windowBox.w < 1 || windowBox.h < 1)
            return;

        glClearStencil(0);
        glClear(GL_STENCIL_BUFFER_BIT);

        g_pHyprOpenGL->setCapStatus(GL_STENCIL_TEST, true);

        glStencilFunc(GL_ALWAYS, 1, -1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        windowBox.scale(pMonitor->m_scale).round();
        g_pHyprOpenGL->renderRect(windowBox, CHyprColor(0, 0, 0, 0), {.round = (int)scaledRounding, .roundingPower = m_pWindow->roundingPower()});
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glStencilFunc(GL_NOTEQUAL, 1, -1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    }

    const auto &NPI = m_bWindowHasFocus ? gPlugin->activeNinepatch : gPlugin->inactiveNinepatch;
    float border[4] = {NPI.border[0], NPI.border[1], NPI.border[2], NPI.border[3]};
    cairo_surface_t *sourceSurface = m_bWindowHasFocus ? gPlugin->activeSurface : gPlugin->inactiveSurface;

    if (sourceSurface)
    {
        if (m_bWindowSizeChanged || m_pBarFinalTex->m_texID == 0 || focusChanged || m_bNinePatchChanged)
        {
            const auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, titleBarBox.width, titleBarBox.height);
            const auto CAIRO = cairo_create(CAIROSURFACE);

            cairo_set_operator(CAIRO, CAIRO_OPERATOR_CLEAR);
            cairo_paint(CAIRO);
            cairo_set_operator(CAIRO, CAIRO_OPERATOR_OVER);

            const int sw = cairo_image_surface_get_width(sourceSurface);
            const int sh = cairo_image_surface_get_height(sourceSurface);

            double sx[4] = {0, border[0], sw - border[2], (double)sw};
            double sy[4] = {0, border[1], sh - border[3], (double)sh};

            double dx[4] = {0, (sx[1] - sx[0]) * pMonitor->m_scale, (double)titleBarBox.width - (sx[3] - sx[2]) * pMonitor->m_scale, (double)titleBarBox.width};
            double dy[4] = {0, (sy[1] - sy[0]) * pMonitor->m_scale, (double)titleBarBox.height - (sy[3] - sy[2]) * pMonitor->m_scale, (double)titleBarBox.height};

            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    double middleAlphaVal = (i == 1 && j == 1) ? gPlugin->ninepatch_middle_alpha : 1.0;
                    if (middleAlphaVal <= 0)
                        continue;

                    cairo_save(CAIRO);
                    if (middleAlphaVal < 1.0)
                    {
                        cairo_push_group(CAIRO);
                    }

                    bool isMiddlePatch = (i == 1 || j == 1);
                    if (gPlugin->ninepatch_repeat && isMiddlePatch)
                    {
                        drawRepeatedSurface(CAIRO, sourceSurface, sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j], dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]);
                    }
                    else
                    {
                        drawSizedSurface(CAIRO, sourceSurface, sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j], dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]);
                    }

                    if (middleAlphaVal < 1.0)
                    {
                        cairo_pop_group_to_source(CAIRO);
                        cairo_paint_with_alpha(CAIRO, middleAlphaVal);
                    }
                    cairo_restore(CAIRO);
                }
            }

            cairo_surface_flush(CAIROSURFACE);
            const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
            m_pBarFinalTex->allocate();
            glBindTexture(GL_TEXTURE_2D, m_pBarFinalTex->m_texID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gPlugin->ninepatch_linear_filtering ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gPlugin->ninepatch_linear_filtering ? GL_LINEAR : GL_NEAREST);
#ifndef GLES2
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, titleBarBox.width, titleBarBox.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, DATA);
            m_pBarFinalTex->m_size = {(double)titleBarBox.width, (double)titleBarBox.height};

            cairo_destroy(CAIRO);
            cairo_surface_destroy(CAIROSURFACE);
            m_bNinePatchChanged = false;
        }
        CHyprOpenGLImpl::STextureRenderData data;
        data.a = a;
        g_pHyprOpenGL->renderTexture(m_pBarFinalTex, titleBarBox, data);
    }
    else
    {
        if (SHOULDBLUR)
            g_pHyprOpenGL->renderRect(titleBarBox, color, {.round = (int)scaledRounding, .roundingPower = m_pWindow->roundingPower(), .blur = true, .blurA = a});
        else
            g_pHyprOpenGL->renderRect(titleBarBox, color, {.round = (int)scaledRounding, .roundingPower = m_pWindow->roundingPower()});
    }

    if (m_bWindowSizeChanged)
        m_bButtonsDirty = true;

    const auto P = gPlugin->decoration_padding;
    CBox topBarBox;
    if (gPlugin->decoration_title_placement == "top")
    {
        topBarBox = {titleBarBox.x + (int)std::round((gPlugin->decoration_offset_left + NPI.padding[0] + P) * pMonitor->m_scale),
                     titleBarBox.y,
                     (double)std::round(titleBarBox.width - (gPlugin->decoration_offset_left + gPlugin->decoration_offset_right + NPI.padding[0] + NPI.padding[2] + 2 * P) * pMonitor->m_scale),
                     (double)std::round((gPlugin->decoration_offset_top + NPI.padding[1] + P) * pMonitor->m_scale)};
    }
    else if (gPlugin->decoration_title_placement == "bottom")
    {
        topBarBox = {titleBarBox.x + (int)std::round((gPlugin->decoration_offset_left + NPI.padding[0] + P) * pMonitor->m_scale),
                     titleBarBox.y + (int)std::round(titleBarBox.height - (gPlugin->decoration_offset_bottom + NPI.padding[3] + P) * pMonitor->m_scale),
                     (double)std::round(titleBarBox.width - (gPlugin->decoration_offset_left + gPlugin->decoration_offset_right + NPI.padding[0] + NPI.padding[2] + 2 * P) * pMonitor->m_scale),
                     (double)std::round((gPlugin->decoration_offset_bottom + NPI.padding[3] + P) * pMonitor->m_scale)};
    }
    else if (gPlugin->decoration_title_placement == "left")
    {
        topBarBox = {titleBarBox.x,
                     titleBarBox.y + (int)std::round((gPlugin->decoration_offset_top + NPI.padding[1] + P) * pMonitor->m_scale),
                     (double)std::round((gPlugin->decoration_offset_left + NPI.padding[0] + P) * pMonitor->m_scale),
                     (double)std::round(titleBarBox.height - (gPlugin->decoration_offset_top + gPlugin->decoration_offset_bottom + NPI.padding[1] + NPI.padding[3] + 2 * P) * pMonitor->m_scale)};
    }
    else if (gPlugin->decoration_title_placement == "right")
    {
        topBarBox = {titleBarBox.x + (int)std::round(titleBarBox.width - (gPlugin->decoration_offset_right + NPI.padding[2] + P) * pMonitor->m_scale),
                     titleBarBox.y + (int)std::round((gPlugin->decoration_offset_top + NPI.padding[1] + P) * pMonitor->m_scale),
                     (double)std::round((gPlugin->decoration_offset_right + NPI.padding[2] + P) * pMonitor->m_scale),
                     (double)std::round(titleBarBox.height - (gPlugin->decoration_offset_top + gPlugin->decoration_offset_bottom + NPI.padding[1] + NPI.padding[3] + 2 * P) * pMonitor->m_scale)};
    }

    // render app icon
    if (gPlugin->decoration_appicon_enabled)
    {
        std::string appId = PWINDOW->m_initialClass;

        int iconSizeDesired = (int)(std::max(topBarBox.width, topBarBox.height) * 0.6); // Reasonable size check
        if (gPlugin->decoration_title_placement == "top" || gPlugin->decoration_title_placement == "bottom")
            iconSizeDesired = (int)(topBarBox.height * 0.6);
        else
            iconSizeDesired = (int)(topBarBox.width * 0.6);

        if (appId != m_szLastAppId || m_pAppIconTex->m_texID == 0 || (int)m_pAppIconTex->m_size.x != iconSizeDesired)
        {
            m_szLastAppId = appId;

            // Reset the texture
            if (m_pAppIconTex->m_texID != 0)
            {
                m_pAppIconTex->destroyTexture();
                m_pAppIconTex = makeShared<CTexture>();
            }

            loadAppIcon(appId, m_pAppIconTex, iconSizeDesired);
        }

        if (m_pAppIconTex->m_texID != 0)
        {
            int iconPad;
            CBox iconBox;
            const auto ATEXSIZE = m_pAppIconTex->m_size;
            if (gPlugin->decoration_title_placement == "top" || gPlugin->decoration_title_placement == "bottom")
            {
                iconPad = (int)std::round(topBarBox.height * 0.2);
                iconBox = {topBarBox.x + iconPad + gPlugin->decoration_appicon_offset.x * pMonitor->m_scale, topBarBox.y + (int)std::round((topBarBox.height - ATEXSIZE.y) / 2.0) + gPlugin->decoration_appicon_offset.y * pMonitor->m_scale, ATEXSIZE.x, ATEXSIZE.y};
            }
            else
            {
                iconPad = (int)std::round(topBarBox.width * 0.2);
                iconBox = {topBarBox.x + (int)std::round((topBarBox.width - ATEXSIZE.x) / 2.0) + gPlugin->decoration_appicon_offset.x * pMonitor->m_scale, topBarBox.y + iconPad + gPlugin->decoration_appicon_offset.y * pMonitor->m_scale, ATEXSIZE.x, ATEXSIZE.y};
            }
            CHyprOpenGLImpl::STextureRenderData data;
            data.a = a;
            g_pHyprOpenGL->renderTexture(m_pAppIconTex, iconBox, data);
        }
    }

    // render title
    if (gPlugin->decoration_title_enabled && (m_szLastTitle != PWINDOW->m_title || m_bWindowSizeChanged || m_pTextTex->m_texID == 0 || m_bTitleColorChanged))
    {
        m_szLastTitle = PWINDOW->m_title;
        renderBarTitle(Vector2D((double)topBarBox.width, (double)topBarBox.height), pMonitor->m_scale);
    }

    if (ROUNDING)
    {
        // cleanup stencil
        glClearStencil(0);
        glClear(GL_STENCIL_BUFFER_BIT);
        g_pHyprOpenGL->setCapStatus(GL_STENCIL_TEST, false);
        glStencilMask(-1);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
    }

    if (gPlugin->decoration_title_enabled && m_pTextTex->m_texID)
    {
        // render title texture at full bar size (text is already positioned within the texture)
        CBox textBox = {topBarBox.x, topBarBox.y, (double)m_pTextTex->m_size.x, (double)m_pTextTex->m_size.y};
        CHyprOpenGLImpl::STextureRenderData data;
        data.a = a;
        g_pHyprOpenGL->renderTexture(m_pTextTex, textBox, data);
    }

    if (m_bButtonsDirty || m_bWindowSizeChanged)
    {
        const bool VERTICAL = gPlugin->decoration_title_placement == "left" || gPlugin->decoration_title_placement == "right";
        m_bButtonsDirty = renderBarButtons(VERTICAL ? Vector2D((double)topBarBox.height, (double)topBarBox.width) : Vector2D((double)topBarBox.width, (double)topBarBox.height), pMonitor->m_scale);
    }

    if (m_pButtonsTex->m_texID)
    {
        CHyprOpenGLImpl::STextureRenderData data;
        data.a = a;
        g_pHyprOpenGL->renderTexture(m_pButtonsTex, topBarBox, data);
    }

    g_pHyprOpenGL->scissor(nullptr);

    renderBarButtonsText(&topBarBox, pMonitor->m_scale, a);

    m_bWindowSizeChanged = false;
    m_bTitleColorChanged = false;
    m_bButtonsDirty = false;

    // dynamic updates change the extents
    if (m_iLastHeight != gPlugin->decoration_offset_top)
    {
        g_pLayoutManager->getCurrentLayout()->recalculateWindow(PWINDOW);
        m_iLastHeight = gPlugin->decoration_offset_top;
    }
}

eDecorationType CHyprWindowDecorator::getDecorationType()
{
    return DECORATION_CUSTOM;
}

void CHyprWindowDecorator::updateWindow(PHLWINDOW pWindow)
{
    damageEntire();
}

void CHyprWindowDecorator::damageEntire()
{
    g_pHyprRenderer->damageBox(assignedBoxGlobal());
}

Vector2D CHyprWindowDecorator::cursorRelativeToBar()
{
    return g_pInputManager->getMouseCoordsInternal() - assignedBoxGlobal().pos();
}

bool CHyprWindowDecorator::isMouseOnBar()
{
    return isPointOnBar(cursorRelativeToBar());
}

bool CHyprWindowDecorator::isPointOnBar(Vector2D COORDS)
{
    const auto BOX = assignedBoxGlobal();

    if (!VECINRECT(COORDS, 0, 0, BOX.w, BOX.h))
        return false;

    const auto &NPI = gPlugin->activeNinepatch.defined ? gPlugin->activeNinepatch : gPlugin->inactiveNinepatch;
    const auto P = gPlugin->decoration_padding;
    const auto L = gPlugin->decoration_offset_left + NPI.padding[0] + P;
    const auto T = gPlugin->decoration_offset_top + NPI.padding[1] + P;
    const auto R = gPlugin->decoration_offset_right + NPI.padding[2] + P;
    const auto B = gPlugin->decoration_offset_bottom + NPI.padding[3] + P;

    if (COORDS.x >= L && COORDS.x < BOX.w - R && COORDS.y >= T && COORDS.y < BOX.h - B)
        return false;

    return true;
}

eDecorationLayer CHyprWindowDecorator::getDecorationLayer()
{
    return gPlugin->decoration_render_above ? DECORATION_LAYER_OVER : DECORATION_LAYER_UNDER;
}

uint64_t CHyprWindowDecorator::getDecorationFlags()
{
    return DECORATION_ALLOWS_MOUSE_INPUT | (gPlugin->bar_part_of_window ? DECORATION_PART_OF_MAIN_WINDOW : 0);
}

CBox CHyprWindowDecorator::assignedBoxGlobal()
{
    if (!validMapped(m_pWindow))
        return {};

    const auto PWINDOW = m_pWindow.lock();

    CBox box = m_bAssignedBox;
    box.translate(PWINDOW->m_realPosition->value());

    const auto PWORKSPACE = PWINDOW->m_workspace;
    if (PWORKSPACE && !PWINDOW->m_pinned)
        box.translate(PWORKSPACE->m_renderOffset->value());

    return box;
}

PHLWINDOW CHyprWindowDecorator::getOwner()
{
    return m_pWindow.lock();
}

void CHyprWindowDecorator::updateRules()
{
    const auto PWINDOW = m_pWindow.lock();
    auto prevHidden = m_hidden;
    auto prevForcedTitleColor = m_bForcedTitleColor;

    m_bForcedBarColor = std::nullopt;
    m_bForcedTitleColor = std::nullopt;
    m_hidden = false;

    if (PWINDOW->m_ruleApplicator->m_otherProps.props.contains(gPlugin->m_nobarRuleIdx))
        m_hidden = truthy(PWINDOW->m_ruleApplicator->m_otherProps.props.at(gPlugin->m_nobarRuleIdx)->effect);
    if (PWINDOW->m_ruleApplicator->m_otherProps.props.contains(gPlugin->m_barColorRuleIdx))
        m_bForcedBarColor = CHyprColor(configStringToInt(PWINDOW->m_ruleApplicator->m_otherProps.props.at(gPlugin->m_barColorRuleIdx)->effect).value_or(0));
    if (PWINDOW->m_ruleApplicator->m_otherProps.props.contains(gPlugin->m_titleColorRuleIdx))
        m_bForcedTitleColor = CHyprColor(configStringToInt(PWINDOW->m_ruleApplicator->m_otherProps.props.at(gPlugin->m_titleColorRuleIdx)->effect).value_or(0));

    if (prevHidden != m_hidden)
        g_pDecorationPositioner->repositionDeco(this);
    if (prevForcedTitleColor != m_bForcedTitleColor)
        m_bTitleColorChanged = true;
}

void CHyprWindowDecorator::invalidateTextures()
{
    m_pBarFinalTex = makeShared<CTexture>();
    m_pTextTex = makeShared<CTexture>();
    m_pButtonsTex = makeShared<CTexture>();

    // Mark everything as dirty to force full re-render
    m_bWindowSizeChanged = true;
    m_bButtonsDirty = true;
    m_bTitleColorChanged = true;
    m_bNinePatchChanged = true;
    m_szLastTitle = ""; // Force title re-render

    damageEntire();
}

void CHyprWindowDecorator::damageOnButtonHover()
{
    const auto COORDS = cursorRelativeToBar();
    const int idx = indexToButton(COORDS);

    uint32_t newHoverState = 0;
    if (idx != -1)
        newHoverState = (1 << idx);

    if (newHoverState != m_iButtonHoverState)
    {
        m_iButtonHoverState = newHoverState;
        damageEntire();
    }
}
