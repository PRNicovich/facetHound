#pragma once

// Lightweight RP2040/TFT_eSPI ports of the two matplotlib display modes.
// This file intentionally uses only TFT_eSPI primitives and the built-in fonts.

#include <TFT_eSPI.h>
#include <math.h>

enum DisplayMode : uint8_t {
  DISPLAY_LEGACY = 0,
  DISPLAY_DYNAMIC = 1,
  DISPLAY_STATIC = 2
};

struct PythonUiState {
  float theta;
  float thetaError;
  float phi;
  float phiError;
  float z;
  float force;
  float flow;
  long rpmSet;
  uint32_t rpmActual;
  int wheel;
  int tier;
  int tierCount;
  int facet;
  int facetCount;
  int zStep;
  int motorDirection;
  bool motorRunning;
  bool flowRunning;
  float viewYaw;
  float viewPitch;
  String title;
};

namespace Gen2Display {

static const uint16_t C_BG = 0x0021;
static const uint16_t C_PANEL = 0x0842;
static const uint16_t C_PANEL_EDGE = 0x1126;
static const uint16_t C_WHITE = 0xEFBF;
static const uint16_t C_DIM = 0x7BEF;
static const uint16_t C_THETA = 0xFEC9;
static const uint16_t C_THETA_SOFT = 0xFF73;
static const uint16_t C_PHI = 0x471F;
static const uint16_t C_PHI_SOFT = 0x9F7F;
static const uint16_t C_GREEN = 0x27E8;
static const uint16_t C_AMBER = 0xFCE0;
static const uint16_t C_MAGENTA = 0xFB3B;

struct V3 { float x, y, z; };

static V3 ringPoint(uint8_t i, uint8_t count, float radius, float z, float phase = 0.0f) {
  float a = 2.0f * PI * ((float)i / count) + phase;
  return {radius * cosf(a), radius * sinf(a), z};
}

static V3 vertex(uint8_t i) {
  if (i < 8) return ringPoint(i, 8, 0.34f, 0.45f, PI / 8.0f);
  if (i < 24) return ringPoint(i - 8, 16, 1.0f, 0.04f);
  if (i < 40) return ringPoint(i - 24, 16, 1.0f, -0.04f);
  if (i < 48) return ringPoint(i - 40, 8, 0.48f, -0.50f, PI / 8.0f);
  return {0.0f, 0.0f, -0.78f};
}

static V3 rotatePoint(V3 p, float yawDeg, float pitchDeg) {
  float y = yawDeg * DEG_TO_RAD;
  float x = pitchDeg * DEG_TO_RAD;
  float cy = cosf(y), sy = sinf(y), cx = cosf(x), sx = sinf(x);
  V3 q = {cy * p.x - sy * p.y, sy * p.x + cy * p.y, p.z};
  return {q.x, cx * q.y - sx * q.z, sx * q.y + cx * q.z};
}

static uint16_t depthColor(float z) {
  int shade = constrain((int)(105.0f + 105.0f * (z + 1.0f) * 0.5f), 85, 220);
  return TFT_eSPI::color565(shade, shade + 5, shade + 12);
}

static bool highlighted(uint8_t sector, const PythonUiState &s) {
  int f = max(1, s.facet) - 1;
  return sector == (uint8_t)(f % 8);
}

static void dynamicEdge(TFT_eSPI &tft, V3 a, V3 b, uint8_t sector,
                        const PythonUiState &s, int cx, int cy, float scale) {
  a = rotatePoint(a, s.viewYaw, s.viewPitch);
  b = rotatePoint(b, s.viewYaw, s.viewPitch);
  int x0 = cx + (int)(a.x * scale), y0 = cy - (int)(a.z * scale);
  int x1 = cx + (int)(b.x * scale), y1 = cy - (int)(b.z * scale);
  uint16_t c = highlighted(sector, s) ? C_GREEN : depthColor((a.y + b.y) * 0.5f);
  tft.drawLine(x0, y0, x1, y1, c);
  if (highlighted(sector, s)) tft.drawLine(x0 + 1, y0, x1 + 1, y1, c);
}

static void forEachGemEdgeDynamic(TFT_eSPI &tft, const PythonUiState &s) {
  const int cx = 160, cy = 160;
  const float scale = 115.0f;
  for (uint8_t i = 0; i < 8; ++i) {
    dynamicEdge(tft, vertex(i), vertex((i + 1) % 8), i, s, cx, cy, scale);
    dynamicEdge(tft, vertex(i), vertex(8 + i * 2), i, s, cx, cy, scale);
    dynamicEdge(tft, vertex(i), vertex(8 + (i * 2 + 1) % 16), i, s, cx, cy, scale);
  }
  for (uint8_t i = 0; i < 16; ++i) {
    dynamicEdge(tft, vertex(8 + i), vertex(8 + (i + 1) % 16), i / 2, s, cx, cy, scale);
    dynamicEdge(tft, vertex(24 + i), vertex(24 + (i + 1) % 16), i / 2, s, cx, cy, scale);
    dynamicEdge(tft, vertex(8 + i), vertex(24 + i), i / 2, s, cx, cy, scale);
    dynamicEdge(tft, vertex(24 + i), vertex(40 + i / 2), i / 2, s, cx, cy, scale);
  }
  for (uint8_t i = 0; i < 8; ++i) {
    dynamicEdge(tft, vertex(40 + i), vertex(40 + (i + 1) % 8), i, s, cx, cy, scale);
    dynamicEdge(tft, vertex(40 + i), vertex(48), i, s, cx, cy, scale);
  }
}

enum Projection : uint8_t { PROJ_TOP, PROJ_BOTTOM, PROJ_FRONT, PROJ_SIDE };

static void project(V3 p, Projection projection, int cx, int cy, float scale, int &x, int &y) {
  if (projection == PROJ_TOP) { x = cx + p.x * scale; y = cy - p.y * scale; }
  else if (projection == PROJ_BOTTOM) { x = cx + p.x * scale; y = cy + p.y * scale; }
  else if (projection == PROJ_FRONT) { x = cx + p.x * scale; y = cy - p.z * scale; }
  else { x = cx + p.y * scale; y = cy - p.z * scale; }
}

static void staticEdge(TFT_eSPI &tft, V3 a, V3 b, uint8_t sector,
                       Projection projection, const PythonUiState &s,
                       int cx, int cy, float scale) {
  int x0, y0, x1, y1;
  project(a, projection, cx, cy, scale, x0, y0);
  project(b, projection, cx, cy, scale, x1, y1);
  uint16_t c = highlighted(sector, s) ? C_GREEN : 0x94B2;
  tft.drawLine(x0, y0, x1, y1, c);
}

static void drawStaticGem(TFT_eSPI &tft, Projection p, const PythonUiState &s,
                          int cx, int cy, float scale) {
  for (uint8_t i = 0; i < 16; ++i) {
    staticEdge(tft, vertex(8 + i), vertex(8 + (i + 1) % 16), i / 2, p, s, cx, cy, scale);
  }
  if (p != PROJ_BOTTOM) {
    for (uint8_t i = 0; i < 8; ++i) {
      staticEdge(tft, vertex(i), vertex((i + 1) % 8), i, p, s, cx, cy, scale);
      staticEdge(tft, vertex(i), vertex(8 + i * 2), i, p, s, cx, cy, scale);
      staticEdge(tft, vertex(i), vertex(8 + (i * 2 + 1) % 16), i, p, s, cx, cy, scale);
    }
  }
  if (p != PROJ_TOP) {
    for (uint8_t i = 0; i < 16; ++i)
      staticEdge(tft, vertex(24 + i), vertex(40 + i / 2), i / 2, p, s, cx, cy, scale);
    for (uint8_t i = 0; i < 8; ++i) {
      staticEdge(tft, vertex(40 + i), vertex(40 + (i + 1) % 8), i, p, s, cx, cy, scale);
      staticEdge(tft, vertex(40 + i), vertex(48), i, p, s, cx, cy, scale);
    }
  }
}

static void topBar(TFT_eSPI &tft, const PythonUiState &s) {
  tft.setTextFont(2);
  tft.setTextColor(C_WHITE, C_BG);
  tft.setTextDatum(TL_DATUM);
  String title = s.title.length() ? s.title : "POLAKIEWICZ ROUND";
  if (title.length() > 21) title = title.substring(0, 20) + "~";
  tft.drawString(title, 7, 5);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(0xC638, C_BG);
  tft.drawString("o > ^ W" + String(s.wheel), 313, 5);
}

static void sharedHud(TFT_eSPI &tft, const PythonUiState &s) {
  tft.fillRect(0, 310, 320, 170, C_BG);
  tft.drawFastHLine(22, 381, 276, 0x21EA);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_THETA, C_BG);
  tft.drawString("THETA", 23, 317);
  tft.setTextFont(4);
  tft.drawFloat(s.theta, 2, 78, 315);
  tft.setTextFont(2);
  tft.setTextColor(C_THETA_SOFT, C_BG);
  tft.drawString((s.thetaError >= 0 ? "+" : "") + String(s.thetaError, 2), 235, 322);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xD64A, C_BG);
  tft.drawString(String(s.theta - 1.0f, 2) + "  < " + String(s.tier) + "/" +
                 String(max(1, s.tierCount)) + " >  " + String(s.theta + 1.0f, 2), 160, 365);

  int barW = constrain((int)(276.0f * s.force), 0, 276);
  tft.fillRect(22, 380, 276, 3, 0x18C3);
  tft.fillRect(22, 380, barW, 3, 0xCDE5);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_PHI, C_BG);
  tft.drawString("PHI", 23, 389);
  tft.setTextFont(4);
  tft.drawFloat(s.phi, 2, 78, 387);
  tft.setTextFont(2);
  tft.setTextColor(C_PHI_SOFT, C_BG);
  tft.drawString((s.phiError >= 0 ? "+" : "") + String(s.phiError, 2), 235, 394);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String(s.phi - 1.0f, 2) + "  < " + String(s.facet) + "/" +
                 String(max(1, s.facetCount)) + " >  " + String(s.phi + 1.0f, 2), 160, 435);

  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MAGENTA, C_BG);
  tft.drawString("Z" + String(s.zStep) + " mm", 6, 449);
  tft.setTextFont(2);
  tft.drawString((s.z >= 0 ? "+" : "") + String(s.z, 3), 6, 461);

  tft.setTextFont(1);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(s.motorRunning ? C_WHITE : C_DIM, C_BG);
  tft.drawString(String("M ") + (s.motorDirection >= 0 ? "<" : ">") + " rpm", 160, 449);
  tft.setTextFont(2);
  String setRpm = s.motorRunning ? String(s.rpmSet) : "--";
  tft.drawString(setRpm + " / " + String(s.rpmActual), 160, 461);

  tft.setTextFont(1);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(s.flowRunning ? C_PHI : C_DIM, C_BG);
  tft.drawString(String("F ") + (s.flowRunning ? ">" : "-") + " mL/min", 314, 449);
  tft.setTextFont(2);
  tft.drawString(String(s.flow, 2), 314, 461);
}

