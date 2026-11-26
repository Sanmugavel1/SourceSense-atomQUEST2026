#pragma once
// SourceSense TFT dashboard UI for the 1.8" ST7735, 160 x 128 (rotation 1).
// Layout (pixel-exact, nothing overlaps):
//   y   0-15  header: drop icon | WATER QUALITY MONITOR  (live dot)
//   y  16     blue line
//   y  18-111 six tiles, 2 columns x 3 rows, each 79 x 30, 2 px gaps
//   y 113     blue line
//   y 115-127 status bar: gear | route + reason | cycle number
// Solid RGB565 colours only (no shading), so nothing looks faded on this panel.
// The screen is only fully cleared when switching between a message and the dashboard;
// after that only changed parts are redrawn, so there is no black flash.

// Colours (RGB565)
#define C_BG     0x0000
#define C_WHITE  0xFFFF
#define C_BLUE   0x2CFE
#define C_CYAN   0x3F1F
#define C_GREEN  0x3F87
#define C_YELLOW 0xFF00
#define C_ORANGE 0xFCC0
#define C_RED    0xF986
#define C_GREY   0x8C71

struct UiData {
  float tds, tempC, ntu, clearPct;   // ntu < 0: no clear level saved, show clear %
  float flowLpm;                     // < 0: no flow this cycle
  const char* flowNote;              // "" or a short note for the flow tile
  const char* source;                // CORP / BORE / TANK / NONE
  uint16_t sourceCol;
  bool locked; float normalTds; int learnN, learnOf;
  bool bypass; const char* why;      // decide() result
  unsigned long cycle;
  uint16_t tdsCol, tempCol, turbCol;
};

const int TILE_W = 79, TILE_H = 30;
const int TILE_X[2] = {0, 81};
const int TILE_Y[3] = {18, 50, 82};
const int TXT_X = 16;                // text starts here inside a tile (icon is left of it)
const int VAL_R = 74;                // values end here inside a tile
bool uiFrame = false;                // dashboard frame is on the screen
uint16_t tileCol[6]; const char* tileLbl[6]; int tileIcon[6];   // what each tile shows now

// ---------- small icons (lines, rectangles and circles, at most 12 x 20 px) ----------
void iconDrop(int x, int y, uint16_t c) {            // header logo, 11 x 14
  tft.fillCircle(x + 5, y + 8, 4, c);
  tft.fillTriangle(x + 1, y + 7, x + 9, y + 7, x + 5, y, c);
  tft.drawFastHLine(x, y + 13, 3, C_WHITE); tft.drawFastHLine(x + 4, y + 13, 3, C_WHITE);
  tft.drawFastHLine(x + 8, y + 13, 3, C_WHITE);
}
void iconTurb(int x, int y, uint16_t c) {            // falling particles, 11 x 18
  for (int r = 0; r < 5; r++)
    for (int k = 0; k <= r; k++) tft.fillRect(x + 4 - r + 2 * k, y + 1 + r * 4, 2, 2, c);
}
void iconTds(int x, int y, uint16_t c) {             // dissolved dots, 11 x 19
  const int px[7] = {5, 1, 9, 5, 1, 9, 5}, py[7] = {1, 5, 5, 9, 13, 13, 17};
  for (int i = 0; i < 7; i++) tft.fillCircle(x + px[i], y + py[i], 1, c);
}
void iconTemp(int x, int y) {                        // thermometer, 11 x 22
  tft.drawRoundRect(x + 3, y, 5, 15, 2, C_WHITE);
  tft.fillCircle(x + 5, y + 17, 3, C_RED);
  tft.drawCircle(x + 5, y + 17, 4, C_WHITE);
  tft.drawFastVLine(x + 5, y + 7, 9, C_RED);
}
void iconFlow(int x, int y, uint16_t c) {            // three waves, 12 x 17
  for (int w = 0; w < 3; w++)
    for (int i = 0; i < 12; i++) tft.fillRect(x + i, y + 2 + w * 6 + ((i / 3) % 2), 1, 2, c);
}
void iconSource(int x, int y, uint16_t c) {          // tap with a drop, 11 x 19
  tft.fillRect(x, y + 3, 9, 3, c);
  tft.fillRect(x + 7, y + 3, 3, 8, c);
  tft.fillRect(x + 3, y, 3, 3, c);
  tft.fillCircle(x + 8, y + 16, 2, C_CYAN);
  tft.fillTriangle(x + 6, y + 15, x + 10, y + 15, x + 8, y + 12, C_CYAN);
}
void iconLock(int x, int y, bool closed, uint16_t c) { // frozen normal water, 12 x 19
  tft.fillRoundRect(x + 1, y + 9, 10, 9, 1, c);
  if (closed) { tft.drawRoundRect(x + 3, y + 2, 6, 10, 3, c); tft.drawRoundRect(x + 4, y + 3, 4, 8, 2, c); }
  else { tft.drawRoundRect(x + 5, y, 6, 10, 3, c); tft.drawRoundRect(x + 6, y + 1, 4, 8, 2, c); }
  tft.fillRect(x + 5, y + 12, 2, 3, C_BG);
}
void iconGear(int x, int y, uint16_t c) {            // 13 x 13
  tft.fillCircle(x + 6, y + 6, 4, c);
  tft.fillRect(x + 5, y, 3, 13, c); tft.fillRect(x, y + 5, 13, 3, c);
  tft.fillRect(x + 1, y + 1, 3, 3, c); tft.fillRect(x + 9, y + 1, 3, 3, c);
  tft.fillRect(x + 1, y + 9, 3, 3, c); tft.fillRect(x + 9, y + 9, 3, 3, c);
  tft.fillCircle(x + 6, y + 6, 2, C_BG);
}

