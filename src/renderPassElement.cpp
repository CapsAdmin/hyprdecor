#include "renderPassElement.hpp"
#include <hyprland/src/render/OpenGL.hpp>
#include "hyprWindowDecorator.hpp"
#include "plugin.hpp"

CRenderPassElement::CRenderPassElement(const CRenderPassElement::SBarData &data_) : data(data_)
{
    ;
}

void CRenderPassElement::draw(const CRegion &damage)
{
    data.deco->renderPass(g_pHyprOpenGL->m_renderData.pMonitor.lock(), data.a);
}

bool CRenderPassElement::needsLiveBlur()
{
    static auto *const PENABLEBLURGLOBAL = (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(gPlugin->m_pHandle, "decoration:blur:enabled")->getDataStaticPtr();
    if (!**PENABLEBLURGLOBAL)
        return false;

    CHyprColor color = data.deco->m_bForcedBarColor.value_or(gPlugin->bar_color);
    color.a *= data.a;
    const bool SHOULDBLUR = gPlugin->bar_blur && gPlugin->bar_blur && color.a < 1.F;

    return SHOULDBLUR;
}

std::optional<CBox> CRenderPassElement::boundingBox()
{
    // Temporary fix: expand the bar bb a bit, otherwise occlusion gets too aggressive.
    return data.deco->assignedBoxGlobal().translate(-g_pHyprOpenGL->m_renderData.pMonitor->m_position).expand(10);
}

bool CRenderPassElement::needsPrecomputeBlur()
{
    return false;
}