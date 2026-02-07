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
#include <filesystem>
#include <fstream>
#include <sstream>
#include "plugin.hpp"

static cairo_surface_t *loadSurface(std::string path, SNinePatchInfo *pInfo = nullptr, int targetSize = 0)
{
    if (path.empty())
        return nullptr;

    // Handle PNG files
    cairo_surface_t *rawSurface = cairo_image_surface_create_from_png(path.c_str());

    if (cairo_surface_status(rawSurface) != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy(rawSurface);
        return nullptr;
    }

    if (!pInfo && targetSize > 0)
    {
        int w = cairo_image_surface_get_width(rawSurface);
        int h = cairo_image_surface_get_height(rawSurface);

        cairo_surface_t *scaledSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, targetSize, targetSize);
        cairo_t *cr = cairo_create(scaledSurface);

        double scale = (double)targetSize / std::max(w, h);
        double sw = w * scale;
        double sh = h * scale;

        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, rawSurface, (targetSize / scale - w) / 2.0, (targetSize / scale - h) / 2.0);
        cairo_paint(cr);

        cairo_destroy(cr);
        cairo_surface_destroy(rawSurface);
        return scaledSurface;
    }

    if (!pInfo)
        return rawSurface;

    // Parse ninepatch if pInfo is provided
    int w = cairo_image_surface_get_width(rawSurface);
    int h = cairo_image_surface_get_height(rawSurface);

    if (w < 3 || h < 3)
        return rawSurface;

    unsigned char *data = cairo_image_surface_get_data(rawSurface);
    int stride = cairo_image_surface_get_stride(rawSurface);

    auto isBlack = [&](int x, int y)
    {
        uint32_t pixel = *(uint32_t *)(data + y * stride + x * 4);
        // Cairo ARGB32: 0xAARRGGBB. Standard .9.png markers are opaque black.
        return ((pixel >> 24) & 0xFF) == 255 && ((pixel >> 16) & 0xFF) == 0 && ((pixel >> 8) & 0xFF) == 0 && (pixel & 0xFF) == 0;
    };

    // Extract markers
    // Top (stretch X)
    int firstX = -1, lastX = -1;
    for (int x = 1; x < w - 1; ++x)
    {
        if (isBlack(x, 0))
        {
            if (firstX == -1)
                firstX = x;
            lastX = x;
        }
    }
    if (firstX != -1)
    {
        pInfo->border[0] = firstX - 1;
        pInfo->border[2] = (w - 2) - lastX;
    }

    // Left (stretch Y)
    int firstY = -1, lastY = -1;
    for (int y = 1; y < h - 1; ++y)
    {
        if (isBlack(0, y))
        {
            if (firstY == -1)
                firstY = y;
            lastY = y;
        }
    }
    if (firstY != -1)
    {
        pInfo->border[1] = firstY - 1;
        pInfo->border[3] = (h - 2) - lastY;
    }

    // Bottom (content X)
    firstX = -1, lastX = -1;
    for (int x = 1; x < w - 1; ++x)
    {
        if (isBlack(x, h - 1))
        {
            if (firstX == -1)
                firstX = x;
            lastX = x;
        }
    }
    if (firstX != -1)
    {
        pInfo->padding[0] = firstX - 1;
        pInfo->padding[2] = (w - 2) - lastX;
    }

    // Right (content Y)
    firstY = -1, lastY = -1;
    for (int y = 1; y < h - 1; ++y)
    {
        if (isBlack(w - 1, y))
        {
            if (firstY == -1)
                firstY = y;
            lastY = y;
        }
    }
    if (firstY != -1)
    {
        pInfo->padding[1] = firstY - 1;
        pInfo->padding[3] = (h - 2) - lastY;
    }

    pInfo->defined = true;

    cairo_surface_t *cropped = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w - 2, h - 2);
    cairo_t *cr = cairo_create(cropped);
    cairo_set_source_surface(cr, rawSurface, -1, -1);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(rawSurface);

    return cropped;
}

static void drawSizedSurface(cairo_t *cr, cairo_surface_t *surface, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh)
{
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    cairo_save(cr);
    cairo_rectangle(cr, dx, dy, dw, dh);
    cairo_clip(cr);
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, dw / sw, dh / sh);
    cairo_set_source_surface(cr, surface, -sx, -sy);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void drawRepeatedSurface(cairo_t *cr, cairo_surface_t *surface, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh)
{
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    cairo_save(cr);
    cairo_rectangle(cr, dx, dy, dw, dh);
    cairo_clip(cr);

    // Create a pattern from the surface region
    cairo_surface_t *patternSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    cairo_t *patternCr = cairo_create(patternSurface);
    cairo_set_source_surface(patternCr, surface, -sx, -sy);
    cairo_paint(patternCr);
    cairo_destroy(patternCr);

    // Create repeating pattern
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(patternSurface);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);

    // Position and paint the pattern
    cairo_translate(cr, dx, dy);
    cairo_set_source(cr, pattern);
    cairo_paint(cr);

    cairo_pattern_destroy(pattern);
    cairo_surface_destroy(patternSurface);
    cairo_restore(cr);
}

static void loadTexture(std::string path, SP<CTexture> &out, bool linear = true, int targetSize = 0)
{
    if (path.empty() || out->m_texID != 0)
        return;

    const auto CAIROSURFACE = loadSurface(path, nullptr, targetSize);
    if (!CAIROSURFACE)
        return;

    const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
    const auto WIDTH = cairo_image_surface_get_width(CAIROSURFACE);
    const auto HEIGHT = cairo_image_surface_get_height(CAIROSURFACE);

    out->allocate();
    glBindTexture(GL_TEXTURE_2D, out->m_texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);

#ifndef GLES2
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, DATA);

    out->m_size = {(double)WIDTH, (double)HEIGHT};

    cairo_surface_destroy(CAIROSURFACE);
}