// ---------- text helpers ----------
int textW(const char* s, int size) { return strlen(s) * 6 * size; }
void textAt(int x, int y, const char* s, int size, uint16_t c) {
  tft.setTextWrap(false);
  tft.setTextSize(size);
  tft.setTextColor(c);
  tft.setCursor(x, y);
  tft.print(s);
}
// Width of a value: a '.' is drawn as a small dot (2 px cell per size) instead of a full
// 6 px cell, so "27.8" fits beside its unit like on the reference design.
int valW(const char* v, int size) {
  int w = 0;
  for (const char* p = v; *p; p++) w += (*p == '.') ? 2 * size : 6 * size;
  return w - size;                                // ink width (last column is blank)
}
void valDraw(int x, int y, const char* v, int size, uint16_t c) {
  tft.setTextWrap(false); tft.setTextSize(size); tft.setTextColor(c);
  for (const char* p = v; *p; p++) {
    if (*p == '.') { tft.fillRect(x, y + 5 * size, size, 2 * size - (size > 1), c); x += 2 * size; }
    else { tft.setCursor(x, y); tft.print(*p); x += 6 * size; }
  }
}

// Big value with a small unit, right-aligned to VAL_R. Falls back to small text if it
// would not fit, and drops the unit before anything can touch the icon or the border.
void tileValue(int t, const char* val, const char* unit, uint16_t c, bool degree = false) {
  int x = TILE_X[t % 2], y = TILE_Y[t / 2];
  tft.fillRect(x + TXT_X, y + 13, VAL_R - TXT_X + 1, 14, C_BG);   // clear old value only
  int uw = unit[0] ? textW(unit, 1) - 1 + (degree ? 4 : 0) : 0;
  int room = VAL_R - TXT_X;
  int size = (valW(val, 2) + (uw ? uw + 2 : 0) <= room) ? 2 : 1;
  int vw = valW(val, size);
  if (vw + (uw ? uw + 2 : 0) > room) uw = 0;
  int ux = x + VAL_R + 1 - uw;
  int vx = ux - (uw ? 2 : 0) - vw;
  valDraw(vx, size == 2 ? y + 13 : y + 17, val, size, c);
  if (uw) {
    if (degree) { tft.drawCircle(ux + 1, y + 19, 1, C_WHITE); ux += 4; }
    textAt(ux, y + 19, unit, 1, C_WHITE);
  }
}