static void drawDynamic(TFT_eSPI &tft, const PythonUiState &s) {
  tft.fillScreen(C_BG);
  topBar(tft, s);
  forEachGemEdgeDynamic(tft, s);
  sharedHud(tft, s);
}

static void panel(TFT_eSPI &tft, int x, int y, const char *label,
                  Projection p, const PythonUiState &s) {
  const int w = 138, h = 126;
  tft.fillRect(x, y, w, h, C_PANEL);
  tft.drawRect(x, y, w, h, C_PANEL_EDGE);
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(0x5B0C, C_PANEL);
  tft.drawString(label, x + 4, y + 3);
  drawStaticGem(tft, p, s, x + w / 2, y + h / 2 + 4, 49.0f);
  // Amber index-zero fiducial, matching the Python static view.
  tft.fillCircle(x + w / 2 + 53, y + h / 2 + 4, 2, C_AMBER);
}

static void drawStatic(TFT_eSPI &tft, const PythonUiState &s) {
  tft.fillScreen(C_BG);
  topBar(tft, s);
  panel(tft, 17, 31, "TOP", PROJ_TOP, s);
  panel(tft, 165, 31, "BOTTOM", PROJ_BOTTOM, s);
  panel(tft, 17, 173, "FRONT", PROJ_FRONT, s);
  panel(tft, 165, 173, "SIDE", PROJ_SIDE, s);
  sharedHud(tft, s);
}

}  // namespace Gen2Display