static std::string findDesktopFile(const std::string &appId)
{
    std::vector<std::string> searchPaths;

    // Use XDG_DATA_HOME (defaults to ~/.local/share)
    const char *xdgDataHome = getenv("XDG_DATA_HOME");
    if (xdgDataHome && xdgDataHome[0] != '\0')
    {
        searchPaths.push_back(std::string(xdgDataHome) + "/applications/");
    }
    else
    {
        const char *home = getenv("HOME");
        if (home)
            searchPaths.push_back(std::string(home) + "/.local/share/applications/");
    }

    // Use XDG_DATA_DIRS (defaults to /usr/local/share:/usr/share)
    const char *xdgDataDirs = getenv("XDG_DATA_DIRS");
    std::string dataDirs = xdgDataDirs ? xdgDataDirs : "/usr/local/share:/usr/share";

    std::stringstream ss(dataDirs);
    std::string dir;
    while (std::getline(ss, dir, ':'))
    {
        if (!dir.empty())
            searchPaths.push_back(dir + "/applications/");
    }

    // Also add Flatpak paths
    const char *home = getenv("HOME");
    if (home)
    {
        searchPaths.push_back(std::string(home) + "/.local/share/flatpak/exports/share/applications/");
    }
    searchPaths.push_back("/var/lib/flatpak/exports/share/applications/");

    // Search for desktop file
    for (const auto &path : searchPaths)
    {
        if (!std::filesystem::exists(path))
            continue;

        std::string desktop = path + appId + ".desktop";
        if (std::filesystem::exists(desktop))
            return desktop;
    }

    return "";
}

static std::string getIconFromDesktop(const std::string &desktopPath)
{
    std::ifstream file(desktopPath);
    if (!file.is_open())
        return "";

    std::string line;
    bool inDesktopEntry = false;

    while (std::getline(file, line))
    {
        // Check if we're in the [Desktop Entry] section
        if (line == "[Desktop Entry]")
            inDesktopEntry = true;
        else if (!line.empty() && line[0] == '[')
            inDesktopEntry = false;

        if (inDesktopEntry && line.find("Icon=") == 0)
        {
            return line.substr(5);
        }
    }
    return "";
}

static std::string resolveIconPath(const std::string &iconName)
{
    if (iconName.empty())
        return "";

    // If already an absolute path
    if (iconName[0] == '/' && std::filesystem::exists(iconName))
        return iconName;

    std::vector<std::string> iconPaths;
    const char *home = getenv("HOME");

    // Common icon sizes to search (in order of preference)
    std::vector<std::string> sizes = {"48x48", "64x64", "128x128", "scalable"};

    // Common icon themes (hicolor is the fallback theme per freedesktop spec)
    std::vector<std::string> themes = {"hicolor", "breeze", "oxygen", "Adwaita", "Papirus"};

    // Get XDG data directories for icons
    std::vector<std::string> dataDirs;

    // XDG_DATA_HOME
    const char *xdgDataHome = getenv("XDG_DATA_HOME");
    if (xdgDataHome && xdgDataHome[0] != '\0')
    {
        dataDirs.push_back(std::string(xdgDataHome));
    }
    else if (home)
    {
        dataDirs.push_back(std::string(home) + "/.local/share");
    }

    // XDG_DATA_DIRS
    const char *xdgDataDirs = getenv("XDG_DATA_DIRS");
    std::string dataDir = xdgDataDirs ? xdgDataDirs : "/usr/local/share:/usr/share";

    std::stringstream ss(dataDir);
    std::string dir;
    while (std::getline(ss, dir, ':'))
    {
        if (!dir.empty())
        {
            dataDirs.push_back(dir);
        }
    }

    // Search in icon themes
    for (const auto &baseDir : dataDirs)
    {
        for (const auto &theme : themes)
        {
            for (const auto &size : sizes)
            {
                // Standard structure: icons/{theme}/{size}/apps/{icon}
                iconPaths.push_back(baseDir + "/icons/" + theme + "/" + size + "/apps/" + iconName + ".png");
                // Oxygen structure: icons/{theme}/base/{size}/apps/{icon}
                iconPaths.push_back(baseDir + "/icons/" + theme + "/base/" + size + "/apps/" + iconName + ".png");
                // Breeze structure: icons/{theme}/apps/{size}/{icon} (size without 'x')
                std::string sizeNum = size;
                if (size.find("x") != std::string::npos)
                    sizeNum = size.substr(0, size.find("x")); // "48x48" -> "48"
                iconPaths.push_back(baseDir + "/icons/" + theme + "/apps/" + sizeNum + "/" + iconName + ".png");
            }
        }
        // Also check pixmaps as fallback
        iconPaths.push_back(baseDir + "/pixmaps/" + iconName + ".png");
        iconPaths.push_back(baseDir + "/pixmaps/" + iconName);
    }

    for (const auto &path : iconPaths)
    {
        if (std::filesystem::exists(path) && path.ends_with(".png"))
        {
            return path;
        }
    }

    return "";
}

static void loadAppIcon(const std::string &appId, SP<CTexture> &out, int targetSize)
{
    if (appId.empty())
        return;

    auto desktopFile = findDesktopFile(appId);
    if (desktopFile.empty())
        return;

    auto iconName = getIconFromDesktop(desktopFile);
    if (iconName.empty())
        return;

    auto iconPath = resolveIconPath(iconName);
    if (iconPath.empty() || !iconPath.ends_with(".png"))
        return;

    loadTexture(iconPath, out, true, targetSize);
}