void tileIconDraw(int t, int icon, uint16_t c) {
  int x = TILE_X[t % 2] + 3, y = TILE_Y[t / 2] + 5;
  switch (icon) {
    case 0: iconTurb(x, y, C_CYAN); break;
    case 1: iconTds(x, y, C_CYAN); break;
    case 2: iconTemp(x, y - 1); break;
    case 3: iconFlow(x, y + 1, C_CYAN); break;
    case 4: iconSource(x, y, C_YELLOW); break;
    case 5: iconLock(x, y, true, c); break;
    case 6: iconLock(x, y, false, c); break;
  }
}
// Box with a clean 2 px rounded border, icon and label. Redrawn only when its look
// changes, so most updates touch just the value.
void tileBox(int t, uint16_t c, const char* label, int icon) {
  if (tileCol[t] == c && tileLbl[t] == label && tileIcon[t] == icon) return;
  int x = TILE_X[t % 2], y = TILE_Y[t / 2];
  tft.fillRoundRect(x, y, TILE_W, TILE_H, 5, c);
  tft.fillRoundRect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, 3, C_BG);
  tileIconDraw(t, icon, c);
  textAt(x + TXT_X, y + 4, label, 1, C_WHITE);
  tileCol[t] = c; tileLbl[t] = label; tileIcon[t] = icon;
}

void drawHeader() {
  iconDrop(1, 1, C_CYAN);
  tft.drawFastVLine(14, 2, 12, C_BLUE);
  textAt(18, 5, "WATER QUALITY MONITOR", 1, C_WHITE);   // ends at x 143
  tft.fillCircle(153, 8, 3, C_GREEN);                    // live
  tft.drawFastHLine(14, 16, 146, C_BLUE);
}
void drawFooterFrame() {
  tft.drawFastHLine(0, 113, 160, C_BLUE);
  iconGear(1, 115, C_WHITE);
  tft.drawFastVLine(16, 115, 12, C_BLUE);
  tft.drawFastVLine(121, 115, 12, C_BLUE);
}

// Status bar: "BYPASS: WATER OK" or "RO: TDS ROSE", cycle number on the right.
const char* shortWhy(const char* why) {
  static const char* map[][2] = {
    {"SAFE", "WATER OK"}, {"FAULT:TEMP", "TEMP FAULT"}, {"FAULT:TDS DRY", "JAR EMPTY"},
    {"FAULT:TDS RNG", "TDS FAULT"}, {"FAULT:TURB", "TURB FAULT"}, {"NO SOURCE TAG", "NO SOURCE"},
    {"TANKER WATER", "TANKER"}, {"CLOUDY WATER", "CLOUDY"}, {"SOURCE CHANGE", "NEW SOURCE"}};
  for (auto& m : map) if (strcmp(why, m[0]) == 0) return m[1];
  return why;                                              // HIGH TDS, TDS ROSE ... already short
}
void drawStatus(const char* route, uint16_t routeCol, const char* text, uint16_t textCol, unsigned long cycle) {
  tft.fillRect(18, 115, 102, 13, C_BG);
  tft.fillRect(123, 115, 37, 13, C_BG);
  int x = 20;
  if (route[0]) {
    textAt(x, 118, route, 1, routeCol);
    x += textW(route, 1);
    textAt(x - 1, 118, ":", 1, C_WHITE);
    x += 6;
  }
  char buf[17];
  int maxChars = (120 - x) / 6;                            // never past the divider
  if (maxChars > 16) maxChars = 16;
  strncpy(buf, text, maxChars); buf[maxChars] = 0;
  textAt(x, 118, buf, 1, textCol);
  if (cycle) {
    char n[8];
    snprintf(n, sizeof(n), "#%lu", cycle % 10000);
    textAt(159 - textW(n, 1) + 1, 118, n, 1, C_GREY);
  }
}

void drawDashboardFrame() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawFooterFrame();
  for (int t = 0; t < 6; t++) { tileCol[t] = 1; tileLbl[t] = nullptr; tileIcon[t] = -1; }
  uiFrame = true;
}

