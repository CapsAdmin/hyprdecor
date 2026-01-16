#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/devices/ITouch.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRule.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <cairo/cairo.h>
#include "plugin.hpp"

#define private public
#include <hyprland/src/managers/input/InputManager.hpp>
#undef private

class CHyprWindowDecorator : public IHyprWindowDecoration
{
public:
  CHyprWindowDecorator(PHLWINDOW);
  virtual ~CHyprWindowDecorator();

  virtual SDecorationPositioningInfo getPositioningInfo();

  virtual void onPositioningReply(const SDecorationPositioningReply &reply);

  virtual void draw(PHLMONITOR, float const &a);

  virtual eDecorationType getDecorationType();

  virtual void updateWindow(PHLWINDOW);

  virtual void damageEntire();

  virtual eDecorationLayer getDecorationLayer();

  virtual uint64_t getDecorationFlags();

  bool m_bButtonsDirty = true;

  virtual std::string getDisplayName();

  PHLWINDOW getOwner();

  void updateRules();

  void invalidateTextures();

  CHyprWindowDecorator *m_self;

private:
  SBoxExtents m_seExtents;

  PHLWINDOWREF m_pWindow;

  CBox m_bAssignedBox;

  SP<CTexture> m_pTextTex;
  SP<CTexture> m_pButtonsTex;
  SP<CTexture> m_pBarFinalTex;

  SP<CTexture> m_pAppIconTex;
  std::string m_szLastAppId;

  bool m_bWindowSizeChanged = false;
  bool m_hidden = false;
  bool m_bTitleColorChanged = false;
  bool m_bLastEnabledState = false;
  bool m_bWindowHasFocus = false;
  bool m_bNinePatchChanged = false;
  std::optional<CHyprColor> m_bForcedBarColor;
  std::optional<CHyprColor> m_bForcedTitleColor;

  Time::steady_tp m_lastMouseDown = Time::steadyNow();

  PHLANIMVAR<CHyprColor> m_cRealBarColor;

  Vector2D cursorRelativeToBar();
  bool isMouseOnBar();

  void renderPass(PHLMONITOR, float const &a);
  void renderBarTitle(const Vector2D &bufferSize, const float scale);
  void renderText(SP<CTexture> out, const std::string &text, const CHyprColor &color, const Vector2D &bufferSize, const float scale, const int fontSize);
  bool renderBarButtons(const Vector2D &bufferSize, const float scale);
  void renderBarButtonsText(CBox *barBox, const float scale, const float a);
  void renderNinePatch(SP<CTexture> tex, const CBox &box, const float margins[4], const float a, const float middleAlpha);
  void damageOnButtonHover();

  bool inputIsValid();
  bool isPointOnBar(Vector2D COORDS);
  void onMouseButton(SCallbackInfo &info, IPointer::SButtonEvent e);
  void onTouchDown(SCallbackInfo &info, ITouch::SDownEvent e);
  void onTouchUp(SCallbackInfo &info, ITouch::SUpEvent e);
  void onMouseMove(Vector2D coords);
  void onTouchMove(SCallbackInfo &info, ITouch::SMotionEvent e);

  void handleDownEvent(SCallbackInfo &info, std::optional<ITouch::SDownEvent> touchEvent);
  void handleUpEvent(SCallbackInfo &info);
  void handleMovement();
  int indexToButton(Vector2D COORDS);

  CBox assignedBoxGlobal();

  SP<HOOK_CALLBACK_FN> m_pMouseButtonCallback;
  SP<HOOK_CALLBACK_FN> m_pTouchDownCallback;
  SP<HOOK_CALLBACK_FN> m_pTouchUpCallback;

  SP<HOOK_CALLBACK_FN> m_pTouchMoveCallback;
  SP<HOOK_CALLBACK_FN> m_pMouseMoveCallback;

  std::string m_szLastTitle;
  Vector2D m_vLastTitleSize;

  bool m_bDraggingThis = false;
  bool m_bTouchEv = false;
  bool m_bDragPending = false;
  bool m_bCancelledDown = false;
  int m_touchId = 0;

  int m_iButtonPressedIdx = -1;

  // store hover state for buttons as a bitfield
  unsigned int m_iButtonHoverState = 0;

  // for dynamic updates
  int m_iLastHeight = 0;

  size_t getVisibleButtonCount(const Vector2D &bufferSize, const float scale);

  friend class CRenderPassElement;
};
