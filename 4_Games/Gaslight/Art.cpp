// Gaslight & Griffins - people and creatures: one description per person
// drives both their 3D figure and their portrait; creatures are built from
// a body plan (the shape recipe the world model will one day write).
#pragma GCC optimize("O2")
#include "Game.h"
#include <stdio.h>
#include "Raster.h"
#include "GLWorld.h"

namespace Gaslight {

//                coat      legs      skin      hair      accent    hat          hairStyle   brd ep gl dr  height build age mood
const Look LOOKS[L_COUNT] = {
    /* WREN     */ {0x7A6A4A, 0x3A3530, 0xF0C8A8, 0xA0522D, 0xD8A33A, HAT_NONE, HAIR_MESSY, 0, 0, 0, 0, 0.92f, 0.95f, 1, 0},
    /* VANE     */ {0x2E4A3A, 0x23262A, 0xE8C8B0, 0xC8C8C8, 0x7A2430, HAT_NONE, HAIR_SWEPT, 0, 0, 0, 0, 1.12f, 0.85f, 3, 2},
    /* VANEMASK */ {0x1A1E2A, 0x15171C, 0xE8E0D0, 0xC8C8C8, 0x2A4AA0, HAT_NONE, HAIR_SWEPT, 0, 0, 0, 0, 1.12f, 0.85f, 3, 2},
    /* JUNO     */ {0x2E4A7A, 0x1E2230, 0x7A4A32, 0x141414, 0xB9853A, HAT_NONE, HAIR_BOB, 0, 0, 0, 0, 0.95f, 0.9f, 1, 4},
    /* MARISOL  */ {0x8A2A30, 0x6A2026, 0xE0B090, 0x2A1A14, 0x4F8A78, HAT_NONE, HAIR_CURLS, 0, 0, 0, 1, 0.98f, 0.95f, 2, 3},
    /* SILAS    */ {0x5A5A3A, 0x3A3A30, 0xD8B098, 0x9A9A98, 0xE8A33D, HAT_CAP, HAIR_SHORT, 2, 1, 0, 0, 1.0f, 1.15f, 3, 3},
    /* OTTILIE  */ {0x23324A, 0x23324A, 0xEAC8B0, 0x8A8A8A, 0xB9853A, HAT_BUN, HAIR_BALD, 0, 0, 1, 1, 1.0f, 1.0f, 3, 2},
    /* CREWE    */ {0x3A3A42, 0x2A2A30, 0xE8C0A0, 0x3A2A1A, 0xD8B040, HAT_TOP, HAIR_SHORT, 1, 0, 0, 0, 1.05f, 1.1f, 2, 4},
    /* PIP      */ {0x5A4A3A, 0x3A3028, 0xE8B898, 0x6A4A2A, 0x8A3A2A, HAT_CAP, HAIR_MESSY, 0, 0, 0, 0, 0.72f, 0.85f, 0, 1},
    /* YSOLDE   */ {0x6A5A6A, 0x4A3E4A, 0xD8B8A0, 0xEDEDE8, 0x9A8A7A, HAT_NONE, HAIR_BRAID, 0, 0, 0, 1, 0.9f, 0.95f, 3, 0},
    /* HALLORAN */ {0x1E2A4A, 0x1A1E2A, 0xD8A888, 0x9A9A9A, 0x4F8AA8, HAT_CAPTAIN, HAIR_SHORT, 0, 0, 0, 0, 1.05f, 1.25f, 2, 1},
    /* QUILL    */ {0x7A5A6A, 0x5A4A5A, 0xF0C8B0, 0x9A8A7A, 0xEFE6D4, HAT_BONNET, HAIR_BUN, 0, 0, 1, 1, 0.9f, 1.3f, 3, 1},
    /* DOBBS    */ {0x23324A, 0x1A2234, 0xF0D0B8, 0xC89060, 0xB9853A, HAT_HELMET, HAIR_SHORT, 1, 0, 0, 0, 1.1f, 0.85f, 1, 5},
    /* LUNE     */ {0x4A3A5A, 0x2A2430, 0xC89878, 0x3A2A2A, 0xB9853A, HAT_NONE, HAIR_BUN, 0, 0, 1, 1, 0.98f, 0.95f, 2, 0},
    /* SAILOR   */ {0x2A3A5A, 0x3A3A3A, 0xD8A888, 0x4A3A2A, 0xEFEFEF, HAT_CAP, HAIR_SHORT, 2, 0, 0, 0, 1.0f, 1.15f, 2, 0},
    /* SAILOR2  */ {0x4A5A4A, 0x2A2A2A, 0xB88868, 0x1A1A1A, 0xA0A0A0, HAT_NONE, HAIR_SHORT, 1, 0, 0, 0, 1.02f, 1.05f, 2, 3},
    /* FISHWIFE */ {0x6A4A3A, 0x4A3A3A, 0xE8B898, 0xB86A3A, 0xEFE6D4, HAT_BONNET, HAIR_CURLS, 0, 0, 0, 1, 0.95f, 1.2f, 2, 1},
    /* URCHIN   */ {0x4A5A6A, 0x3A3A3A, 0xC89878, 0x2A1A14, 0xD8A33A, HAT_NONE, HAIR_MESSY, 0, 0, 0, 0, 0.7f, 0.85f, 0, 1},
    /* GENT     */ {0x4A3A2A, 0x2A2A2A, 0xE8C8B0, 0x8A8A8A, 0x7A2430, HAT_TOP, HAIR_SHORT, 2, 0, 1, 0, 1.0f, 1.2f, 3, 0},
    /* LADY     */ {0x5A6A8A, 0x4A5A7A, 0xF0D0C0, 0xC8A060, 0xEFE6D4, HAT_BONNET, HAIR_LONG, 0, 0, 0, 1, 0.96f, 0.9f, 2, 1},
    /* CURATOR  */ {0x1A1A1E, 0x141418, 0xE8E0D0, 0x1A1A1A, 0x2A4AA0, HAT_HOOD, HAIR_SHORT, 0, 0, 0, 0, 1.05f, 1.0f, 2, 2},
    /* BRAM     */ {0x1A2238, 0x141A2A, 0xE0B898, 0x6A6A6A, 0xD8B040, HAT_NONE, HAIR_SHORT, 2, 0, 0, 0, 1.08f, 1.2f, 3, 2},
    /* TRAINER_A*/ {0x6A3A2A, 0x3A3A30, 0xE0B898, 0x3A2A1A, 0xE8A33D, HAT_CAP, HAIR_SHORT, 0, 0, 0, 0, 0.95f, 1.0f, 1, 1},
    /* TRAINER_B*/ {0x3A6A5A, 0x2A3A3A, 0x9A6A4A, 0x141414, 0xEFE6D4, HAT_NONE, HAIR_LONG, 0, 0, 0, 1, 0.92f, 0.9f, 1, 4},
    /* TRAINER_C*/ {0x5A5A6A, 0x3A3A44, 0xE8C8B0, 0xD8C080, 0x7A2430, HAT_TOP, HAIR_SHORT, 0, 0, 1, 0, 0.98f, 0.95f, 1, 2},
    /* TALLY    */ {0xC8A060, 0x8A6A40, 0x9A6A44, 0x1A1410, 0xD84A3A, HAT_NONE, HAIR_BRAID, 0, 0, 0, 0, 0.72f, 0.85f, 0, 1},
    /* CONDUCTOR*/ {0x23324A, 0x1A1E2A, 0xE0B898, 0x5A5A5A, 0xB9853A, HAT_CAP, HAIR_SHORT, 1, 0, 0, 0, 1.00f, 1.05f, 2, 0},
    /* NEFERET  */ {0xE8E0D0, 0xC8C0B0, 0xB88860, 0x141414, 0xD8B040, HAT_NONE, HAIR_LONG, 0, 0, 0, 1, 1.00f, 0.90f, 2, 4},
    /* TOMAS    */ {0xB89A6A, 0x6A5A40, 0xC89870, 0x3A2A1A, 0x7A2430, HAT_CAP, HAIR_SHORT, 1, 0, 0, 0, 1.00f, 1.00f, 1, 1},
    /* MERCHANT */ {0x8A3A6A, 0x4A3A3A, 0x9A6A44, 0x1A1A1A, 0xD8B040, HAT_NONE, HAIR_SHORT, 2, 0, 0, 0, 1.00f, 1.25f, 2, 1},
    /* ASSISTANT*/ {0x3A4A3A, 0x2A2A2A, 0xC89870, 0x2A2A2A, 0xB9853A, HAT_CAP, HAIR_SHORT, 1, 0, 0, 0, 1.02f, 1.00f, 1, 5},
    /* KAMAL    */ {0xD8C8A0, 0x8A7A5A, 0xA87850, 0x2A2A2A, 0x2A4A7A, HAT_NONE, HAIR_SHORT, 1, 0, 1, 0, 1.00f, 1.05f, 2, 5},
    /* SEFA     */ {0x5A4A6A, 0x3A3044, 0x7A4A32, 0x141414, 0xD8B040, HAT_BUN, HAIR_BALD, 0, 0, 0, 1, 0.95f, 0.90f, 1, 3},
    /* IMANI    */ {0xC86A3A, 0x5A3A28, 0x5A3A28, 0x141414, 0x6AC8E8, HAT_NONE, HAIR_CURLS, 0, 0, 0, 0, 1.00f, 1.05f, 2, 1},
    /* INNKEEP_S*/ {0xA86A3A, 0x6A4A2A, 0xB88860, 0x2A1A14, 0xEFE6D4, HAT_NONE, HAIR_SHORT, 2, 0, 0, 0, 1.00f, 1.30f, 2, 1},
    /* INNKEEP_G*/ {0x6A5A3A, 0x4A3A2A, 0xF0C8A8, 0xA86A3A, 0xEFE6D4, HAT_BONNET, HAIR_CURLS, 0, 0, 0, 1, 0.95f, 1.25f, 2, 1},
    /* INNKEEP_K*/ {0x6A5A4A, 0x4A4A44, 0xE8C8B0, 0xE8D8A0, 0x7A2430, HAT_NONE, HAIR_BRAID, 0, 0, 0, 1, 1.00f, 1.20f, 2, 1},
    /* INNKEEP_U*/ {0x7A4A6A, 0x4A3A4A, 0x9A6A44, 0x141414, 0xD8B040, HAT_NONE, HAIR_SHORT, 1, 0, 0, 0, 1.00f, 1.20f, 2, 1},
    /* MOTH     */ {0x4A6A3A, 0x3A3A2A, 0xE8B898, 0x6A4A2A, 0x8A6A3A, HAT_HOOD, HAIR_MESSY, 0, 0, 0, 0, 0.70f, 0.85f, 0, 1},
    /* BERTRAM  */ {0x2E5A2E, 0x3A2A1A, 0xF0C0A0, 0xB86A3A, 0xD8B040, HAT_NONE, HAIR_SHORT, 2, 0, 0, 0, 1.08f, 1.40f, 2, 1},
    /* GREENKNIG*/ {0x2E6A2E, 0x2A4A2A, 0x6A9A5A, 0x3A6A2A, 0x8AC86A, HAT_NONE, HAIR_LONG, 2, 0, 0, 0, 1.70f, 1.50f, 3, 2},
    /* GARETH   */ {0x8A8A9A, 0x5A5A6A, 0xF0D0B8, 0xD8B060, 0x7A2430, HAT_NONE, HAIR_SHORT, 0, 0, 0, 0, 1.02f, 1.00f, 1, 5},
    /* ROSALIND */ {0x7A2A4A, 0x5A2038, 0xF0D0C0, 0x6A3A1A, 0x3AA83A, HAT_NONE, HAIR_LONG, 0, 0, 0, 1, 0.98f, 0.90f, 2, 4},
    /* MORGANA  */ {0x3A4A3A, 0x2A3A2A, 0xD8B8A0, 0xC8C8C8, 0x8A6AC8, HAT_NONE, HAIR_LONG, 0, 0, 0, 1, 0.92f, 0.95f, 3, 4},
    /* IVY      */ {0x4A5A2A, 0x3A3A2A, 0xE0B898, 0x8A4A2A, 0xB9853A, HAT_NONE, HAIR_BOB, 0, 0, 0, 0, 1.00f, 1.00f, 1, 2},
    /* SIGGA    */ {0x8A9AB8, 0x5A5A6A, 0xF0D0C0, 0xE8D8A0, 0xC83A3A, HAT_CAP, HAIR_BRAID, 0, 0, 0, 0, 0.72f, 0.90f, 0, 1},
    /* ASTA     */ {0x6A5A4A, 0x4A4A3A, 0xE8C8B0, 0xD8C080, 0x4A6A8A, HAT_NONE, HAIR_BRAID, 0, 0, 0, 0, 0.98f, 1.00f, 2, 0},
    /* VILLAGER */ {0x5A6A7A, 0x3A3A44, 0xE8C8B0, 0x8A6A4A, 0xC8B8A0, HAT_NONE, HAIR_SHORT, 2, 0, 0, 0, 1.00f, 1.15f, 2, 5},
    /* EIRIK    */ {0x7A3A3A, 0x4A2A2A, 0xF0C8A8, 0xE8C060, 0xD8B040, HAT_NONE, HAIR_LONG, 2, 0, 0, 0, 1.05f, 1.10f, 2, 4},
    /* HILDE    */ {0x3A3A4A, 0x2A2A34, 0xF0D0C0, 0xE8E0D0, 0x6A8AC8, HAT_NONE, HAIR_BRAID, 0, 0, 0, 0, 1.05f, 1.10f, 2, 2},
    /* RAVEN    */ {0x141418, 0x141418, 0x141418, 0x141418, 0x6A6A88, HAT_NONE, HAIR_BALD, 0, 0, 0, 0, 0.50f, 0.60f, 1, 0},
    /* SIDURI   */ {0x8A5A2A, 0x6A4A2A, 0xB88860, 0x2A1A14, 0xE8C060, HAT_NONE, HAIR_CURLS, 0, 0, 0, 1, 0.98f, 1.05f, 2, 1},
};
const char *const LOOK_NAMES[L_COUNT] = {
    "Wren", "Prof. Vane", "The Collector", "Juno", "Marisol", "Silas", "Ottilie", "Crewe", "Pip", "Ysolde",
    "Capt. Halloran", "Mrs. Quill", "Constable Dobbs", "Dr. Lune", "Sailor", "Old Salt", "Fishwife", "Urchin",
    "Gentleman", "Lady", "Curator", "Insp. Castellane", "Climber Tam", "Birder Noor", "Scholar Fenn",
    "Tally", "Conductor", "Lady Neferet", "Tomas Reyes", "Merchant", "Museum guard", "Kamal Idris", "Sefa Okon", "Imani Sato", "Innkeeper", "Innkeeper", "Innkeeper", "Innkeeper", "Moth", "Lord Bertram", "The Green Knight", "Gareth Lyle", "Lady Rosalind", "Morgana Fay", "Ser Ivy", "Sigga", "Asta Brun", "Villager", "Eirik Hallsson", "Hilde Ravnsdottir", "Muninn", "Siduri",
};

static uint32_t shadeHex(uint32_t c, float k) {
  int r = clampi((int)(((c >> 16) & 255) * k), 0, 255), g = clampi((int)(((c >> 8) & 255) * k), 0, 255),
      b = clampi((int)((c & 255) * k), 0, 255);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// --- 3D people -------------------------------------------------------------------------------

// --- Wren, drawn cute: big head, small round body, big eyes -------------------------------------
// 0 = the plain figure every other person uses; 1 little detective; 2 plush; 3 fox hood; 4 blocky toy; 5-6 chibi toy; 7 plush, cuter (in use).
int g_wrenStyle = 7;
// Style 4: a blocky toy figure - cylinder head with a painted face, a cap, a tapered
// torso, block legs that swing from the hip, and hook hands.
static void buildWrenToy(MeshBuf &m, float x, float z, float face, float walkT, bool moving) {
  float cs = cosf(face), sn = sinf(face);
  auto px = [&](float lx, float lz) { return x + lx * cs - lz * sn; };
  auto pz = [&](float lx, float lz) { return z + lx * sn + lz * cs; };
  float sw = moving ? sinf(walkT * 3.0f) * 0.6f : 0;
  float b = moving ? fabsf(sinf(walkT * 3.0f)) * 0.03f : 0;
  const uint32_t SKIN = 0xF2C9A0, CAP = 0xD0302A, COAT = 0x2E5E96, LEGS = 0x26272E, SHOE = 0x9A9AA0, SCARF = 0xE8B03A,
                 INK = 0x1A1414, HAIR = 0xB0562A;
  // a tapered box: half-widths at the bottom (wb) and top (wt), half-depth d
  auto taper = [&](float y0, float y1, float wb, float wt, float d, uint32_t col) {
    float P[8][3];
    for (int i = 0; i < 8; i++) {
      bool top = i >= 4;
      float hw = top ? wt : wb;
      float lx = (i & 1) ? hw : -hw, lz = (i & 2) ? d : -d;
      P[i][0] = px(lx, lz); P[i][1] = top ? y1 : y0; P[i][2] = pz(lx, lz);
    }
    auto q = [&](int a, int bb, int c, int dd) {
      float p[12] = {P[a][0], P[a][1], P[a][2], P[bb][0], P[bb][1], P[bb][2], P[c][0], P[c][1], P[c][2], P[dd][0], P[dd][1], P[dd][2]};
      addQuad(m, p, col);
    };
    q(2, 3, 7, 6); q(1, 0, 4, 5); q(3, 1, 5, 7); q(0, 2, 6, 4); q(6, 7, 5, 4);
  };
  addDisc(m, x, 0.03f, z, 0.3f, 0x1E2420, 6);
  // legs: blocks swinging from the hip, with grey-soled shoes
  float hip = 0.58f + b;
  for (int s = -1; s <= 1; s += 2) {
    float lx = s * 0.105f;
    addLimb(m, px(lx, 0), hip, pz(lx, 0), 0.1f, 0.27f, 0.11f, face, LEGS, s * sw);
    float fz = 0.03f + sinf(s * sw) * 0.5f;
    addBlob(m, px(lx, fz), 0.05f + b, pz(lx, fz), 0.1f, 0.05f, 0.14f, SHOE, 6, 3);
  }
  taper(hip - 0.04f, hip + 0.06f, 0.215f, 0.215f, 0.11f, LEGS);   // hips
  // the torso: wider at the bottom, a jacket with lapels, buttons and a badge
  float t0 = hip + 0.06f, t1 = t0 + 0.42f;
  taper(t0, t1, 0.235f, 0.135f, 0.11f, COAT);
  for (int s = -1; s <= 1; s += 2)   // lapels in a V
    addPanel(m, px(s * 0.045f, 0.112f), t1 - 0.1f, pz(s * 0.045f, 0.112f), 0.035f, 0.1f, face, 0x24497A);
  addPanel(m, px(0, 0.113f), t0 + 0.2f, pz(0, 0.113f), 0.016f, 0.016f, face, SCARF);
  addPanel(m, px(0, 0.113f), t0 + 0.11f, pz(0, 0.113f), 0.016f, 0.016f, face, SCARF);
  addPanel(m, px(-0.11f, 0.113f), t1 - 0.12f, pz(-0.11f, 0.113f), 0.03f, 0.03f, face, 0xD8B040); // a brass badge
  // arms angled out a little, swinging; hook hands
  for (int s = -1; s <= 1; s += 2) {
    float ax = s * 0.235f;
    addLimb(m, px(ax, 0.02f), t1 - 0.02f, pz(ax, 0.02f), 0.06f, 0.16f, 0.065f, face, COAT, -s * sw * 0.7f - 0.25f);
    float hz = -sinf(-s * sw * 0.7f - 0.25f) * 0.32f + 0.02f;
    addCyl(m, px(ax + s * 0.01f, hz + 0.02f), t1 - 0.4f, pz(ax + s * 0.01f, hz + 0.02f), 0.055f, t1 - 0.32f, 0.055f, SKIN, 7);
    addPanel(m, px(ax + s * 0.01f, hz + 0.08f), t1 - 0.36f, pz(ax + s * 0.01f, hz + 0.08f), 0.024f, 0.03f, face, 0xA0704C); // the hook's gap
  }
  // scarf round the neck, then the neck
  addCyl(m, x, t1 - 0.02f, z, 0.13f, t1 + 0.04f, 0.11f, SCARF, 8);
  addLimb(m, px(0.07f, 0.1f), t1 + 0.01f, pz(0.07f, 0.1f), 0.03f, 0.08f, 0.015f, face, SCARF, 0.2f - sw * 0.3f);
  // the head: a short cylinder with a painted face
  float h0 = t1 + 0.04f, h1 = h0 + 0.3f, hr = 0.165f;
  addCyl(m, x, h0, z, hr, h1, hr, SKIN, 10);
  float fy = (h0 + h1) * 0.5f, fz = hr + 0.004f;
  for (int s = -1; s <= 1; s += 2) {
    addPanel(m, px(s * 0.058f, fz), fy + 0.015f, pz(s * 0.058f, fz), 0.026f, 0.034f, face, INK);     // eyes
    addPanel(m, px(s * 0.064f, fz + 0.001f), fy + 0.028f, pz(s * 0.064f, fz + 0.001f), 0.01f, 0.01f, face, 0xFFFFFF, 0.5f);
    addPanel(m, px(s * 0.06f, fz), fy + 0.066f, pz(s * 0.06f, fz), 0.024f, 0.005f, face, 0x5A2E1C); // brows
    addPanel(m, px(s * 0.095f, fz - 0.03f), fy - 0.015f, pz(s * 0.095f, fz - 0.03f), 0.022f, 0.012f, face, 0xF0A090); // cheeks
  }
  addPanel(m, px(0, fz), fy - 0.05f, pz(0, fz), 0.05f, 0.022f, face, 0x5A1E1E);    // a big open smile
  addPanel(m, px(0, fz + 0.001f), fy - 0.032f, pz(0, fz + 0.001f), 0.046f, 0.006f, face, 0xFFFFFF);
  addPanel(m, px(0, fz + 0.001f), fy - 0.062f, pz(0, fz + 0.001f), 0.026f, 0.009f, face, 0xD85A5A);
  // auburn hair poking out under the cap
  addBlob(m, px(0, -0.07f), h1 - 0.03f, pz(0, -0.07f), 0.17f, 0.08f, 0.12f, HAIR, 7, 3);
  for (int s = -1; s <= 1; s += 2) addBlob(m, px(s * 0.14f, -0.02f), h1 - 0.07f, pz(s * 0.14f, -0.02f), 0.03f, 0.06f, 0.06f, HAIR, 5, 3);
  // the cap: a dome, a button, and a brim out the front
  addBlob(m, px(0, -0.02f), h1 + 0.01f, pz(0, -0.02f), 0.185f, 0.12f, 0.185f, CAP, 8, 4);
  addBlob(m, px(0, -0.02f), h1 + 0.125f, pz(0, -0.02f), 0.025f, 0.02f, 0.025f, 0xA02420, 5, 3);
  addLimb(m, px(0, 0.24f), h1 + 0.02f, pz(0, 0.24f), 0.14f, 0.012f, 0.085f, face, 0xB02820, -0.12f);
}

// Styles 5 and 6: a chibi toy - a big round head nearly the size of the body, a wide grin,
// bright plastic colours, a tiny body with stubby block legs. 5 = red and black, 6 = detective blue.
static void buildWrenChibi(MeshBuf &m, int style, float x, float z, float face, float walkT, bool moving) {
  float cs = cosf(face), sn = sinf(face);
  auto px = [&](float lx, float lz) { return x + lx * cs - lz * sn; };
  auto pz = [&](float lx, float lz) { return z + lx * sn + lz * cs; };
  float sw = moving ? sinf(walkT * 3.2f) * 0.55f : 0;
  float b = moving ? fabsf(sinf(walkT * 3.2f)) * 0.06f : 0;
  const uint32_t SKIN = 0xFFD9B8, CAP = 0xF0302A, INK = 0x201418, HAIR = 0xC0602A;
  uint32_t TOP = style == 5 ? 0xF0302A : 0x2E6EC0, LEGS = style == 5 ? 0x2A2A32 : 0x3A3040, TRIM = style == 5 ? 0x3AB8F0 : 0xF8C040;
  const bool plush = style == 7; // the soft one: mustard jumper, red scarf and mitts, a beret
  const uint32_t SCARF = 0xE8383E, BERET = 0xB0304C;
  if (plush) { TOP = 0xF6B83A; LEGS = 0x6A4250; }
  addDisc(m, x, 0.03f, z, 0.32f, 0x1E2420, 7);
  // stubby block legs and big round shoes
  for (int s = -1; s <= 1; s += 2) {
    float lx = s * 0.085f;
    addLimb(m, px(lx, 0), 0.27f + b, pz(lx, 0), 0.075f, 0.1f, 0.08f, face, LEGS, s * sw);
    float fz = 0.04f + sinf(s * sw) * 0.18f;
    addBlob(m, px(lx, fz), 0.055f + b * 0.5f, pz(lx, fz), 0.085f, 0.06f, 0.11f, plush ? 0x7A4A2E : 0xF4F4F4, 5, 3);
  }
  // a small round body, a coloured collar band and a chest print
  float by = 0.38f + b;
  addBlob(m, x, by, z, plush ? 0.2f : 0.19f, plush ? 0.18f : 0.17f, plush ? 0.17f : 0.16f, TOP, 6, 3);
  if (plush) {
    // a cosy scarf, its long tail flapping as she walks, and two jumper buttons
    addCyl(m, x, by + 0.1f, z, 0.17f, by + 0.17f, 0.15f, SCARF, 6);
    addLimb(m, px(0.07f, 0.13f), by + 0.13f, pz(0.07f, 0.13f), 0.045f, 0.11f, 0.02f, face, SCARF, 0.25f - sw * 0.35f);
    addPanel(m, px(0, 0.17f), by - 0.02f, pz(0, 0.17f), 0.018f, 0.018f, face, 0xFFF4E0);
    addPanel(m, px(0, 0.172f), by - 0.08f, pz(0, 0.172f), 0.018f, 0.018f, face, 0xFFF4E0);
  } else {
    addPanel(m, px(0, 0.16f), by + 0.01f, pz(0, 0.16f), 0.06f, 0.05f, face, TRIM);        // a patch on the chest
    addPanel(m, px(0, 0.163f), by + 0.01f, pz(0, 0.163f), 0.025f, 0.025f, face, 0xFFFFFF);
  }
  // arms: little cylinders with round mitts that swing
  for (int s = -1; s <= 1; s += 2) {
    float ax = s * 0.2f;
    addLimb(m, px(ax, 0), by + 0.1f, pz(ax, 0), 0.045f, 0.09f, 0.045f, face, TOP, -s * sw * 0.8f - 0.15f);
    float hz = -sinf(-s * sw * 0.8f - 0.15f) * 0.18f;
    addBlob(m, px(ax + s * 0.015f, hz), by - 0.1f, pz(ax + s * 0.015f, hz), 0.06f, 0.06f, 0.06f, plush ? SCARF : SKIN, 5, 3);
  }
  // the big round head
  float hr = 0.28f, hy = by + 0.14f + hr;
  addBlob(m, x, hy, z, hr, hr * 0.92f, hr * 0.95f, SKIN, 8, 5);
  // hair tufts at the sides and back, under the cap
  addBlob(m, px(0, -hr * 0.45f), hy + hr * 0.1f, pz(0, -hr * 0.45f), hr * 0.95f, hr * 0.7f, hr * 0.6f, HAIR, 6, 3);
  for (int s = -1; s <= 1; s += 2)
    addBlob(m, px(s * hr * 0.9f, hr * 0.05f), hy + hr * 0.12f, pz(s * hr * 0.9f, hr * 0.05f), hr * 0.16f, hr * 0.3f, hr * 0.2f, HAIR, 4, 3);
  // the face: features painted on panels tipped back to follow the head's curve, so they
  // read from the game's high camera
  const float TILT = 0.55f;
  auto feat = [&](float lx, float y, float hw, float hh, uint32_t col, float glow) {
    // where the sphere's front is at this height, pushed a hair outward
    float dy = (y - hy) / hr;
    float lz = hr * 0.95f * sqrtf(fmaxf(0.05f, 1 - dy * dy - (lx / hr) * (lx / hr) * 0.6f)) + 0.006f;
    float ct = cosf(TILT), st = sinf(TILT);
    float c[4][3] = {{-hw, -hh, 0}, {hw, -hh, 0}, {hw, hh, 0}, {-hw, hh, 0}};
    float q[12];
    for (int i = 0; i < 4; i++) {
      float ox = lx + c[i][0], oy = c[i][1] * ct, oz = lz - c[i][1] * st;
      q[i * 3] = px(ox, oz); q[i * 3 + 1] = y + oy; q[i * 3 + 2] = pz(ox, oz);
    }
    addQuad(m, q, col, glow);
  };
  for (int s2 = -1; s2 <= 1; s2 += 2) {
    float ex = s2 * hr * 0.34f;
    feat(ex, hy + hr * 0.08f, hr * 0.13f, hr * 0.19f, INK, 0);                                  // big eyes
    feat(ex + s2 * hr * 0.04f, hy + hr * 0.17f, hr * 0.05f, hr * 0.055f, 0xFFFFFF, 0.7f);     // shine
    feat(ex - s2 * hr * 0.04f, hy - hr * 0.02f, hr * 0.025f, hr * 0.03f, 0xFFFFFF, 0.5f);
    feat(s2 * hr * 0.6f, hy - hr * 0.2f, hr * 0.13f, hr * 0.07f, 0xFF9C8C, 0);                 // cheeks
    feat(ex, hy + hr * 0.38f, hr * 0.1f, hr * 0.022f, 0x7A3A20, 0);                             // brows
  }
  if (plush) {
    // a small sweet smile
    feat(0, hy - hr * 0.27f, hr * 0.09f, hr * 0.045f, 0x6A2228, 0);
    feat(0, hy - hr * 0.3f, hr * 0.05f, hr * 0.02f, 0xF07070, 0);
  } else {
    // a wide grin with a tongue
    feat(0, hy - hr * 0.28f, hr * 0.2f, hr * 0.08f, 0x5A1A1E, 0);
    feat(0, hy - hr * 0.21f, hr * 0.18f, hr * 0.022f, 0xFFFFFF, 0);
    feat(0, hy - hr * 0.33f, hr * 0.09f, hr * 0.035f, 0xF06A6A, 0);
  }
  float top = hy + hr * 0.92f;
  if (plush) {
    // a big soft beret worn back and tipped to one side, with a little stalk
    addBlob(m, px(-hr * 0.12f, -hr * 0.3f), top - hr * 0.1f, pz(-hr * 0.12f, -hr * 0.3f), hr * 1.12f, hr * 0.32f, hr * 1.02f, BERET, 8, 3);
    addBlob(m, px(-hr * 0.12f, -hr * 0.3f), top - hr * 0.3f, pz(-hr * 0.12f, -hr * 0.3f), hr * 0.92f, hr * 0.2f, hr * 0.85f, 0x902640, 7, 3);
    addCyl(m, px(-hr * 0.12f, -hr * 0.3f), top + hr * 0.18f, pz(-hr * 0.12f, -hr * 0.3f), 0.018f, top + hr * 0.38f, 0.012f, 0x7A2034, 4);
    addPanel(m, px(-hr * 0.5f, hr * 0.3f), top + hr * 0.08f, pz(-hr * 0.5f, hr * 0.3f), hr * 0.16f, hr * 0.05f, face, 0xE07090, 0.35f);
    return;
  }
  // a glossy cap worn back on the head, its brim tipped up
  addBlob(m, px(0, -hr * 0.28f), top - hr * 0.2f, pz(0, -hr * 0.28f), hr * 0.98f, hr * 0.48f, hr * 0.9f, CAP, 10, 4);
  addBlob(m, px(0, -hr * 0.28f), top + hr * 0.26f, pz(0, -hr * 0.28f), 0.03f, 0.02f, 0.03f, 0xC02020, 5, 3);
  addLimb(m, px(0, hr * 0.55f), top - hr * 0.12f, pz(0, hr * 0.55f), hr * 0.62f, 0.014f, hr * 0.36f, face, 0xD82620, 0.55f);
  addPanel(m, px(-hr * 0.4f, hr * 0.35f), top + hr * 0.02f, pz(-hr * 0.4f, hr * 0.35f), hr * 0.14f, hr * 0.05f, face, 0xFF8A80, 0.4f);
}

static void buildWrenCute(MeshBuf &m, int style, float x, float z, float face, float walkT, bool moving) {
  if (style == 4) { buildWrenToy(m, x, z, face, walkT, moving); return; }
  if (style >= 5) { buildWrenChibi(m, style, x, z, face, walkT, moving); return; }
  float cs = cosf(face), sn = sinf(face);
  auto px = [&](float lx, float lz) { return x + lx * cs - lz * sn; };
  auto pz = [&](float lx, float lz) { return z + lx * sn + lz * cs; };
  float sw = moving ? sinf(walkT * 3.0f) * 0.5f : 0;
  float b = moving ? fabsf(sinf(walkT * 3.0f)) * 0.07f : 0;     // a bouncier walk
  const uint32_t SKIN = 0xF6D2B8, HAIR = 0xB0562A, CHEEK = 0xF29A8C, EYE = 0x2A1A14, SHINE = 0xFFFFFF;
  uint32_t body = 0x7A6A4A, legs = 0x4A3A30, trim = 0xE0A83A;
  if (style == 2) { body = 0xE0A83A; legs = 0x5A3A3A; trim = 0xB8343A; }
  if (style == 3) { body = 0xC8602A; legs = 0x4A3028; trim = 0xF4E8D8; }
  addDisc(m, x, 0.03f, z, 0.3f, 0x1E2420, 6);
  // stubby legs and round feet
  for (int s = -1; s <= 1; s += 2) {
    float lx = s * 0.09f;
    addLimb(m, px(lx, 0), 0.3f + b, pz(lx, 0), 0.06f, 0.12f, 0.06f, face, legs, s * sw);
    addBlob(m, px(lx, 0.03f + s * sw * 0.08f), 0.05f + b * 0.5f, pz(lx, 0.03f + s * sw * 0.08f), 0.075f, 0.05f, 0.1f, 0x2A2220, 5, 3);
  }
  // a round little body
  float by = 0.48f + b;
  addBlob(m, x, by, z, 0.21f, 0.22f, 0.18f, body, 7, 4);
  if (style == 3) addBlob(m, px(0, 0.1f), by - 0.02f, pz(0, 0.1f), 0.12f, 0.15f, 0.09f, trim, 6, 3); // pale tummy
  if (style == 1) addCyl(m, x, by - 0.06f, z, 0.25f, by + 0.2f, 0.12f, shadeHex(body, 0.85f), 7);     // a short cape
  // arms: little mitts
  for (int s = -1; s <= 1; s += 2) {
    float ax = s * 0.22f;
    addLimb(m, px(ax, 0), by + 0.14f, pz(ax, 0), 0.05f, 0.11f, 0.05f, face, body, -s * sw * 0.7f);
    addBlob(m, px(ax, -s * sw * 0.1f), by - 0.1f, pz(ax, -s * sw * 0.1f), 0.06f, 0.06f, 0.06f, style == 2 ? trim : SKIN, 5, 3);
  }
  // the big head
  float hr = 0.25f, hy = by + 0.2f + hr * 0.95f;
  addBlob(m, x, hy, z, hr, hr * 0.95f, hr * 0.95f, SKIN, 8, 5);
  // scarf (style 2 has a long one that trails)
  addCyl(m, x, by + 0.15f, z, 0.17f, by + 0.23f, 0.15f, trim, 7);
  if (style == 2) addLimb(m, px(0.08f, -0.12f), by + 0.2f, pz(0.08f, -0.12f), 0.05f, 0.14f, 0.025f, face, trim, -0.4f - sw * 0.3f);
  // hair: a fringe and a fluffy back
  addBlob(m, px(0, -hr * 0.12f), hy + hr * 0.22f, pz(0, -hr * 0.12f), hr * 1.06f, hr * 0.82f, hr * 1.04f, HAIR, 7, 3);
  addBlob(m, px(0, hr * 0.55f), hy + hr * 0.62f, pz(0, hr * 0.55f), hr * 0.7f, hr * 0.2f, hr * 0.28f, HAIR, 6, 3); // fringe
  addBlob(m, px(0, -hr * 0.7f), hy - hr * 0.1f, pz(0, -hr * 0.7f), hr * 0.6f, hr * 0.5f, hr * 0.3f, HAIR, 6, 3);   // fluff at the back
  // big eyes with a shine, rosy cheeks, a small mouth
  for (int s = -1; s <= 1; s += 2) {
    float ex = s * hr * 0.36f, ez = hr * 0.93f;
    addPanel(m, px(ex, ez), hy - hr * 0.05f, pz(ex, ez), hr * 0.12f, hr * 0.19f, face, EYE);
    addPanel(m, px(ex + s * hr * 0.04f, ez + 0.01f), hy + hr * 0.04f, pz(ex + s * hr * 0.04f, ez + 0.01f), hr * 0.045f, hr * 0.05f, face, SHINE, 0.6f);
    addPanel(m, px(s * hr * 0.6f, hr * 0.8f), hy - hr * 0.3f, pz(s * hr * 0.6f, hr * 0.8f), hr * 0.12f, hr * 0.06f, face, CHEEK);
  }
  addPanel(m, px(0, hr * 0.95f), hy - hr * 0.4f, pz(0, hr * 0.95f), hr * 0.08f, hr * 0.025f, face, 0x8A3A30);
  float top = hy + hr * 0.95f;
  if (style == 1) {
    // a deerstalker: checked cap, brims fore and aft, flaps tied up
    addBlob(m, x, top - 0.05f, z, hr * 1.1f, hr * 0.55f, hr * 1.1f, 0xB89A6A, 7, 3);
    addLimb(m, px(0, hr * 0.95f), top - 0.06f, pz(0, hr * 0.95f), hr * 0.55f, 0.012f, hr * 0.3f, face, 0x8A7048);
    addLimb(m, px(0, -hr * 0.95f), top - 0.06f, pz(0, -hr * 0.95f), hr * 0.55f, 0.012f, hr * 0.3f, face, 0x8A7048);
    addBlob(m, x, top + hr * 0.42f, z, hr * 0.12f, hr * 0.1f, hr * 0.12f, 0x8A7048, 4, 3);
    // and a magnifying glass in her right hand
    float gx = 0.26f, gy = by + 0.02f;
    addLimb(m, px(gx, 0.08f), gy + 0.08f, pz(gx, 0.08f), 0.015f, 0.06f, 0.015f, face, 0x5A3A22);
    addCyl(m, px(gx, 0.08f), gy + 0.08f, pz(gx, 0.08f), 0.08f, gy + 0.1f, 0.08f, 0xD8B040, 8);
    addCyl(m, px(gx, 0.08f), gy + 0.1f, pz(gx, 0.08f), 0.065f, gy + 0.11f, 0.065f, 0xBFE8F0, 8, 0.4f);
  } else if (style == 2) {
    // a big soft beret with a stalk
    addCyl(m, px(-0.03f, -0.02f), top - 0.08f, pz(-0.03f, -0.02f), hr * 1.0f, top + 0.02f, hr * 1.2f, 0x8A2A3A, 8);
    addBlob(m, px(-0.03f, -0.02f), top + 0.03f, pz(-0.03f, -0.02f), hr * 1.18f, hr * 0.22f, hr * 1.18f, 0x9A3444, 8, 3);
    addCyl(m, x, top + 0.07f, z, 0.015f, top + 0.12f, 0.01f, 0x6A2030, 4);
  } else if (style == 3) {
    // a fox hood: rust hood, pointed ears with cream insides, a little lantern
    addBlob(m, px(0, -hr * 0.15f), hy + hr * 0.18f, pz(0, -hr * 0.15f), hr * 1.18f, hr * 1.12f, hr * 1.12f, body, 8, 4);
    for (int s = -1; s <= 1; s += 2) {
      float ex = s * hr * 0.62f;
      addCone(m, px(ex, -hr * 0.1f), top - 0.04f, pz(ex, -hr * 0.1f), hr * 0.32f, top + hr * 0.62f, body, 5);
      addCone(m, px(ex, hr * 0.02f), top - 0.02f, pz(ex, hr * 0.02f), hr * 0.17f, top + hr * 0.45f, trim, 4);
    }
    // the face shows through the hood's front
    addBlob(m, px(0, hr * 0.12f), hy - hr * 0.02f, pz(0, hr * 0.12f), hr * 0.86f, hr * 0.82f, hr * 0.86f, SKIN, 8, 4);
    for (int s = -1; s <= 1; s += 2) {
      float ex = s * hr * 0.33f, ez = hr * 0.96f;
      addPanel(m, px(ex, ez), hy - hr * 0.05f, pz(ex, ez), hr * 0.12f, hr * 0.19f, face, EYE);
      addPanel(m, px(ex + s * hr * 0.04f, ez + 0.01f), hy + hr * 0.04f, pz(ex + s * hr * 0.04f, ez + 0.01f), hr * 0.045f, hr * 0.05f, face, SHINE, 0.6f);
      addPanel(m, px(s * hr * 0.55f, hr * 0.84f), hy - hr * 0.3f, pz(s * hr * 0.55f, hr * 0.84f), hr * 0.11f, hr * 0.06f, face, CHEEK);
    }
    float lx = 0.27f, ly = by - 0.12f;
    addCyl(m, px(lx, 0.06f), ly - 0.1f, pz(lx, 0.06f), 0.05f, ly, 0.05f, 0xFFD27A, 6, 0.9f);
    addCyl(m, px(lx, 0.06f), ly, pz(lx, 0.06f), 0.055f, ly + 0.03f, 0.03f, 0x3A2E24, 6);
    addCyl(m, px(lx, 0.06f), ly - 0.13f, pz(lx, 0.06f), 0.055f, ly - 0.1f, 0.055f, 0x3A2E24, 6);
  }
}

void buildFigure(MeshBuf &m, int look, float x, float z, float face, float walkT, bool moving, float bob) {
  if (look == L_WREN && g_wrenStyle > 0) { buildWrenCute(m, g_wrenStyle, x, z, face, walkT, moving); return; }
  const Look &L = LOOKS[look];
  float h = L.height, w = L.build;
  float cs = cosf(face), sn = sinf(face);
  auto px = [&](float lx, float lz) { return x + lx * cs - lz * sn; };
  auto pz = [&](float lx, float lz) { return z + lx * sn + lz * cs; };
  float sw = moving ? sinf(walkT * 3.0f) * 0.55f : 0;
  float up = moving ? fabsf(sinf(walkT * 3.0f)) * 0.04f : 0;
  float base = bob + up;
  // shadow
  addDisc(m, x, 0.03f, z, 0.32f * w, 0x1E2420, 6);
  float hip = 0.78f * h + base, sh = 1.42f * h + base;
  // legs
  for (int s = -1; s <= 1; s += 2) {
    float lx = s * 0.1f * w;
    addLimb(m, px(lx, 0), hip, pz(lx, 0), 0.075f * w, hip * 0.5f, 0.08f, face, L.legs, s * sw);
  }
  // body
  if (L.dress) {
    addCyl(m, x, 0.08f + base, z, 0.3f * w, hip + 0.08f, 0.22f * w, L.legs, 6);
  }
  // coat skirt for long coats
  if (look == L_VANE || look == L_VANEMASK || look == L_CREWE || look == L_GENT || look == L_CURATOR || look == L_HALLORAN ||
      look == L_BRAM || look == L_WREN)
    addCyl(m, x, hip - 0.38f * h, z, 0.26f * w, hip + 0.06f, 0.21f * w, shadeHex(L.coat, 0.92f), 6);
  addLimb(m, x, sh, z, 0.21f * w, (sh - hip) * 0.5f + 0.04f, 0.14f, face, L.coat);
  // collar / scarf
  addLimb(m, px(0, 0.02f), sh + 0.04f, pz(0, 0.02f), 0.15f * w, 0.05f, 0.11f, face, L.accent);
  // arms
  for (int s = -1; s <= 1; s += 2) {
    float ax = s * 0.27f * w;
    addLimb(m, px(ax, 0), sh - 0.02f, pz(ax, 0), 0.065f, 0.3f * h, 0.07f, face, L.coat, -s * sw * 0.8f);
  }
  // head
  float hy = sh + 0.22f * h, hr = 0.165f * (0.85f + 0.15f * h);
  if (L.age == 0) hr *= 1.15f;
  addBlob(m, x, hy, z, hr, hr * 1.1f, hr, L.skin, 6, 4);
  // eyes (on the front of the face)
  for (int s = -1; s <= 1; s += 2) {
    float ex = s * hr * 0.38f, ez = hr * 0.9f;
    if (L.eyepatch && s < 0) {
      addPanel(m, px(ex, ez * 1.08f), hy + hr * 0.1f, pz(ex, ez * 1.08f), hr * 0.25f, hr * 0.18f, face, 0x141414);
      continue;
    }
    addPanel(m, px(ex, ez * 1.08f), hy + hr * 0.05f, pz(ex, ez * 1.08f), hr * 0.11f, hr * 0.13f, face, 0x1A1410);
  }
  if (L.beard) addLimb(m, px(0, hr * 0.8f), hy - hr * 0.25f, pz(0, hr * 0.8f), hr * (L.beard == 2 ? 0.55f : 0.4f),
                       hr * (L.beard == 2 ? 0.35f : 0.08f), 0.05f, face, L.hair);
  // hair
  if (L.hairStyle != HAIR_BALD) {
    float hs = 1.08f;
    addBlob(m, px(0, -hr * 0.18f), hy + hr * 0.25f, pz(0, -hr * 0.18f), hr * hs, hr * 0.85f, hr * hs, L.hair, 6, 3);
    if (L.hairStyle == HAIR_LONG || L.hairStyle == HAIR_CURLS || L.hairStyle == HAIR_BRAID)
      addLimb(m, px(0, -hr * 0.6f), hy + hr * 0.2f, pz(0, -hr * 0.6f), hr * 0.85f, hr * 1.1f, hr * 0.35f, face, L.hair);
    if (L.hairStyle == HAIR_BOB)
      addLimb(m, px(0, -hr * 0.2f), hy + hr * 0.4f, pz(0, -hr * 0.2f), hr * 1.12f, hr * 0.75f, hr * 0.9f, face, L.hair);
  }
  float top = hy + hr * 1.0f;
  switch (L.hat) {
    case HAT_TOP:
      addCyl(m, x, top - 0.04f, z, hr * 1.5f, top, hr * 1.5f, 0x141418, 6);
      addCyl(m, x, top, z, hr * 0.92f, top + 0.3f * h, hr * 0.95f, 0x18181C, 6);
      break;
    case HAT_CAP:
      addBlob(m, x, top - 0.05f, z, hr * 1.1f, hr * 0.45f, hr * 1.1f, L.accent == 0xEFEFEF ? 0x2A3A5A : shadeHex(L.coat, 0.8f), 6, 3);
      addLimb(m, px(0, hr * 0.9f), top - 0.05f, pz(0, hr * 0.9f), hr * 0.7f, 0.015f, hr * 0.35f, face, 0x1E1E22);
      break;
    case HAT_HELMET:
      addBlob(m, x, top + 0.06f, z, hr * 1.05f, hr * 1.4f, hr * 1.05f, 0x1A2234, 6, 4);
      addBlob(m, x, top + 0.06f + hr * 1.3f, z, hr * 0.15f, hr * 0.15f, hr * 0.15f, 0xB9853A, 4, 3);
      break;
    case HAT_BONNET:
      addBlob(m, px(0, -hr * 0.1f), hy + hr * 0.3f, pz(0, -hr * 0.1f), hr * 1.25f, hr * 1.05f, hr * 1.2f, L.accent, 6, 3);
      break;
    case HAT_CAPTAIN:
      addCyl(m, x, top - 0.06f, z, hr * 1.05f, top + 0.08f, hr * 1.2f, 0x1A1E2A, 6);
      addCyl(m, x, top + 0.08f, z, hr * 1.2f, top + 0.1f, hr * 1.2f, 0xEFEFEF, 6);
      addLimb(m, px(0, hr * 1.0f), top - 0.04f, pz(0, hr * 1.0f), hr * 0.8f, 0.015f, hr * 0.35f, face, 0x101010);
      break;
    case HAT_HOOD:
      addBlob(m, px(0, -hr * 0.1f), hy + hr * 0.2f, pz(0, -hr * 0.1f), hr * 1.35f, hr * 1.35f, hr * 1.3f, L.coat, 6, 3);
      addLimb(m, px(0, hr * 0.85f), hy + hr * 0.5f, pz(0, hr * 0.85f), hr * 0.6f, hr * 0.6f, 0.03f, face, 0xE8E0D0); // the clay mask
      break;
    case HAT_BUN:
      addBlob(m, px(0, -hr * 0.7f), top - 0.02f, pz(0, -hr * 0.7f), hr * 0.45f, hr * 0.4f, hr * 0.45f, L.hair, 6, 3);
      break;
    default: break;
  }
  if (look == L_VANEMASK)
    addLimb(m, px(0, hr * 0.92f), hy + hr * 0.6f, pz(0, hr * 0.92f), hr * 0.7f, hr * 0.7f, 0.03f, face, 0xE8E0D0);
}

// --- 3D creatures ------------------------------------------------------------------------------

// --- the world model: a variant of the species from the creature's seed ---------
struct Variant { uint32_t c1, c2, c3; float size; };
static uint32_t packRGB(float r, float g, float b) {
  return ((uint32_t)clampi((int)(r * 255), 0, 255) << 16) | ((uint32_t)clampi((int)(g * 255), 0, 255) << 8) |
         (uint32_t)clampi((int)(b * 255), 0, 255);
}
static Variant decodeVariant(int sp, uint32_t seed) {
  static struct { int sp; uint32_t seed; Variant v; } cache[16];
  static int next = 0;
  for (auto &c : cache) if (c.sp == sp && c.seed == seed) return c.v;
  const Shape &S = SPECIES[sp].shape;
  float in[WM_C + WM_Z];
  uint32_t cols[3] = {S.c1, S.c2, S.c3};
  for (int i = 0; i < 3; i++) {
    in[i * 3] = ((cols[i] >> 16) & 255) / 255.0f; in[i * 3 + 1] = ((cols[i] >> 8) & 255) / 255.0f; in[i * 3 + 2] = (cols[i] & 255) / 255.0f;
  }
  in[9] = S.size / 1.5f;
  // the code: normal noise from the seed (Box-Muller)
  for (int i = 0; i < WM_Z; i += 2) {
    float u1 = ((hash32(seed, i) & 0xFFFFFF) + 1) / 16777217.0f, u2 = (hash32(seed, i + 1) & 0xFFFFFF) / 16777216.0f;
    float r = sqrtf(-2 * logf(u1));
    in[WM_C + i] = r * cosf(6.2831853f * u2);
    if (i + 1 < WM_Z) in[WM_C + i + 1] = r * sinf(6.2831853f * u2);
  }
  float h1[WM_H], h2[WM_H], o[WM_X];
  for (int j = 0; j < WM_H; j++) {
    float a = WM_B1[j];
    for (int k = 0; k < WM_C + WM_Z; k++) a += WM_W1[j * (WM_C + WM_Z) + k] * in[k];
    h1[j] = tanhf(a);
  }
  for (int j = 0; j < WM_H; j++) {
    float a = WM_B2[j];
    for (int k = 0; k < WM_H; k++) a += WM_W2[j * WM_H + k] * h1[k];
    h2[j] = tanhf(a);
  }
  for (int j = 0; j < WM_X; j++) {
    float a = WM_B3[j];
    for (int k = 0; k < WM_H; k++) a += WM_W3[j * WM_H + k] * h2[k];
    o[j] = a;
  }
  Variant v = {packRGB(o[0], o[1], o[2]), packRGB(o[3], o[4], o[5]), packRGB(o[6], o[7], o[8]), clampf(o[9], 0.85f, 1.15f)};
  cache[next] = {sp, seed, v};
  next = (next + 1) & 15;
  return v;
}

void buildMon3D(MeshBuf &m, int sp, float x, float y, float z, float face, float t, float scale, uint32_t seed,
                float flash) {
  if (sp < 0 || sp >= N_SPECIES) return;
  const Shape &S = SPECIES[sp].shape;
  Variant var = decodeVariant(sp, seed);
  float k = scale * S.size * var.size;
  float cs = cosf(face), sn = sinf(face);
  bool side = fabsf(sn) > 0.7f;
  auto px = [&](float lx, float lz) { return x + (lx * cs - lz * sn) * k; };
  auto pz = [&](float lx, float lz) { return z + (lx * sn + lz * cs) * k; };
  auto blob = [&](float lx, float ly, float lz, float rx, float ry, float rz, uint32_t c, int seg, int rings, float glow) {
    if (side) { float q = rx; rx = rz; rz = q; }
    addBlob(m, px(lx, lz), y + ly * k, pz(lx, lz), rx * k, ry * k, rz * k, c, seg, rings, glow + flash);
  };
  // the world model's colours for this one
  uint32_t c1 = var.c1, c2 = var.c2, c3 = var.c3;
  float bob = sinf(t * 3.0f + seed) * 0.03f;
  float flap = sinf(t * 8.0f + seed) * 0.6f;
  // shadow
  addDisc(m, x, 0.03f, z, 0.45f * k, 0x1E2420, 6);
  float bodyY = 0.42f, headY = 0.82f, headZ = 0.3f, headR = 0.27f;
  switch (S.body) {
    case 0: // round
      blob(0, 0.42f + bob, 0, 0.42f, 0.38f, 0.4f, c1, 8, 5, 0);
      blob(0, 0.36f + bob, 0.18f, 0.28f, 0.24f, 0.24f, c2, 7, 4, 0);
      headY = 0.78f + bob; headZ = 0.2f;
      break;
    case 1: // long, four-legged
      blob(0, 0.5f + bob, -0.05f, 0.3f, 0.28f, 0.55f, c1, 8, 5, 0);
      blob(0, 0.42f + bob, 0.05f, 0.22f, 0.18f, 0.4f, c2, 7, 4, 0);
      headY = 0.78f + bob; headZ = 0.55f; headR = 0.25f;
      break;
    case 2: // upright
      blob(0, 0.62f + bob, 0, 0.3f, 0.42f, 0.27f, c1, 8, 5, 0);
      blob(0, 0.58f + bob, 0.12f, 0.2f, 0.3f, 0.18f, c2, 7, 4, 0);
      headY = 1.15f + bob; headZ = 0.05f; headR = 0.27f;
      break;
    case 3: { // serpent
      for (int i = 0; i < 5; i++) {
        float lz = -0.7f + i * 0.3f, lx = sinf(t * 2 + i * 1.1f) * 0.18f;
        blob(lx, 0.2f + i * 0.06f, lz, 0.2f - i * 0.01f, 0.18f, 0.2f, i & 1 ? c1 : shadeHex(c1, 0.85f), 7, 4, 0);
      }
      headY = 0.62f + bob; headZ = 0.62f; headR = 0.24f;
      break;
    }
    case 4: // bird
      blob(0, 0.55f + bob, 0, 0.3f, 0.3f, 0.38f, c1, 8, 5, 0);
      blob(0, 0.5f + bob, 0.12f, 0.22f, 0.22f, 0.25f, c2, 7, 4, 0);
      headY = 0.92f + bob; headZ = 0.25f; headR = 0.22f;
      break;
    case 5: { // floating wisp
      float fy = 0.3f + sinf(t * 2.0f + seed) * 0.12f;
      blob(0, 0.55f + fy, 0, 0.32f, 0.36f, 0.32f, c1, 8, 5, 0.6f);
      addCone(m, x, y + (0.75f + fy) * k, z, 0.22f * k, y + (1.35f + fy) * k, c2, 6);
      headY = 0.6f + fy; headZ = 0.22f; headR = 0.0f;
      break;
    }
    case 6: // stacked stones
      blob(0, 0.22f, 0, 0.42f, 0.24f, 0.38f, c1, 7, 4, 0);
      blob(0.03f, 0.58f + bob * 0.5f, 0, 0.32f, 0.2f, 0.3f, shadeHex(c1, 1.08f), 7, 4, 0);
      headY = 0.86f + bob; headZ = 0; headR = 0.22f;
      break;
  }
  bodyY = headY;
  // legs
  int nl = S.legs;
  for (int i = 0; i < nl; i++) {
    float lx = (i & 1 ? 0.18f : -0.18f), lz = nl <= 2 ? 0.02f : (i < 2 ? 0.22f : -0.22f);
    if (nl == 6) lz = (i / 2 - 1) * 0.22f;
    float swing = sinf(t * 6 + i * 1.7f) * 0.1f;
    float legTop = S.body == 2 ? 0.35f : 0.32f;
    addLimb(m, px(lx, lz), y + legTop * k, pz(lx, lz), 0.07f * k, legTop * 0.5f * k, 0.07f * k, face, shadeHex(c1, 0.8f), swing);
  }
  // head
  if (headR > 0) {
    blob(0, headY, headZ, headR, headR * 0.95f, headR, c1, 8, 5, 0);
    if (S.head == 1) blob(0, headY - 0.06f, headZ + headR * 0.85f, headR * 0.5f, headR * 0.4f, headR * 0.55f, c2, 6, 4, 0);
    if (S.head == 2) blob(0, headY - 0.02f, headZ + headR * 0.95f, headR * 0.25f, headR * 0.22f, headR * 0.55f, c3, 5, 3, 0);
  }
  float eyeZ = headZ + (headR > 0 ? headR * 0.85f : 0.3f), eyeY = headY + (headR > 0 ? headR * 0.15f : 0);
  for (int s = -1; s <= 1; s += 2) {
    float ex = s * (headR > 0 ? headR * 0.42f : 0.12f);
    blob(ex, eyeY, eyeZ, 0.06f, 0.075f, 0.04f, 0x101014, 5, 3, 0);
    blob(ex + 0.015f, eyeY + 0.025f, eyeZ + 0.03f, 0.02f, 0.02f, 0.015f, 0xFFFFFF, 4, 2, 0.5f);
  }
  // ears / horns
  float ey = headY + headR * 0.75f;
  for (int s = -1; s <= 1; s += 2) {
    float ex = s * headR * 0.55f;
    switch (S.ears) {
      case 1: addCone(m, px(ex, headZ - 0.03f), y + (ey - 0.02f) * k, pz(ex, headZ - 0.03f), 0.09f * k, y + (ey + 0.28f) * k, c3, 4); break;
      case 2: blob(ex * 1.2f, ey, headZ - 0.05f, 0.11f, 0.11f, 0.05f, c3, 5, 3, 0); break;
      case 3:
        addLimb(m, px(ex, headZ - 0.05f), y + (ey + 0.3f) * k, pz(ex, headZ - 0.05f), 0.025f * k, 0.16f * k, 0.025f * k, face, c3, 0.0f);
        addLimb(m, px(ex * 1.6f, headZ - 0.05f), y + (ey + 0.38f) * k, pz(ex * 1.6f, headZ - 0.05f), 0.02f * k, 0.09f * k, 0.02f * k, face, c3, 0.5f);
        break;
      case 4: addCone(m, px(ex * 0.8f, headZ), y + ey * k, pz(ex * 0.8f, headZ), 0.06f * k, y + (ey + 0.22f) * k, c3, 4); break;
      case 5:
        addLimb(m, px(ex * 0.6f, headZ + 0.05f), y + (ey + 0.25f) * k, pz(ex * 0.6f, headZ + 0.05f), 0.015f * k, 0.13f * k, 0.015f * k, face, c3, -0.4f);
        blob(ex * 0.6f, ey + 0.27f, headZ + 0.18f, 0.04f, 0.04f, 0.04f, c3, 4, 2, 0.3f);
        break;
      default: break;
    }
  }
  // tail
  switch (S.tail) {
    case 1: blob(0, 0.42f, -0.5f, 0.07f, 0.07f, 0.18f, c1, 5, 3, 0); blob(0, 0.5f, -0.72f, 0.06f, 0.06f, 0.14f, c3, 5, 3, 0); break;
    case 2: {
      float fy = 0.55f + sinf(t * 9) * 0.03f;
      addCone(m, px(0, -0.55f), y + fy * k, pz(0, -0.55f), 0.11f * k, y + (fy + 0.4f) * k, 0xFFB040, 5);
      blob(0, fy + 0.05f, -0.55f, 0.08f, 0.08f, 0.08f, 0xFFE070, 5, 3, 1.0f);
      break;
    }
    case 3: {
      float a[12] = {px(-0.05f, -0.35f), y + 0.4f * k, pz(-0.05f, -0.35f), px(-0.35f, -0.7f), y + 0.9f * k, pz(-0.35f, -0.7f),
                     px(0.35f, -0.7f), y + 0.9f * k, pz(0.35f, -0.7f), px(0.05f, -0.35f), y + 0.4f * k, pz(0.05f, -0.35f)};
      addQuad(m, a, c3);
      float b[12] = {a[9], a[10], a[11], a[6], a[7], a[8], a[3], a[4], a[5], a[0], a[1], a[2]};
      addQuad(m, b, shadeHex(c3, 0.8f));
      break;
    }
    case 4: {
      float wag = sinf(t * 5) * 0.2f;
      float a[12] = {px(0, -0.4f), y + 0.35f * k, pz(0, -0.4f), px(wag, -0.8f), y + 0.15f * k, pz(wag, -0.8f),
                     px(wag, -0.8f), y + 0.65f * k, pz(wag, -0.8f), px(0, -0.4f), y + 0.5f * k, pz(0, -0.4f)};
      addQuad(m, a, c3);
      float b[12] = {a[9], a[10], a[11], a[6], a[7], a[8], a[3], a[4], a[5], a[0], a[1], a[2]};
      addQuad(m, b, shadeHex(c3, 0.8f));
      break;
    }
    default: break;
  }
  // wings
  if (S.wings) {
    float span = S.wings == 2 ? 0.75f : 0.6f, wy = S.body == 4 ? 0.6f : bodyY - 0.25f;
    uint32_t wc = S.wings == 2 ? c3 : shadeHex(c1, 0.9f);
    for (int s = -1; s <= 1; s += 2) {
      float tipY = wy + 0.25f + flap * 0.3f * s * s;
      float a[12] = {px(s * 0.15f, 0.12f), y + wy * k, pz(s * 0.15f, 0.12f), px(s * span, 0.0f), y + tipY * k, pz(s * span, 0.0f),
                     px(s * span * 0.9f, -0.35f), y + (tipY - 0.05f) * k, pz(s * span * 0.9f, -0.35f), px(s * 0.15f, -0.25f), y + wy * k,
                     pz(s * 0.15f, -0.25f)};
      if (s > 0) {
        float b[12] = {a[9], a[10], a[11], a[6], a[7], a[8], a[3], a[4], a[5], a[0], a[1], a[2]};
        addQuad(m, b, wc);
        addQuad(m, a, shadeHex(wc, 0.75f));
      } else {
        addQuad(m, a, wc);
        float b[12] = {a[9], a[10], a[11], a[6], a[7], a[8], a[3], a[4], a[5], a[0], a[1], a[2]};
        addQuad(m, b, shadeHex(wc, 0.75f));
      }
    }
  }
}

// --- 2D creatures (menus) -----------------------------------------------------------------------------

void drawMon2D(int sp, float cx, float cy, float size, uint32_t seed) {
  if (sp < 0 || sp >= N_SPECIES) return;
  const Shape &S = SPECIES[sp].shape;
  float k = size;
  uint16_t c1 = c565(S.c1), c2 = c565(S.c2), c3 = c565(S.c3);
  (void)seed;
  float by = cy + 0.1f * k;
  if (S.body == 5) {
    Raster::ellipse(cx, by - 0.05f * k, 0.32f * k, 0.36f * k, c1, 220);
    poly3(cx - 0.2f * k, by - 0.2f * k, cx + 0.2f * k, by - 0.2f * k, cx, by - 0.75f * k, c2);
  } else if (S.body == 3) {
    for (int i = 0; i < 4; i++) Raster::circle(cx - 0.45f * k + i * 0.25f * k, by + sinf(i * 1.3f) * 0.08f * k, 0.18f * k, c1);
  } else if (S.body == 6) {
    Raster::ellipse(cx, by + 0.15f * k, 0.42f * k, 0.22f * k, c1);
    Raster::ellipse(cx, by - 0.18f * k, 0.32f * k, 0.18f * k, mix565(c1, 0xFFFF, 30));
  } else {
    Raster::ellipse(cx, by, 0.4f * k, (S.body == 2 ? 0.42f : 0.32f) * k, c1);
    Raster::ellipse(cx, by + 0.06f * k, 0.25f * k, 0.2f * k, c2);
  }
  float hx = cx + (S.body == 1 || S.body == 3 ? 0.35f * k : 0), hy = by - (S.body == 2 ? 0.48f : 0.32f) * k;
  if (S.body == 5) hy = by - 0.05f * k;
  if (S.body != 5) Raster::circle(hx, hy, 0.25f * k, c1);
  if (S.ears == 1 || S.ears == 4) {
    poly3(hx - 0.22f * k, hy - 0.08f * k, hx - 0.08f * k, hy - 0.16f * k, hx - 0.2f * k, hy - 0.42f * k, c3);
    poly3(hx + 0.22f * k, hy - 0.08f * k, hx + 0.08f * k, hy - 0.16f * k, hx + 0.2f * k, hy - 0.42f * k, c3);
  } else if (S.ears == 2) {
    Raster::circle(hx - 0.2f * k, hy - 0.18f * k, 0.1f * k, c3);
    Raster::circle(hx + 0.2f * k, hy - 0.18f * k, 0.1f * k, c3);
  } else if (S.ears == 3 || S.ears == 5) {
    Raster::line(hx - 0.1f * k, hy - 0.2f * k, hx - 0.25f * k, hy - 0.5f * k, 0.04f * k, c3);
    Raster::line(hx + 0.1f * k, hy - 0.2f * k, hx + 0.25f * k, hy - 0.5f * k, 0.04f * k, c3);
  }
  if (S.wings) {
    poly3(cx - 0.2f * k, by - 0.1f * k, cx - 0.75f * k, by - 0.45f * k, cx - 0.6f * k, by + 0.1f * k, S.wings == 2 ? c3 : mix565(c1, 0, 40));
    poly3(cx + 0.2f * k, by - 0.1f * k, cx + 0.75f * k, by - 0.45f * k, cx + 0.6f * k, by + 0.1f * k, S.wings == 2 ? c3 : mix565(c1, 0, 40));
  }
  if (S.tail == 2) Raster::circle(cx - 0.45f * k, by - 0.2f * k, 0.1f * k, c565(0xFFB040));
  float ex = 0.09f * k;
  Raster::circle(hx - ex, hy, 0.05f * k + 0.6f, 0x0841);
  Raster::circle(hx + ex, hy, 0.05f * k + 0.6f, 0x0841);
  Raster::circle(hx - ex + 0.02f * k, hy - 0.02f * k, 0.018f * k + 0.3f, 0xFFFF);
  Raster::circle(hx + ex + 0.02f * k, hy - 0.02f * k, 0.018f * k + 0.3f, 0xFFFF);
}

// --- portraits ------------------------------------------------------------------------------------------
// Painted-looking busts from the same description as the 3D figure.

void drawPortrait(int look, float x, float y, float w, float h, int mood) {
  if (look < 0 || look >= L_COUNT) return;
  const Look &L = LOOKS[look];
  if (mood < 0) mood = L.mood;
  // backdrop: a warm vignette card
  Raster::roundRect(x, y, w, h, 8, c565(0x2A2420));
  Raster::roundRect(x + 2, y + 2, w - 4, h - 4, 7, c565(0x4A3E34));
  Raster::ellipse(x + w * 0.5f, y + h * 0.42f, w * 0.48f, h * 0.42f, c565(0x6A5A48), 120);
  float cx = x + w * 0.5f, s = w / 100.0f;
  float hy = y + h * 0.42f;
  if (L.age == 0) hy += 6 * s;
  uint16_t skin = c565(L.skin), skinD = mix565(skin, 0x0000, 60), hair = c565(L.hair), coat = c565(L.coat),
           acc = c565(L.accent);
  float hw = 26 * s * (L.age == 0 ? 1.05f : 1.0f), hh = 32 * s;
  // shoulders and coat
  float shY = y + h * 0.78f;
  Raster::ellipse(cx, shY + 18 * s, 48 * s * L.build, 30 * s, coat);
  Raster::roundRect(cx - 46 * s * L.build, shY + 6 * s, 92 * s * L.build, h - (shY - y) - 8 * s, 10 * s, coat);
  // lapels
  poly3(cx - 14 * s, shY - 2 * s, cx, shY + 26 * s, cx - 26 * s, shY + 30 * s, mix565(coat, 0, 50));
  poly3(cx + 14 * s, shY - 2 * s, cx, shY + 26 * s, cx + 26 * s, shY + 30 * s, mix565(coat, 0, 50));
  // neck and collar/scarf
  Raster::roundRect(cx - 9 * s, hy + hh * 0.6f, 18 * s, 24 * s, 6 * s, skinD);
  Raster::ellipse(cx, shY, 19 * s, 8 * s, acc);
  if (look == L_WREN) Raster::roundRect(cx + 6 * s, shY, 9 * s, 30 * s, 4 * s, acc); // the scarf's tail
  // a bonnet's crown sits behind the head
  if (L.hat == HAT_BONNET) Raster::ellipse(cx, hy - 6 * s, hw * 1.32f, hh * 1.18f, mix565(acc, 0, 30));
  // hair behind the head
  if (L.hairStyle == HAIR_LONG || L.hairStyle == HAIR_CURLS || L.hairStyle == HAIR_BRAID) {
    Raster::ellipse(cx, hy + 14 * s, hw * 1.25f, hh * 1.25f, hair);
    if (L.hairStyle == HAIR_CURLS)
      for (int i = 0; i < 6; i++) Raster::circle(cx - hw * 1.1f + i * hw * 0.44f, hy + hh * 0.9f + (i & 1) * 4 * s, 9 * s, hair);
    if (L.hairStyle == HAIR_BRAID) Raster::roundRect(cx + hw * 0.6f, hy, 10 * s, 52 * s, 5 * s, hair);
  }
  if (L.hairStyle == HAIR_BOB) Raster::roundRect(cx - hw * 1.18f, hy - hh * 0.8f, hw * 2.36f, hh * 1.65f, 12 * s, hair);
  // ears and head
  Raster::ellipse(cx - hw * 0.95f, hy + 3 * s, 5 * s, 8 * s, skinD);
  Raster::ellipse(cx + hw * 0.95f, hy + 3 * s, 5 * s, 8 * s, skinD);
  Raster::ellipse(cx, hy, hw, hh, skin);
  // a little shading down one side (light from the upper left)
  Raster::ellipse(cx + hw * 0.45f, hy + 4 * s, hw * 0.55f, hh * 0.9f, skinD, 70);
  // hair on top
  switch (L.hairStyle) {
    case HAIR_BALD: break;
    case HAIR_SWEPT:
      Raster::ellipse(cx - 2 * s, hy - hh * 0.7f, hw * 1.05f, hh * 0.42f, hair);
      poly3(cx - hw, hy - hh * 0.5f, cx + hw * 1.05f, hy - hh * 0.75f, cx + hw * 1.0f, hy - hh * 0.2f, hair);
      break;
    case HAIR_MESSY:
      Raster::ellipse(cx, hy - hh * 0.62f, hw * 1.08f, hh * 0.5f, hair);
      for (int i = 0; i < 5; i++)
        poly3(cx - hw + i * hw * 0.5f, hy - hh * 0.55f, cx - hw + i * hw * 0.5f + 10 * s, hy - hh * 0.55f,
                                     cx - hw + i * hw * 0.5f + 2 * s, hy - hh * 0.2f + (i & 1) * 4 * s, hair);
      break;
    case HAIR_CURLS:
      for (int i = 0; i < 6; i++) Raster::circle(cx - hw * 0.9f + i * hw * 0.36f, hy - hh * 0.72f + (i & 1) * 3 * s, 10 * s, hair);
      break;
    case HAIR_BUN:
      Raster::ellipse(cx, hy - hh * 0.62f, hw * 1.02f, hh * 0.48f, hair);
      Raster::circle(cx, hy - hh * 1.08f, 10 * s, hair);
      break;
    default:
      Raster::ellipse(cx, hy - hh * 0.64f, hw * 1.04f, hh * 0.46f, hair);
      if (L.hairStyle == HAIR_BOB || L.hairStyle == HAIR_LONG || L.hairStyle == HAIR_BRAID)
        Raster::rect(cx - hw * 1.0f, hy - hh * 0.5f, hw * 2.0f, 6 * s, hair);
      break;
  }
  // eyes
  float ey = hy - 2 * s, ex = hw * 0.4f;
  for (int sd = -1; sd <= 1; sd += 2) {
    float x0 = cx + sd * ex;
    if (L.eyepatch && sd < 0) {
      Raster::line(cx - hw, hy - hh * 0.55f, cx + hw * 0.2f, hy + 4 * s, 2 * s, 0x0000);
      Raster::ellipse(x0, ey, 8 * s, 7 * s, 0x0841);
      continue;
    }
    float lid = mood == 2 ? 0.55f : mood == 4 ? 0.5f : 1.0f;
    Raster::ellipse(x0, ey, 6.5f * s, 4.5f * s * lid + 1, c565(0xF4EEE4));
    Raster::circle(x0 + 0.5f * s, ey + 0.3f * s, 3.3f * s, look == L_VANE ? c565(0x5A7A8A) : c565(0x4A3A2A));
    Raster::circle(x0 + 0.5f * s, ey + 0.3f * s, 1.7f * s, 0x0000);
    Raster::circle(x0 + 1.6f * s, ey - 1.2f * s, 0.9f * s + 0.3f, 0xFFFF);
    // brows
    float tilt = mood == 2 ? 3.0f : mood == 3 || mood == 5 ? -3.0f : mood == 4 ? (sd < 0 ? 2.0f : -1.5f) : 0;
    Raster::line(x0 - 7 * s, ey - 8 * s + sd * tilt * s * -1 * (mood == 2 ? -1 : 1), x0 + 7 * s, ey - 8 * s - sd * tilt * s * -1 * (mood == 2 ? -1 : 1),
                 2.2f * s, L.hairStyle == HAIR_BALD ? c565(0x8A8A8A) : mix565(hair, 0, 40));
  }
  if (L.glasses) {
    Raster::circle(cx - ex, ey, 9 * s, c565(0xB9853A), 200);
    Raster::circle(cx - ex, ey, 7.5f * s, c565(0xF4EEE4), 40);
    Raster::circle(cx + ex, ey, 9 * s, c565(0xB9853A), 200);
    Raster::circle(cx + ex, ey, 7.5f * s, c565(0xF4EEE4), 40);
    Raster::line(cx - ex + 8 * s, ey, cx + ex - 8 * s, ey, 1.5f * s, c565(0xB9853A));
  }
  // nose
  Raster::line(cx + 1 * s, ey + 3 * s, cx - 2 * s, hy + hh * 0.3f, 1.6f * s, skinD);
  Raster::line(cx - 2 * s, hy + hh * 0.3f, cx + 3 * s, hy + hh * 0.32f, 1.6f * s, skinD);
  // cheeks
  if (L.age < 3) {
    Raster::circle(cx - hw * 0.55f, hy + hh * 0.3f, 6 * s, c565(0xE88A7A), 50);
    Raster::circle(cx + hw * 0.55f, hy + hh * 0.3f, 6 * s, c565(0xE88A7A), 50);
  } else {
    Raster::line(cx - hw * 0.5f, hy + hh * 0.15f, cx - hw * 0.35f, hy + hh * 0.5f, 1.2f * s, skinD);
    Raster::line(cx + hw * 0.5f, hy + hh * 0.15f, cx + hw * 0.35f, hy + hh * 0.5f, 1.2f * s, skinD);
  }
  // mouth
  float my = hy + hh * 0.52f;
  uint16_t lip = c565(0x9A4A44);
  switch (mood) {
    case 1: // smile
      Raster::line(cx - 8 * s, my - 2 * s, cx, my + 2 * s, 2 * s, lip);
      Raster::line(cx, my + 2 * s, cx + 8 * s, my - 2 * s, 2 * s, lip);
      break;
    case 3: case 5: // sad / worried
      Raster::line(cx - 7 * s, my + 2 * s, cx, my - 1 * s, 2 * s, lip);
      Raster::line(cx, my - 1 * s, cx + 7 * s, my + 2 * s, 2 * s, lip);
      break;
    case 4: // a sly half-smile
      Raster::line(cx - 7 * s, my, cx + 3 * s, my, 2 * s, lip);
      Raster::line(cx + 3 * s, my, cx + 9 * s, my - 4 * s, 2 * s, lip);
      break;
    default: Raster::line(cx - 7 * s, my, cx + 7 * s, my, 2 * s, lip); break;
  }
  // beard / moustache
  if (L.beard == 1) {
    Raster::ellipse(cx - 6 * s, my - 4 * s, 7 * s, 3 * s, hair);
    Raster::ellipse(cx + 6 * s, my - 4 * s, 7 * s, 3 * s, hair);
  } else if (L.beard == 2) {
    Raster::ellipse(cx, hy + hh * 0.72f, hw * 0.9f, hh * 0.5f, hair);
    Raster::ellipse(cx, my, 7 * s, 2.5f * s, lip);
  }
  // hats
  float top = hy - hh * 0.95f;
  switch (L.hat) {
    case HAT_TOP:
      Raster::roundRect(cx - hw * 1.35f, top + 4 * s, hw * 2.7f, 6 * s, 3 * s, c565(0x141418));
      Raster::roundRect(cx - hw * 0.85f, top - 34 * s, hw * 1.7f, 40 * s, 4 * s, c565(0x18181C));
      Raster::rect(cx - hw * 0.85f, top - 4 * s, hw * 1.7f, 4 * s, acc);
      break;
    case HAT_CAP:
      Raster::ellipse(cx, top + 6 * s, hw * 1.1f, 12 * s, mix565(coat, 0, 60));
      Raster::ellipse(cx - 6 * s, top + 14 * s, hw * 0.95f, 5 * s, c565(0x1E1E22));
      break;
    case HAT_HELMET:
      Raster::ellipse(cx, top - 6 * s, hw * 1.08f, 28 * s, c565(0x1A2234));
      Raster::circle(cx, top - 30 * s, 4 * s, c565(0xB9853A));
      Raster::circle(cx, top - 2 * s, 6 * s, c565(0xD8B040));
      Raster::rect(cx - hw * 1.1f, top + 14 * s, hw * 2.2f, 4 * s, c565(0x141A28));
      break;
    case HAT_BONNET: {
      // a ruffled brim framing the face, drawn as an arc of puffs
      for (int i = 0; i <= 8; i++) {
        float a = PI_F * (1.05f + i * 0.1125f);
        Raster::circle(cx + cosf(a) * hw * 1.12f, hy - 4 * s + sinf(a) * hh * 1.0f, 8 * s, acc);
      }
      poly3(cx - hw * 0.4f, hy + hh * 0.75f, cx - hw * 0.2f, hy + hh * 1.15f, cx - hw * 0.6f, hy + hh * 1.1f, acc);
      break;
    }
    case HAT_CAPTAIN:
      Raster::roundRect(cx - hw * 1.1f, top - 6 * s, hw * 2.2f, 18 * s, 6 * s, c565(0x1A1E2A));
      Raster::rect(cx - hw * 1.1f, top - 6 * s, hw * 2.2f, 5 * s, c565(0xEFEFEF));
      Raster::ellipse(cx, top + 13 * s, hw * 1.05f, 5 * s, c565(0x101010));
      Raster::circle(cx, top + 4 * s, 4 * s, c565(0xD8B040));
      break;
    case HAT_HOOD:
      Raster::ellipse(cx, hy - 4 * s, hw * 1.45f, hh * 1.3f, coat, 240);
      Raster::ellipse(cx, hy + 2 * s, hw * 0.95f, hh * 0.95f, c565(0xE8E0D0));
      Raster::ellipse(cx - ex, ey, 6 * s, 3 * s, 0x0000);
      Raster::ellipse(cx + ex, ey, 6 * s, 3 * s, 0x0000);
      break;
    case HAT_BUN: Raster::circle(cx, top - 4 * s, 10 * s, hair); break;
    default: break;
  }
  if (look == L_VANEMASK) {
    // the clay mask pushed up on the brow, cuneiform pressed into it
    Raster::ellipse(cx, top + 2 * s, hw * 0.95f, 14 * s, c565(0xE8E0D0));
    for (int i = 0; i < 5; i++) poly3(cx - 14 * s + i * 7 * s, top - 4 * s, cx - 10 * s + i * 7 * s, top - 4 * s, cx - 12 * s + i * 7 * s, top + 4 * s, c565(0x8A7A6A));
  }
}

} // namespace Gaslight