// Flow tile only (also called while the pump runs)
void uiFlow(float lpm, const char* note, uint16_t c) {
  char v[12];
  if (note && note[0]) tileValue(3, note, "", c);
  else if (lpm < 0) tileValue(3, "--", "L/m", C_GREY);
  else { snprintf(v, sizeof(v), lpm < 10 ? "%.2f" : "%.1f", lpm); tileValue(3, v, "L/m", c); }
}

void uiShow(const UiData& d) {
  if (!uiFrame) drawDashboardFrame();
  char v[12];

  // 0 turbidity
  if (d.ntu >= 0) {
    snprintf(v, sizeof(v), d.ntu < 10 ? "%.1f" : "%.0f", d.ntu);
    tileBox(0, d.turbCol, "TURBIDITY", 0);
    tileValue(0, v, "NTU", d.turbCol);
  } else {                                         // no clear level saved yet
    snprintf(v, sizeof(v), "%.0f", d.clearPct);
    tileBox(0, d.turbCol, "CLEARNESS", 0);
    tileValue(0, v, "%", d.turbCol);
  }
  // 1 TDS
  tileBox(1, d.tdsCol, "TDS", 1);
  snprintf(v, sizeof(v), "%.0f", d.tds);
  tileValue(1, v, "ppm", d.tdsCol);
  // 2 temperature
  tileBox(2, d.tempCol, "TEMP", 2);
  if (d.tempC < -50) tileValue(2, "ERR", "", C_RED);
  else { snprintf(v, sizeof(v), "%.1f", d.tempC); tileValue(2, v, "C", d.tempCol, true); }
  // 3 flow
  uint16_t fc = (d.flowNote && d.flowNote[0]) ? (strcmp(d.flowNote, "READ ONLY") == 0 ? C_GREY : C_RED) : C_GREEN;
  tileBox(3, fc == C_GREY ? C_GREEN : fc, "FLOW RATE", 3);
  uiFlow(d.flowLpm, d.flowNote, fc);
  // 4 source
  tileBox(4, d.sourceCol, "SOURCE", 4);
  tileValue(4, d.source, "", d.sourceCol);
  // 5 normal water (frozen baseline)
  if (d.locked) {
    tileBox(5, C_CYAN, "NORMAL TDS", 5);
    snprintf(v, sizeof(v), "%.0f", d.normalTds);
    tileValue(5, v, "ppm", C_CYAN);
  } else {
    tileBox(5, C_YELLOW, "LEARNING", 6);
    snprintf(v, sizeof(v), "%d/%d", d.learnN, d.learnOf);
    tileValue(5, v, "", C_YELLOW);
  }
  // status bar
  drawStatus(d.bypass ? "BYPASS" : "RO", d.bypass ? C_GREEN : C_ORANGE,
             shortWhy(d.why), d.bypass ? C_GREEN : (strncmp(d.why, "FAULT", 5) == 0 ? C_RED : C_YELLOW),
             d.cycle);
}

// Full-screen message in the same style: header, one card, status bar.
void uiMessage(const char* a, const char* b, const char* c = "") {
  tft.fillScreen(C_BG);
  uiFrame = false;
  drawHeader();
  drawFooterFrame();
  tft.fillRoundRect(0, 18, 160, 94, 5, C_BLUE);
  tft.fillRoundRect(2, 20, 156, 90, 3, C_BG);
  int sa = textW(a, 2) <= 150 ? 2 : 1;
  textAt(80 - (textW(a, sa) - sa) / 2, c[0] ? 36 : 42, a, sa, C_WHITE);
  if (b[0]) {
    int sb = textW(b, 2) <= 150 ? 2 : 1;
    textAt(80 - (textW(b, sb) - sb) / 2, c[0] ? 60 : 70, b, sb, C_CYAN);
  }
  if (c[0]) textAt(80 - (textW(c, 1) - 1) / 2, 88, c, 1, C_YELLOW);
  drawStatus("", C_WHITE, "SOURCESENSE", C_WHITE, 0);
}
