#include <pebble.h>
#include "config.h"

// Valeurs par défaut si config.h ne les définit pas (français, date JJ-MM).
// Ce ne sont que les valeurs de départ : la page de réglages du téléphone les
// remplace (s_lang, s_date_mdy), et la montre mémorise le choix.
#ifndef CFG_LANGUAGE
#define CFG_LANGUAGE          0
#endif
#ifndef CFG_DATE_MDY
#define CFG_DATE_MDY          0
#endif
#ifndef CFG_TIME_12H
#define CFG_TIME_12H          0
#endif
#ifndef CFG_FONT_STYLE
#define CFG_FONT_STYLE        0
#endif
#ifndef CFG_GHOST_LEVEL
#define CFG_GHOST_LEVEL       0
#endif
#ifndef CFG_ICON_COLOR
#define CFG_ICON_COLOR        1
#endif
#ifndef CFG_COMPLICATION
#define CFG_COMPLICATION      0
#endif
#ifndef CFG_DIST_MILES
#define CFG_DIST_MILES        0
#endif
#ifndef CFG_RED_RING_LATERAL
#define CFG_RED_RING_LATERAL  0
#endif
// Complications des deux rangées de prévisions (0 = météo horaire, 1 = FC + distance,
// 2 = calories + pas, 3 = lever + coucher du soleil, 4 = tendance 3 jours, 5..8 = autres paires FC / distance / calories / pas)
#ifndef CFG_COMP_TOP
#define CFG_COMP_TOP          0
#endif
#ifndef CFG_COMP_BOTTOM
#define CFG_COMP_BOTTOM       0
#endif
// Calories : 0 = actives seulement, 1 = actives + au repos
#ifndef CFG_CALORIES_TOTAL
#define CFG_CALORIES_TOTAL    0
#endif
// Couleurs des icônes des complications (mode « icônes en couleurs »)
#ifndef CFG_COLOR_ICON_HEART
#define CFG_COLOR_ICON_HEART  0xFF0000
#endif
#ifndef CFG_COLOR_ICON_FLAME
#define CFG_COLOR_ICON_FLAME  0xFF5500
#endif
#ifndef CFG_COLOR_ICON_STEPS
#define CFG_COLOR_ICON_STEPS  0x55FF55
#endif
#ifndef CFG_COLOR_ICON_PIN
#define CFG_COLOR_ICON_PIN    0xFFAA00
#endif

// ── Clés AppMessage (téléphone ↔ montre) ─────────────────────────────────
// Les noms sont déclarés dans messageKeys de package.json ; le SDK en fait les
// constantes MESSAGE_KEY_xxx (numéros attribués automatiquement).
//   FORECAST_0 … FORECAST_9  colonnes de prévisions (voir ci-dessous)
//   FORECAST_TS              heure (s Unix) du calcul des prévisions
//   REQUEST_WEATHER          montre → téléphone : rafraîchir maintenant
//   WEATHER_TREND            texte « VILLE : TENDANCE » (téléphone → montre)
//   LANGUAGE, DATE_FORMAT, TIME_FORMAT, ICON_STYLE, GHOST_LEVEL, FONT_STYLE, COMPLICATION, DIST_UNIT, RED_RING_LATERAL
//                            réglages de la page Clay (téléphone → montre)
//   SUNRISE_0..2, SUNSET_0..2  lever / coucher du soleil (s Unix) des 3 prochains jours
//   COMP_TOP, COMP_BOTTOM    complications des rangées de prévisions du haut / du bas
//   DAILY_0..5               tendance du jour J … J+5 : bits 0-6 code météo WMO (127 = inconnu),
//                            8-15 température max + 128, 16-23 min + 128, 24-28 jour du mois, 29-31 jour de semaine (0 = dimanche)
//   TEMP_UNIT, WEATHER_PERIOD  réglages Clay utilisés seulement par le téléphone
// ATTENTION : dans le SDK, les MESSAGE_KEY_xxx sont des variables (extern), pas des
// constantes : on ne peut pas les mettre dans un initialiseur statique. Le tableau
// est donc rempli au démarrage par init_forecast_keys().
#define FORECAST_KEY_COUNT 10
static uint32_t FORECAST_KEYS[FORECAST_KEY_COUNT];

// Clés lever / coucher du soleil (même raison : variables, remplies au démarrage)
static uint32_t SUNRISE_KEYS[3], SUNSET_KEYS[3];
// Tendance sur 3 ou 6 jours (DAILY_0..5) : même raison
static uint32_t DAILY_KEYS[6];

static void init_forecast_keys(void) {
  SUNRISE_KEYS[0] = MESSAGE_KEY_SUNRISE_0; SUNSET_KEYS[0] = MESSAGE_KEY_SUNSET_0;
  SUNRISE_KEYS[1] = MESSAGE_KEY_SUNRISE_1; SUNSET_KEYS[1] = MESSAGE_KEY_SUNSET_1;
  SUNRISE_KEYS[2] = MESSAGE_KEY_SUNRISE_2; SUNSET_KEYS[2] = MESSAGE_KEY_SUNSET_2;
  DAILY_KEYS[0] = MESSAGE_KEY_DAILY_0;
  DAILY_KEYS[1] = MESSAGE_KEY_DAILY_1;
  DAILY_KEYS[2] = MESSAGE_KEY_DAILY_2;
  DAILY_KEYS[3] = MESSAGE_KEY_DAILY_3;
  DAILY_KEYS[4] = MESSAGE_KEY_DAILY_4;
  DAILY_KEYS[5] = MESSAGE_KEY_DAILY_5;
  FORECAST_KEYS[0] = MESSAGE_KEY_FORECAST_0;
  FORECAST_KEYS[1] = MESSAGE_KEY_FORECAST_1;
  FORECAST_KEYS[2] = MESSAGE_KEY_FORECAST_2;
  FORECAST_KEYS[3] = MESSAGE_KEY_FORECAST_3;
  FORECAST_KEYS[4] = MESSAGE_KEY_FORECAST_4;
  FORECAST_KEYS[5] = MESSAGE_KEY_FORECAST_5;
  FORECAST_KEYS[6] = MESSAGE_KEY_FORECAST_6;
  FORECAST_KEYS[7] = MESSAGE_KEY_FORECAST_7;
  FORECAST_KEYS[8] = MESSAGE_KEY_FORECAST_8;
  FORECAST_KEYS[9] = MESSAGE_KEY_FORECAST_9;
}

// Clés de stockage persistant des réglages reçus
#define PERSIST_LANG      1
#define PERSIST_DATE_MDY  2
#define PERSIST_COMP      3
#define PERSIST_TIME12    5
#define PERSIST_ICON_COL  6
#define PERSIST_GHOST     7
#define PERSIST_FONT_ST   8
#define PERSIST_DIST_MI   4
#define PERSIST_RED_RING  9
#define PERSIST_COMP_TOP  10
#define PERSIST_COMP_BOT  11

// ── Prévisions horaires : 2 lignes de 5 colonnes (H+1 … H+10) ────────────
// Entier reçu : bits 0-6 code météo WMO (127 = inconnu), bit 7 jour,
// bits 8-15 température + 128, bits 16-23 heure locale du créneau (0-23).
// Les créneaux sont les heures pleines qui suivent l'heure courante
// (à 13h42 : 14h, 15h, … 23h). Ligne du haut = colonnes 0..4, ligne du
// bas = colonnes 5..9 (voir index.js). Le texte de tendance arrive à part.
#define FC_PER_ROW    5
#define FC_ROWS       2
#define FC_COLS       (FC_PER_ROW * FC_ROWS)
#define FC_UNKNOWN    0x7F
#define FC_MAX_AGE_S  (3 * 3600)   // au-delà, les prévisions sont jugées périmées

// Décalages internes d'une cellule de prévision (identiques pour les deux
// lignes, qui font toutes deux ≈ 32 px de haut sur emery), réglables ici.
#define FC_LBL_DY     (-1)         // libellé « 14H » par rapport au haut de la cellule
#define FC_ICO_DY     10           // haut de l'icône par rapport au haut de la cellule
#define FC_SEP_Y0     5            // séparateurs verticaux : début / fin dans la cellule
#define FC_SEP_Y1     26
#define FC_ICON_UP    3            // segments éteints (glyphe '0') remontés de 3 px ; ne concerne pas les logos 16×16
#define FC_ICON16_DY  4            // logos 16×16 : décalage vertical par rapport à y_ico (FC_ICON_UP n'est pas appliqué)

typedef struct {
  int  code;    // code météo WMO
  bool day;     // true = jour
  int  temp;    // température dans l'unité choisie dans pkjs/config.js
  int  hour;    // heure locale du créneau (0-23)
} Forecast;

// ── State ─────────────────────────────────────────────────────────────────
static Window   *s_window;
static Layer    *s_canvas;

// Libellés (réglés dans config.h ; déclarés comme tableaux de char, ce qui
// permet à strrchr() / aux différences de pointeurs de travailler dessus)
static const char s_label_tl[]  = CFG_LABEL_TOP_LEFT;
static const char s_label_tr[]  = CFG_LABEL_TOP_RIGHT;

// Texte central de la ligne du haut, entre les deux libellés (jaune, comme la
// tendance météo). À régler dans config.h avec CFG_LABEL_CENTER ; vide = rien.
#ifndef CFG_LABEL_CENTER
#define CFG_LABEL_CENTER ""
#endif
static const char s_label_tc[]  = CFG_LABEL_CENTER;

static Forecast s_fc[FC_COLS];
static time_t   s_fc_ts       = 0;   // 0 = aucune prévision reçue
static time_t   s_last_req_ts = 0;   // dernière demande de rafraîchissement

// Réglages d'affichage (défauts de config.h, remplacés par la page Clay)
static int  s_lang     = CFG_LANGUAGE;   // 0 = français, 1 = English
static int  s_date_mdy = CFG_DATE_MDY;   // 0 = JJ-MM, 1 = MM-JJ
static int  s_time12   = CFG_TIME_12H;    // 0 = 24 h, 1 = 12 h (indicateur A/P)
static int  s_font_style   = CFG_FONT_STYLE;   // 0..3 = 7 segments (Classic, Classic Mini, Modern, Modern Mini), 4..7 = 14 segments (mêmes)
static int  s_ghost    = CFG_GHOST_LEVEL; // segments éteints : 0 = normal, 1 = léger, 2 = désactivés
#define GHOST_ON (s_ghost != 2)
static int  s_icon_col = CFG_ICON_COLOR; // 0 = icônes monochromes, 1 = icônes en couleurs
static int  s_comp     = CFG_COMPLICATION; // 0 pas, 1 FC, 2 distance, 3 soleil, 4 secondes
static int  s_dist_mi  = CFG_DIST_MILES; // 0 = kilomètres, 1 = miles
static int  r_r_sides  = CFG_RED_RING_LATERAL; // 0 masqué, 1 affiché
static int  s_comp_top = CFG_COMP_TOP;      // rangée de prévisions du haut : 0 météo horaire, 1 FC + distance, 2 calories + pas, 3 soleil, 4 tendance 3 jours
static int  s_comp_bot = CFG_COMP_BOTTOM;   // rangée du bas (mêmes choix)

static char s_weather_trend[64] = "METEO"; // « VILLE : TENDANCE » reçu du téléphone

static int  s_steps           = -1;   // -1 = indisponible (affiche -----)
static int  s_hr              = 0;   // fréquence cardiaque (0 = pas de mesure)
static int  s_dist            = -1;  // distance du jour en mètres (-1 = indisponible)
static int  s_cal             = -1;  // calories du jour (-1 = indisponible)

// Tendance des 6 prochains jours (envoyée par le téléphone ; 3 colonnes par rangée)
typedef struct {
  int code;   // code météo WMO du jour (FC_UNKNOWN = pas de donnée)
  int tmax, tmin;
  int dom;    // jour du mois (1-31)
  int wd;     // jour de la semaine (0 = dimanche)
} DailyFc;
static DailyFc s_daily[6];
static time_t s_sunrise[3];          // levers du soleil (s Unix, 0 = inconnu)
static time_t s_sunset[3];           // couchers du soleil
static int  s_batt_pct        = 100;

// ── Custom fonts (loaded in window_load) ──────────────────────────────────
static GFont s_font_d14_time   = NULL;  // DSEG7 (style choisi) Bold 48 px : heure HH:MM
static GFont s_font_d7_date    = NULL;  // DSEG7 20 px (gras ou normal) : date ET chiffres allumés de la complication
static GFont s_font_d7_comp    = NULL;  // = s_font_d7_date (même police, même objet)
static GFont s_font_comp_reg   = NULL;  // DSEG7 Regular 20 px : chiffres éteints (fantômes) de la complication
                                        // (= s_font_d7_date quand la graisse « normal » est choisie)
// Déclaration anticipée : appelée par inbox_received(), définie plus bas (avant window_load)
static void load_dseg_fonts(void);

static GFont s_font_weather    = NULL;  // DSEGWeather 20 px : seulement le glyphe '0' (segments éteints derrière les logos)

// ── Semaine ISO 8601 (lundi = 1er jour, semaine 1 = celle du 1er jeudi) ──
static int iso_p(int y) { return (y + y / 4 - y / 100 + y / 400) % 7; }
static int iso_weeks_in_year(int y) { return 52 + ((iso_p(y) == 4 || iso_p(y - 1) == 3) ? 1 : 0); }

static void iso_week(const struct tm *t, int *iso_year, int *iso_wk) {
  int y   = t->tm_year + 1900;
  int wd  = (t->tm_wday + 6) % 7 + 1;          // lundi = 1 … dimanche = 7
  int wk  = (t->tm_yday + 1 - wd + 10) / 7;    // tm_yday : 0-365
  if (wk < 1) { y--; wk = iso_weeks_in_year(y); }
  else if (wk > iso_weeks_in_year(y)) { y++; wk = 1; }
  *iso_year = y;
  *iso_wk   = wk;
}

// ── Helpers ───────────────────────────────────────────────────────────────
static GColor color_from_int(int v) {
  return GColorFromRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ── Prévisions : validité et choix de l'icône ────────────────────────────
static bool fc_fresh(void) {
#if CFG_ICON_TEST
  return true;
#endif
  return s_fc_ts > 0 && (time(NULL) - s_fc_ts) <= FC_MAX_AGE_S;
}

static bool fc_slot_valid(int i) {
  return fc_fresh() && s_fc[i].code != FC_UNKNOWN;
}

// Ghost segments behind real value – simulates unlit LCD segments.
// Désactivé quand le niveau de ghost est « off » (GHOST_ON).
// DSEG text must never use TrailingEllipsis: the fonts have no '…' glyph and
// firmware 4.9 hangs (watchdog / frozen emulator) when it has to ellipsize.
// Petite lettre « A », « P » (indicateur matin / après-midi du format 12 h),
// « K » ou « M » (unité de la complication distance) : matrice 3×5, points de
// 2×2 px, soit 6×10 px.
static void draw_ind_letter(GContext *ctx, int x, int y, char ch, GColor color) {
  static const uint8_t GP[5] = {7, 5, 7, 4, 4};   // P : 111 101 111 100 100
  static const uint8_t GA[5] = {2, 5, 7, 5, 5};   // A : 010 101 111 101 101
  static const uint8_t GK[5] = {5, 5, 6, 5, 5};   // K : 101 101 110 101 101
  static const uint8_t GM[5] = {5, 7, 7, 5, 5};   // M : 101 111 111 101 101
  const uint8_t *g = GA;
  switch (ch) {
    case 'P': g = GP; break;
    case 'K': g = GK; break;
    case 'M': g = GM; break;
    default:  break;
  }
  graphics_context_set_fill_color(ctx, color);
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (g[row] & (4 >> col)) {
        graphics_fill_rect(ctx, GRect(x + col * 2, y + row * 2, 2, 2), 0, GCornerNone);
      }
    }
  }
}

// Logo de battement cardiaque : tracé d'électrocardiogramme (onde P, pic R, onde T)
// dans le rectangle (x, y, w, h), trait de 2 px.
static void draw_ecg(GContext *ctx, int x, int y, int w, int h, GColor color) {
  // Points sur une grille de 30 × 13 (la ligne de base est à y = 8)
  static const int8_t EX[12] = {0, 5, 7, 9, 12, 14, 17, 19, 22, 24, 27, 30};
  static const int8_t EY[12] = {8, 8, 5, 8,  8,  0, 13,  8,  8,  5,  8,  8};
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  GPoint prev = GPoint(x + EX[0] * w / 30, y + EY[0] * (h - 1) / 13);
  for (int i = 1; i < 12; i++) {
    GPoint p = GPoint(x + EX[i] * w / 30, y + EY[i] * (h - 1) / 13);
    graphics_draw_line(ctx, prev, p);
    prev = p;
  }
  graphics_context_set_stroke_width(ctx, 1);
}

// Grande lettre « K » ou « M » (matrice 5×7, points de 2×2 px, soit 10×14 px),
// pour l'unité de la complication distance (taille d'un chiffre).
static void draw_big_letter(GContext *ctx, int x, int y, char ch, GColor color) {
  static const uint8_t GK[7] = {17, 18, 20, 24, 20, 18, 17};   // K : 10001 10010 10100 11000 10100 10010 10001
  static const uint8_t GM[7] = {17, 27, 21, 17, 17, 17, 17};   // M : 10001 11011 10101 10001 10001 10001 10001
  const uint8_t *g = (ch == 'M') ? GM : GK;
  graphics_context_set_fill_color(ctx, color);
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 5; col++) {
      if (g[row] & (16 >> col)) {
        graphics_fill_rect(ctx, GRect(x + col * 2, y + row * 2, 2, 2), 0, GCornerNone);
      }
    }
  }
}

// Caractère « tous les segments allumés » des polices 14 segments (DSEG14) : le
// chiffre « 8 » n'y allume pas les segments diagonaux ni le milieu vertical. Si
// l'affichage ne montre rien à la place des segments éteints, mettre '8' ici.
#define GHOST14_CHAR '~'

// Texte des segments éteints : les « 8 » sont remplacés par GHOST14_CHAR avec les
// polices 14 segments (styles 4 à 7) ; sans effet avec les polices 7 segments.
static void ghost_text(const char *in, char *out, size_t n) {
  size_t i = 0;
  for (; in[i] && i + 1 < n; i++) {
    out[i] = (in[i] == '8' && s_font_style >= 4) ? GHOST14_CHAR : in[i];
  }
  out[i] = '\0';
}

static void lcd_text(GContext *ctx, const char *ghost, const char *real,
                     GFont font, GRect r, GColor gc, GColor rc,
                     GTextAlignment align) {
  if (GHOST_ON) {
    graphics_context_set_text_color(ctx, gc);
    char gbuf[16];
    ghost_text(ghost, gbuf, sizeof(gbuf));
    graphics_draw_text(ctx, gbuf, font, r,
                       GTextOverflowModeFill, align, NULL);
  }
  graphics_context_set_text_color(ctx, rc);
  graphics_draw_text(ctx, real, font, r,
                     GTextOverflowModeFill, align, NULL);
}

// ── Battery callback (accurate, immediate updates) ───────────────────────
static void battery_state_handler(BatteryChargeState state) {
  s_batt_pct = (int)state.charge_percent;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Bandeau météo : ville + tendance, largeur calculée séparément ─────────
// Largeur d'un texte sur UNE seule ligne. La boîte de mesure est volontairement
// très large : avec une boîte étroite, le SDK replie le texte sur plusieurs
// lignes et renvoie une largeur <= à la boîte, ce qui masque tout dépassement.
static int trend_text_w(const char *s, GFont font) {
  GSize sz = graphics_text_layout_get_content_size(
      s, font, GRect(0, 0, 2000, 100),
      GTextOverflowModeFill, GTextAlignmentLeft);
  return sz.w;
}

// Traduit le mot de tendance envoyé par le téléphone (toujours en français,
// éventuellement suivi d'un pourcentage : « PLUIE 8% ») selon la langue choisie (s_lang).
// Sans effet en français ; mot inconnu = laissé tel quel.
static void trend_localize(const char *in, char *out, size_t n) {
  static const struct { const char *fr, *en; } T[] = {
    {"ENSOLEILLE", "SUNNY"}, {"NUAGEUX", "CLOUDY"}, {"BROUILLARD", "FOG"},
    {"PLUIE", "RAIN"}, {"NEIGE", "SNOW"}, {"ORAGE", "STORM"}, {"METEO", "WEATHER"}
  };
  strncpy(out, in, n - 1);
  out[n - 1] = '\0';
  if (s_lang != 1) return;   // français : texte inchangé
  for (unsigned k = 0; k < sizeof(T) / sizeof(T[0]); k++) {
    size_t l = strlen(T[k].fr);
    if (strncmp(in, T[k].fr, l) == 0 && (in[l] == '\0' || in[l] == ' ')) {
      snprintf(out, n, "%s%s", T[k].en, in + l);
      break;
    }
  }
}

static void draw_weather_trend(GContext *ctx, GFont font, int y, int W, int H) {
  char city[48];
  char trend[48];

  // Marge de 3 px de chaque côté ; la largeur utilisable retire 3 marges
  // (soit 3 px de sécurité en plus des deux côtés)
  int margin = 3;
  int max_w = W - (3 * margin);

  // ------------------------------------------------------------
  // Récupération de "VILLE : TENDANCE"
  // ------------------------------------------------------------
  char source[64];

  strncpy(source, s_weather_trend, sizeof(source) - 1);
  source[sizeof(source) - 1] = '\0';

  char *sep = strstr(source, " : ");

  if (!sep) {
    char loc0[64];
    trend_localize(source, loc0, sizeof(loc0));
    graphics_draw_text(ctx, loc0, font,
                       GRect(margin, y, max_w, H - y),
                       GTextOverflowModeFill,
                       GTextAlignmentCenter,
                       NULL);
    return;
  }

  // Ville
  size_t city_len = (size_t)(sep - source);
  if (city_len >= sizeof(city)) {
    city_len = sizeof(city) - 1;
  }
  memcpy(city, source, city_len);
  city[city_len] = '\0';

  // Tendance
  strncpy(trend, sep + 3, sizeof(trend) - 1);
  trend[sizeof(trend) - 1] = '\0';
  {
    char loc[48];
    trend_localize(trend, loc, sizeof(loc));
    strcpy(trend, loc);
  }

  // ------------------------------------------------------------
  // Mesures (une seule ligne)
  // ------------------------------------------------------------
  char trend_text[52];
  snprintf(trend_text, sizeof(trend_text), ": %s", trend);

  int trend_w = trend_text_w(trend_text, font);
  int space_w = trend_text_w(" ", font);

  // La tendance est PRIORITAIRE : tout le reste est pour la ville.
  int city_max_w = max_w - trend_w - space_w;
  if (city_max_w < 1) {
    city_max_w = 1;
  }

  // ------------------------------------------------------------
  // Ville trop large : on garde le maximum de caractères
  // suivis d'UN SEUL "." final.
  // ------------------------------------------------------------
  if (trend_text_w(city, font) > city_max_w) {
    size_t original_len = strlen(city);
    bool found = false;

    for (size_t keep = original_len - 1; keep > 0; keep--) {
      char candidate[48];

      if (keep >= sizeof(candidate) - 2) {
        continue;
      }
      // Ne pas couper au milieu d'un caractère UTF-8
      if (((unsigned char)city[keep] & 0xC0) == 0x80) {
        continue;
      }
      // Ne pas finir par "-." ou " ." : on recule jusqu'à une lettre
      size_t k = keep;
      while (k > 0 && (city[k - 1] == '-' || city[k - 1] == ' ')) {
        k--;
      }
      if (k == 0) {
        continue;
      }

      memcpy(candidate, city, k);
      candidate[k] = '.';
      candidate[k + 1] = '\0';

      if (trend_text_w(candidate, font) <= city_max_w) {
        strcpy(city, candidate);
        found = true;
        break;
      }
    }

    // Sécurité : même "X." ne tient pas
    if (!found) {
      city[0] = '.';
      city[1] = '\0';
    }
  }

  int city_w = trend_text_w(city, font);

  // ------------------------------------------------------------
  // Largeur totale réelle, bloc centré
  // ------------------------------------------------------------
  int total_w = city_w + space_w + trend_w;
  if (total_w > max_w) {
    total_w = max_w;
  }
  int x = (W - total_w) / 2;

  // 1. Ville
  graphics_context_set_text_color(ctx, GColorYellow);
  graphics_draw_text(ctx, city, font,
                     GRect(x, y, city_w + 2, H - y),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  // 2. Tendance météo
  graphics_draw_text(ctx, trend_text, font,
                     GRect(x + city_w + space_w, y, trend_w + 2, H - y),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

// ── DRAW ──────────────────────────────────────────────────────────────────
//
// La disposition suit la Casio « TIME 2 / HEART RATE MONITOR » d'origine, en
// pixels emery (200×228 ; PX/PY adaptent les valeurs aux autres tailles) :
//    0..16   CASIO (gauche) / TIME 2 (droite, suffixe bleu) + texte central jaune
//            – libellés modifiables dans config.h
//   16..210  anneau rouge (bande haute, fins rails latéraux, bande basse)
//   19..52   prévisions H+1 … H+5 (5 colonnes : libellé · logo 16×16 · température)
//   52..173  LCD : cadre extérieur blanc, panneau (CFG_COLOR_BG)
//     57..87   date (DSEG7 20) | case de complication (pas, DSEG7 20, bord arrondi)
//     86..144  HH:MM (DSEG7 48) ; en 12 h, indicateur P / A à gauche
//    144..156  bandeau d'info : pile | 7 carrés des jours
//    156..173  initiales des jours + année/semaine ISO sur la bande sombre
//   175..207  prévisions H+6 … H+10 (même hauteur de colonnes que la ligne du haut)
//   207..228  ville + tendance météo (texte jaune sur le boîtier noir)

// ── Logos météo 16×16 (pixel par pixel, comme la lune) ───────────────────
// Un uint16_t par ligne, bit 15 = colonne de gauche. Le « # » du commentaire
// est un pixel allumé : on peut retoucher un logo en modifiant le dessin,
// puis en recalculant la valeur hexa de la ligne (# = 1, . = 0).
#define WI_W 16
#define WI_H 16


// sun
static const uint16_t WI_SUN[WI_H] = {
  0x0180,  // .......##.......
  0x0180,  // .......##.......
  0x2004,  // ..#..........#..
  0x1008,  // ...#........#...
  0x03C0,  // ......####......
  0x07E0,  // .....######.....
  0x0FF0,  // ....########....
  0xCFF3,  // ##..########..##
  0xCFF3,  // ##..########..##
  0x0FF0,  // ....########....
  0x07E0,  // .....######.....
  0x03C0,  // ......####......
  0x1008,  // ...#........#...
  0x2004,  // ..#..........#..
  0x0180,  // .......##.......
  0x0180,  // .......##.......
};

// moon
static const uint16_t WI_MOON[WI_H] = {
  0x0000,  // ................
  0x0FE0,  // ....#######.....
  0x1F80,  // ...######.......
  0x3E00,  // ..#####.........
  0x7E00,  // .######.........
  0x7C00,  // .#####..........
  0xFC00,  // ######..........
  0xF800,  // #####...........
  0xF800,  // #####...........
  0xFC00,  // ######..........
  0x7C00,  // .#####..........
  0x7E00,  // .######.........
  0x3E00,  // ..#####.........
  0x1F80,  // ...######.......
  0x0FE0,  // ....#######.....
  0x0000,  // ................
};

// cloud
static const uint16_t WI_CLOUD[WI_H] = {
  0x0000,  // ................
  0x0000,  // ................
  0x0180,  // .......##.......
  0x07E0,  // .....######.....
  0x0FF0,  // ....########....
  0x0FF0,  // ....########....
  0x1FF0,  // ...#########....
  0x1FFC,  // ...###########..
  0x3FFE,  // ..#############.
  0x7FFE,  // .##############.
  0x7FFE,  // .##############.
  0x7FFE,  // .##############.
  0x3FFE,  // ..#############.
  0x1FF8,  // ...##########...
  0x0000,  // ................
  0x0000,  // ................
};

// sun + nuage (jour)
static const uint16_t WI_SUNCLOUD[WI_H] = {
  0x0400,  // .....#..........
  0x4440,  // .#...#...#......
  0x2080,  // ..#.....#.......
  0x0E00,  // ....###.........
  0x18E0,  // ...##...###.....
  0xDBF0,  // ##.##.######....
  0x1BF8,  // ...##.#######...
  0x03F8,  // ......#######...
  0x27FE,  // ..#..##########.
  0x4FFE,  // .#..###########.
  0x0FFE,  // ....###########.
  0x0FFE,  // ....###########.
  0x07FC,  // .....#########..
  0x07FC,  // .....#########..
  0x0000,  // ................
  0x0000,  // ................
};

// lune + nuage (nuit)
static const uint16_t WI_MOONCLOUD[WI_H] = {
  0x0000,  // ................
  0x1E00,  // ...####.........
  0x3C00,  // ..####..........
  0x7800,  // .####...........
  0x78E0,  // .####...###.....
  0x7BF0,  // .####.######....
  0x7BF8,  // .####.#######...
  0x73F8,  // .###..#######...
  0x67FE,  // .##..##########.
  0x6FFE,  // .##.###########.
  0x2FFE,  // ..#.###########.
  0x0FFE,  // ....###########.
  0x07FC,  // .....#########..
  0x07FC,  // .....#########..
  0x0000,  // ................
  0x0000,  // ................
};

// goutte (pluie)
static const uint16_t WI_DROP[WI_H] = {
  0x0000,  // ................
  0x0010,  // ...........#....
  0x0030,  // ..........##....
  0x0038,  // ..........###...
  0x0078,  // .........####...
  0x007C,  // .........#####..
  0x18FC,  // ...##...######..
  0x18FE,  // ...##...#######.
  0x3CFE,  // ..####..#######.
  0x3EFE,  // ..#####.#######.
  0x7E7C,  // .######..#####..
  0x7E38,  // .######...###...
  0x7F00,  // .#######........
  0x7F00,  // .#######........
  0x7E00,  // .######.........
  0x3C00,  // ..####..........
};

// « 2 gouttes » (forte pluie) : pour l'instant dessin IDENTIQUE à WI_DROP (une seule goutte)
static const uint16_t WI_DROPS[WI_H] = {
  0x0000,  // ................
  0x0010,  // ...........#....
  0x0030,  // ..........##....
  0x0038,  // ..........###...
  0x0078,  // .........####...
  0x007C,  // .........#####..
  0x18FC,  // ...##...######..
  0x18FE,  // ...##...#######.
  0x3CFE,  // ..####..#######.
  0x3EFE,  // ..#####.#######.
  0x7E7C,  // .######..#####..
  0x7E38,  // .######...###...
  0x7F00,  // .#######........
  0x7F00,  // .#######........
  0x7E00,  // .######.........
  0x3C00,  // ..####..........
};

// flocon
static const uint16_t WI_SNOW[WI_H] = {
  0x0180,  // .......##.......
  0x03C0,  // ......####......
  0x0180,  // .......##.......
  0x1188,  // ...#...##...#...
  0x0990,  // ....#..##..#....
  0x05A0,  // .....#.##.#.....
  0x43C2,  // .#....####....#.
  0xFFFF,  // ################
  0xFFFF,  // ################
  0x43C2,  // .#....####....#.
  0x05A0,  // .....#.##.#.....
  0x0990,  // ....#..##..#....
  0x1188,  // ...#...##...#...
  0x0180,  // .......##.......
  0x03C0,  // ......####......
  0x0180,  // .......##.......
};

// éclair
static const uint16_t WI_BOLT[WI_H] = {
  0x01F8,  // .......######...
  0x03F0,  // ......######....
  0x03E0,  // ......#####.....
  0x07C0,  // .....#####......
  0x07C0,  // .....#####......
  0x0F80,  // ....#####.......
  0x0FFC,  // ....##########..
  0x1FF8,  // ...##########...
  0x1FF0,  // ...#########....
  0x00F0,  // ........####....
  0x01E0,  // .......####.....
  0x01C0,  // .......###......
  0x0380,  // ......###.......
  0x0300,  // ......##........
  0x0600,  // .....##.........
  0x0400,  // .....#..........
};

// petite goutte (une des 2 gouttes du logo pluie) + petit éclair
static const uint16_t WI_DROPBOLT[WI_H] = {
  0x0000,  // ................
  0x000E,  // ............###.
  0x001C,  // ...........###..
  0x0038,  // ..........###...
  0x0070,  // .........###....
  0x007E,  // .........######.
  0x183E,  // ...##.....#####.
  0x180E,  // ...##.......###.
  0x3C1C,  // ..####.....###..
  0x3E38,  // ..#####...###...
  0x7E30,  // .######...##....
  0x7E60,  // .######..##.....
  0x7F40,  // .#######.#......
  0x7F00,  // .#######........
  0x7E00,  // .######.........
  0x3C00,  // ..####..........
};

// brouillard
static const uint16_t WI_FOG[WI_H] = {
  0x0000,  // ................
  0x0000,  // ................
  0x3FFC,  // ..############..
  0x3FFC,  // ..############..
  0x0000,  // ................
  0x0000,  // ................
  0xFFF0,  // ############....
  0xFFF0,  // ############....
  0x0000,  // ................
  0x0000,  // ................
  0x0FFF,  // ....############
  0x0FFF,  // ....############
  0x0000,  // ................
  0x0000,  // ................
  0x7FF8,  // .############...
  0x7FF8,  // .############...
};


// ── Icônes en couleurs : calques ─────────────────────────────────────────
// Les logos ci-dessus servent au mode monochrome. En mode couleurs, chaque logo
// est dessiné en 1 ou 2 calques, chacun avec sa couleur (CFG_COLOR_ICON_*).
// Les calques ci-dessous séparent les logos mixtes en deux parties.

// couleur : partie nuage de WI_SUNCLOUD
static const uint16_t WIC_SUNCLOUD_CLOUD[WI_H] = {
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x00E0,  // ........###.....
  0x03F0,  // ......######....
  0x03F8,  // ......#######...
  0x03F8,  // ......#######...
  0x07FE,  // .....##########.
  0x0FFE,  // ....###########.
  0x0FFE,  // ....###########.
  0x0FFE,  // ....###########.
  0x07FC,  // .....#########..
  0x07FC,  // .....#########..
  0x0000,  // ................
  0x0000,  // ................
};

// couleur : partie soleil de WI_SUNCLOUD
static const uint16_t WIC_SUNCLOUD_SUN[WI_H] = {
  0x0400,  // .....#..........
  0x4440,  // .#...#...#......
  0x2080,  // ..#.....#.......
  0x0E00,  // ....###.........
  0x1800,  // ...##...........
  0xD800,  // ##.##...........
  0x1800,  // ...##...........
  0x0000,  // ................
  0x2000,  // ..#.............
  0x4000,  // .#..............
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
};

// couleur : partie nuage de WI_MOONCLOUD
static const uint16_t WIC_MOONCLOUD_CLOUD[WI_H] = {
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x00E0,  // ........###.....
  0x03F0,  // ......######....
  0x03F8,  // ......#######...
  0x03F8,  // ......#######...
  0x07FE,  // .....##########.
  0x0FFE,  // ....###########.
  0x0FFE,  // ....###########.
  0x0FFE,  // ....###########.
  0x07FC,  // .....#########..
  0x07FC,  // .....#########..
  0x0000,  // ................
  0x0000,  // ................
};

// couleur : partie lune de WI_MOONCLOUD
static const uint16_t WIC_MOONCLOUD_MOON[WI_H] = {
  0x0000,  // ................
  0x1E00,  // ...####.........
  0x3C00,  // ..####..........
  0x7800,  // .####...........
  0x7800,  // .####...........
  0x7800,  // .####...........
  0x7800,  // .####...........
  0x7000,  // .###............
  0x6000,  // .##.............
  0x6000,  // .##.............
  0x2000,  // ..#.............
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
};

// couleur : goutte de WI_DROPBOLT (bleu clair)
static const uint16_t WIC_DROPBOLT_DROP[WI_H] = {
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
  0x1800,  // ...##...........
  0x1800,  // ...##...........
  0x3C00,  // ..####..........
  0x3E00,  // ..#####.........
  0x7E00,  // .######.........
  0x7E00,  // .######.........
  0x7F00,  // .#######........
  0x7F00,  // .#######........
  0x7E00,  // .######.........
  0x3C00,  // ..####..........
};

// couleur : éclair de WI_DROPBOLT (jaune)
static const uint16_t WIC_DROPBOLT_BOLT[WI_H] = {
  0x0000,  // ................
  0x000E,  // ............###.
  0x001C,  // ...........###..
  0x0038,  // ..........###...
  0x0070,  // .........###....
  0x007E,  // .........######.
  0x003E,  // ..........#####.
  0x000E,  // ............###.
  0x001C,  // ...........###..
  0x0038,  // ..........###...
  0x0030,  // ..........##....
  0x0060,  // .........##.....
  0x0040,  // .........#......
  0x0000,  // ................
  0x0000,  // ................
  0x0000,  // ................
};

enum { WI_NONE = 0, WI_I_SUN, WI_I_MOON, WI_I_CLOUD, WI_I_SUNCLOUD, WI_I_MOONCLOUD,
       WI_I_DROP, WI_I_DROPS, WI_I_SNOW, WI_I_BOLT, WI_I_DROPBOLT, WI_I_FOG, WI_COUNT };

static const uint16_t *const WI_TABLE[WI_COUNT] = {
  NULL, WI_SUN, WI_MOON, WI_CLOUD, WI_SUNCLOUD, WI_MOONCLOUD,
  WI_DROP, WI_DROPS, WI_SNOW, WI_BOLT, WI_DROPBOLT, WI_FOG
};

// Familles de couleurs des icônes
enum { WC_SUN = 0, WC_MOON, WC_RAIN, WC_SNOW, WC_CLOUD, WC_BOLT, WC_COUNT };

static GColor wc_color(int c) {
  switch (c) {
    case WC_SUN:   return color_from_int(CFG_COLOR_ICON_SUN);
    case WC_MOON:  return color_from_int(CFG_COLOR_ICON_MOON);
    case WC_RAIN:  return color_from_int(CFG_COLOR_ICON_RAIN);
    case WC_SNOW:  return color_from_int(CFG_COLOR_ICON_SNOW);
    case WC_CLOUD: return color_from_int(CFG_COLOR_ICON_CLOUD);
    default:       return color_from_int(CFG_COLOR_ICON_BOLT);
  }
}

// Description couleur d'un logo : calque 1 (couleur c1), puis calque 2 (couleur c2,
// facultatif, dessiné par-dessus). Pour les logos « simples », le calque 1 est le
// logo lui-même.
typedef struct {
  const uint16_t *l1; uint8_t c1;
  const uint16_t *l2; uint8_t c2;
} WiColor;

static const WiColor WI_COLOR[WI_COUNT] = {
  { NULL, 0, NULL, 0 },
  { WI_SUN,               WC_SUN,   NULL,              0 },          // soleil
  { WI_MOON,              WC_MOON,  NULL,              0 },          // lune
  { WI_CLOUD,             WC_CLOUD, NULL,              0 },          // nuage
  { WIC_SUNCLOUD_CLOUD,   WC_CLOUD, WIC_SUNCLOUD_SUN,  WC_SUN },     // soleil + nuage
  { WIC_MOONCLOUD_CLOUD,  WC_CLOUD, WIC_MOONCLOUD_MOON, WC_MOON },   // lune + nuage
  { WI_DROP,              WC_RAIN,  NULL,              0 },          // pluie
  { WI_DROPS,             WC_RAIN,  NULL,              0 },          // forte pluie
  { WI_SNOW,              WC_SNOW,  NULL,              0 },          // neige
  { WI_BOLT,              WC_BOLT,  NULL,              0 },          // éclair
  { WIC_DROPBOLT_DROP,    WC_RAIN,  WIC_DROPBOLT_BOLT, WC_BOLT },    // petite goutte + petit éclair
  { WI_FOG,               WC_CLOUD, NULL,              0 }           // brouillard
};

// Dessine un calque 16×16 dont le coin haut-gauche est en (x, y).
// Les pixels contigus d'une ligne sont regroupés en un seul rectangle.
static void draw_bits(GContext *ctx, const uint16_t *bits, int x, int y, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  for (int j = 0; j < WI_H; j++) {
    int i = 0;
    while (i < WI_W) {
      if (bits[j] & (1 << (WI_W - 1 - i))) {
        int start = i;
        while (i < WI_W && (bits[j] & (1 << (WI_W - 1 - i)))) i++;
        graphics_fill_rect(ctx, GRect(x + start, y + j, i - start, 1), 0, GCornerNone);
      } else {
        i++;
      }
    }
  }
}

// Dessine un logo : monochrome (couleur col) ou, si s_icon_col, en couleurs.
static void draw_icon(GContext *ctx, int id, int x, int y, GColor col) {
  if (id <= WI_NONE || id >= WI_COUNT) return;
  if (!s_icon_col) {
    draw_bits(ctx, WI_TABLE[id], x, y, col);
    return;
  }
  const WiColor *wc = &WI_COLOR[id];
  draw_bits(ctx, wc->l1, x, y, wc_color(wc->c1));
  if (wc->l2) draw_bits(ctx, wc->l2, x, y, wc_color(wc->c2));
}

// Code météo WMO → logo 16×16 (voir WI_* plus haut)
static int weather_icon(int code, bool day) {
  switch (code) {
    case 0: case 1:                       return day ? WI_I_SUN : WI_I_MOON;
    case 2:                               return day ? WI_I_SUNCLOUD : WI_I_MOONCLOUD;
    case 3:                               return WI_I_CLOUD;
    case 45: case 48:                     return WI_I_FOG;
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 80:                     return WI_I_DROP;       // pluie faible
    case 63: case 65: case 66: case 67:
    case 81: case 82:                     return WI_I_DROPS;      // pluie forte
    case 71: case 73: case 75: case 77:
    case 85: case 86:                     return WI_I_SNOW;
    case 95:                              return WI_I_BOLT;       // orage
    case 96: case 99:                     return WI_I_DROPBOLT;   // orage + pluie / grêle
    default:                              return WI_I_CLOUD;
  }
}

// ── Complications des rangées de prévisions (hors météo horaire) ─────────
// Valeurs de COMP_TOP / COMP_BOTTOM (gardées compatibles avec la version précédente)
enum { CK_WEATHER = 0, CK_HR_DIST, CK_CAL_STEPS, CK_SUN, CK_TREND,
       CK_HR_CAL, CK_HR_STEPS, CK_DIST_CAL, CK_DIST_STEPS, CK_COUNT };
// Mesures affichables dans une demi-rangée
enum { M_HR = 0, M_DIST, M_CAL, M_STEPS };

// cœur (fréquence cardiaque)
static const uint16_t WI_HEART[WI_H] = {
  0x0000,  // ................
  0x0000,  // ................
  0x3C3C,  // ..####....####..
  0x7E7E,  // .######..######.
  0xFFFF,  // ################
  0xFFFF,  // ################
  0xFFFF,  // ################
  0xFFFF,  // ################
  0x7FFE,  // .##############.
  0x3FFC,  // ..############..
  0x1FF8,  // ...##########...
  0x0FF0,  // ....########....
  0x07E0,  // .....######.....
  0x03C0,  // ......####......
  0x0180,  // .......##.......
  0x0000,  // ................
};

// flamme (calories)
static const uint16_t WI_FLAME[WI_H] = {
  0x0100,  // .......#........
  0x0180,  // .......##.......
  0x01C0,  // .......###......
  0x03C0,  // ......####......
  0x07C0,  // .....#####......
  0x07E4,  // .....######..#..
  0x0FEC,  // ....#######.##..
  0x0FF6,  // ....########.##.
  0x1FF8,  // ...##########...
  0x1FFC,  // ...###########..
  0x1FFC,  // ...###########..
  0x1FFC,  // ...###########..
  0x0FF8,  // ....#########...
  0x07F0,  // .....#######....
  0x03E0,  // ......#####.....
  0x0000,  // ................
};

// pas (deux empreintes)
static const uint16_t WI_FEET[WI_H] = {
  0x0000,  // ................
  0x0078,  // .........####...
  0x00F8,  // ........#####...
  0x00F8,  // ........#####...
  0x00F8,  // ........#####...
  0x0078,  // .........####...
  0x0000,  // ................
  0x1E70,  // ...####..###....
  0x3E70,  // ..#####..###....
  0x3E00,  // ..#####.........
  0x3E00,  // ..#####.........
  0x1E00,  // ...####.........
  0x0000,  // ................
  0x1C00,  // ...###..........
  0x1C00,  // ...###..........
  0x0000,  // ................
};

// repère (distance)
static const uint16_t WI_PIN[WI_H] = {
  0x0000,  // ................
  0x07E0,  // .....######.....
  0x0FF0,  // ....########....
  0x1FF8,  // ...##########...
  0x1E78,  // ...####..####...
  0x1C38,  // ...###....###...
  0x1C38,  // ...###....###...
  0x1E78,  // ...####..####...
  0x0FF0,  // ....########....
  0x0FF0,  // ....########....
  0x07E0,  // .....######.....
  0x07E0,  // .....######.....
  0x03C0,  // ......####......
  0x03C0,  // ......####......
  0x0180,  // .......##.......
  0x0180,  // .......##.......
};

// lever du soleil (demi-soleil + flèche vers le haut)
static const uint16_t WI_SUNRISE[WI_H] = {
  0x0180,  // .......##.......
  0x03C0,  // ......####......
  0x07E0,  // .....######.....
  0x0180,  // .......##.......
  0x0180,  // .......##.......
  0x0000,  // ................
  0x07E0,  // .....######.....
  0x0FF0,  // ....########....
  0x1FF8,  // ...##########...
  0x3FFC,  // ..############..
  0x3FFC,  // ..############..
  0x0000,  // ................
  0xFFFF,  // ################
  0x0000,  // ................
  0x3FFC,  // ..############..
  0x0000,  // ................
};

// coucher du soleil (demi-soleil + flèche vers le bas)
static const uint16_t WI_SUNSET[WI_H] = {
  0x0180,  // .......##.......
  0x0180,  // .......##.......
  0x07E0,  // .....######.....
  0x03C0,  // ......####......
  0x0180,  // .......##.......
  0x0000,  // ................
  0x07E0,  // .....######.....
  0x0FF0,  // ....########....
  0x1FF8,  // ...##########...
  0x3FFC,  // ..############..
  0x3FFC,  // ..############..
  0x0000,  // ................
  0xFFFF,  // ################
  0x0000,  // ................
  0x3FFC,  // ..############..
  0x0000,  // ................
};

static const char *const WD_FR[7] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
static const char *const WD_EN[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// Prochain événement (lever ou coucher) strictement après « now » (0 = aucun)
static time_t next_event(const time_t *arr, time_t now) {
  time_t best = 0;
  for (int i = 0; i < 3; i++) {
    if (arr[i] > now && (best == 0 || arr[i] < best)) best = arr[i];
  }
  return best;
}

// Heure locale « HH:MM » (12 h si choisi, sans indicateur A/P). Attention : localtime()
// réutilise un tampon unique : l'appelant sauvegarde / restaure sa struct tm.
static void fmt_clock(time_t t, char *buf, size_t n) {
  if (!t) { snprintf(buf, n, "--:--"); return; }
  struct tm *p = localtime(&t);
  int h = p->tm_hour;
  if (s_time12) { h %= 12; if (h == 0) h = 12; }
  snprintf(buf, n, "%02d:%02d", h, p->tm_min);
}

// Distance du jour en km ou miles, 1 décimale (« xx.x » : moins précis, mais affiche de plus grandes distances)
static void fmt_dist(char *buf, size_t n) {
  if (s_dist < 0) { snprintf(buf, n, "--.-"); return; }
  int tenth = s_dist_mi ? (int)((int64_t)s_dist * 621371 / 100000000) : (s_dist + 50) / 100;
  if (tenth > 9999) tenth = 9999;
  snprintf(buf, n, "%d.%d", tenth / 10, tenth % 10);
}

// Une demi-rangée : icône 16×16 à gauche, valeur à droite (police DSEG, ou petite police
// si elle ne tient pas), petite unité tout à droite. Hauteur de cellule ≈ 32 px.
#define STAT_CELL_H 32
static void draw_stat_cell(GContext *ctx, int x, int w, int y0, const uint16_t *icon, GColor icol,
                           const char *value, const char *unit,
                           GFont f_val, GFont f_small, GFont f_unit) {
  draw_bits(ctx, icon, x + 3, y0 + (STAT_CELL_H - WI_H) / 2,
            s_icon_col ? icol : color_from_int(CFG_COLOR_FORECAST_ICON));
  // Sans unité (soleil, pas) : un peu plus de place pour la valeur, sinon « 07:42 » ne tient
  // plus en DSEG depuis que les demi-rangées sont recentrées sur le « : » (91 px au lieu de 95)
  bool has_unit = (unit && unit[0]);
  int right = x + w - (has_unit ? 5 : 3);
  int unit_w = 0;
  if (unit && unit[0]) {
    unit_w = trend_text_w(unit, f_unit) + 2;
    graphics_context_set_text_color(ctx, color_from_int(CFG_COLOR_FORECAST_LABEL));
    graphics_draw_text(ctx, unit, f_unit, GRect(right - unit_w + 2, y0 + 17, unit_w + 2, 11),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
  int vx = x + 3 + WI_W + (has_unit ? 3 : 1);
  int vr = right - (unit_w ? unit_w + 3 : 0);
  int avail = vr - vx;
  graphics_context_set_text_color(ctx, color_from_int(CFG_COLOR_FORECAST_TEMP));
  if (trend_text_w(value, f_val) <= avail) {
    graphics_draw_text(ctx, value, f_val, GRect(vx, y0 + 6, avail, 24),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
  } else {
    graphics_draw_text(ctx, value, f_small, GRect(vx, y0 + 8, avail, 18),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }
}

// Tendance : 3 colonnes (jour + date · icône de la journée · min / max). day_base = 0 : jours J à J+2 ;
// day_base = 3 : jours J+3 à J+5 (les deux rangées en tendance se suivent : 6 jours)
static void draw_trend_row(GContext *ctx, int x0, int group_w, int y0, int day_base, GFont f_tiny, const struct tm *today) {
  GColor c_lbl  = color_from_int(CFG_COLOR_FORECAST_LABEL);
  GColor c_icon = color_from_int(CFG_COLOR_FORECAST_ICON);
  GColor c_sep  = color_from_int(CFG_COLOR_FORECAST_GHOST);
  GColor c_temp = color_from_int(CFG_COLOR_FORECAST_TEMP);
  int cw = group_w / 3;

  // La première colonne est toujours « aujourd'hui » : on saute les jours déjà passés
  int start = -1;
  for (int i = 0; i < 6; i++) {
    if (s_daily[i].code != FC_UNKNOWN && s_daily[i].dom == today->tm_mday) { start = i; break; }
  }

  for (int j = 0; j < 3; j++) {
    int cx = x0 + j * cw;
    int idx = (start >= 0) ? start + day_base + j : -1;
    bool ok = (idx >= 0 && idx < 6 && s_daily[idx].code != FC_UNKNOWN);

    if (j > 0) {
      graphics_context_set_stroke_color(ctx, c_sep);
      graphics_draw_line(ctx, GPoint(cx - 3, y0 + FC_SEP_Y0), GPoint(cx - 3, y0 + FC_SEP_Y1));
    }

    char lbl[12];
    if (ok) snprintf(lbl, sizeof(lbl), "%s %d", ((s_lang == 1) ? WD_EN : WD_FR)[s_daily[idx].wd % 7], s_daily[idx].dom);
    else    snprintf(lbl, sizeof(lbl), "--");
    graphics_context_set_text_color(ctx, c_lbl);
    graphics_draw_text(ctx, lbl, f_tiny, GRect(cx + 2, y0 + FC_LBL_DY, cw - 2, 11),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);

    int y_ico = y0 + FC_ICO_DY;
    if (ok) draw_icon(ctx, weather_icon(s_daily[idx].code, true), cx + 1, y_ico + FC_ICON16_DY, c_icon);

    // min et max en blanc, séparateur « / » en jaune
    char lo[8], hi[8];
    if (ok) { snprintf(lo, sizeof(lo), "%d", s_daily[idx].tmin); snprintf(hi, sizeof(hi), "%d", s_daily[idx].tmax); }
    else    { snprintf(lo, sizeof(lo), "--");                    hi[0] = '\0'; }
    int tx = cx + 20;
    int lw = trend_text_w(lo, f_tiny);
    int sw = trend_text_w("/", f_tiny);
    graphics_context_set_text_color(ctx, c_temp);
    graphics_draw_text(ctx, lo, f_tiny, GRect(tx, y_ico + 2, lw + 4, 16),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    if (hi[0]) {
      graphics_context_set_text_color(ctx, GColorYellow);
      graphics_draw_text(ctx, "/", f_tiny, GRect(tx + lw, y_ico + 2, sw + 4, 16),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      graphics_context_set_text_color(ctx, c_temp);
      graphics_draw_text(ctx, hi, f_tiny, GRect(tx + lw + sw, y_ico + 2, 40, 16),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
  }
}

// Texte, icône, couleur et unité d'une mesure
static void metric_info(int m, char *buf, size_t n, const uint16_t **icon, GColor *col, const char **unit) {
  *unit = NULL;
  switch (m) {
    case M_HR:
      if (s_hr > 0) snprintf(buf, n, "%d", s_hr > 999 ? 999 : s_hr); else snprintf(buf, n, "---");
      *icon = WI_HEART; *col = color_from_int(CFG_COLOR_ICON_HEART); *unit = "BPM";
      break;
    case M_DIST:
      fmt_dist(buf, n);
      *icon = WI_PIN; *col = color_from_int(CFG_COLOR_ICON_PIN); *unit = s_dist_mi ? "MI" : "KM";
      break;
    case M_CAL:
      if (s_cal >= 0) snprintf(buf, n, "%d", s_cal > 99999 ? 99999 : s_cal); else snprintf(buf, n, "-----");
      *icon = WI_FLAME; *col = color_from_int(CFG_COLOR_ICON_FLAME); *unit = "KCAL";
      break;
    default:   // M_STEPS
      if (s_steps >= 0) snprintf(buf, n, "%d", s_steps > 99999 ? 99999 : s_steps); else snprintf(buf, n, "-----");
      *icon = WI_FEET; *col = color_from_int(CFG_COLOR_ICON_STEPS);
      break;
  }
}

// Les deux mesures (gauche, droite) d'une complication « paire » ; false si ce n'en est pas une
static bool comp_pair(int kind, int *l, int *r) {
  switch (kind) {
    case CK_HR_DIST:    *l = M_HR;   *r = M_DIST;  return true;
    case CK_CAL_STEPS:  *l = M_CAL;  *r = M_STEPS; return true;
    case CK_HR_CAL:     *l = M_HR;   *r = M_CAL;   return true;
    case CK_HR_STEPS:   *l = M_HR;   *r = M_STEPS; return true;
    case CK_DIST_CAL:   *l = M_DIST; *r = M_CAL;   return true;
    case CK_DIST_STEPS: *l = M_STEPS; *r = M_DIST;  return true;   // pas + distance
    default:            return false;
  }
}

// Une rangée (hors météo horaire) : 2 demi-rangées (lever + coucher, ou 2 mesures parmi
// FC, distance, calories, pas) ou la tendance sur 3 jours.
static void draw_comp_row(GContext *ctx, int kind, int x0, int group_w, int y0, int mid, int day_base,
                          GFont f_tiny, GFont f_lbl, GFont f_val, const struct tm *today) {
  if (kind == CK_TREND) { draw_trend_row(ctx, x0, group_w, y0, day_base, f_tiny, today); return; }

  // Paire : séparateur EXACTEMENT sur l'axe vertical du « : » de l'heure (« mid », centre du
  // panneau où l'heure est centrée) ; deux demi-rangées de même largeur, symétriques par rapport à lui.
  int half = group_w / 2 - 4;
  graphics_context_set_stroke_color(ctx, color_from_int(CFG_COLOR_FORECAST_GHOST));
  graphics_draw_line(ctx, GPoint(mid, y0 + FC_SEP_Y0), GPoint(mid, y0 + FC_SEP_Y1));

  char a[16], b[16];
  const uint16_t *ia = WI_HEART, *ib = WI_HEART;
  GColor ca = GColorWhite, cb = GColorWhite;
  const char *ua = NULL, *ub = NULL;
  int ml = M_HR, mr = M_DIST;
  if (kind == CK_SUN) {   // prochain lever et prochain coucher
    time_t n = time(NULL);
    fmt_clock(next_event(s_sunrise, n), a, sizeof(a));
    fmt_clock(next_event(s_sunset,  n), b, sizeof(b));
    ia = WI_SUNRISE; ca = color_from_int(CFG_COLOR_ICON_SUN);
    ib = WI_SUNSET;  cb = color_from_int(CFG_COLOR_ICON_SUN);
  } else {
    if (!comp_pair(kind, &ml, &mr)) { ml = M_HR; mr = M_DIST; }
    metric_info(ml, a, sizeof(a), &ia, &ca, &ua);
    metric_info(mr, b, sizeof(b), &ib, &cb, &ub);
  }
  draw_stat_cell(ctx, mid - half, half, y0, ia, ca, a, ua, f_val, f_tiny, f_lbl);
  draw_stat_cell(ctx, mid,        half, y0, ib, cb, b, ub, f_val, f_tiny, f_lbl);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;

#define PX(x) ((x)*W/200)
#define PY(y) ((y)*H/228)

  // ── Colors ────────────────────────────────────────────────────────────
  GColor col_bg    = color_from_int(CFG_COLOR_BG);
  GColor col_fg    = color_from_int(CFG_COLOR_FG);
  // Segments éteints : couleur du niveau choisi ; le cadre de la case de complication
  // garde toujours la couleur normale (c'est une bordure, pas un segment)
  GColor col_ghost_line = color_from_int(CFG_COLOR_GHOST);
  GColor col_ghost = (s_ghost == 1) ? color_from_int(CFG_COLOR_GHOST_LIGHT) : col_ghost_line;
  GColor col_red   = color_from_int(CFG_COLOR_ACCENT);

  // ── Fonts ─────────────────────────────────────────────────────────────
  GFont f_tiny    = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_lbl     = fonts_get_system_font(FONT_KEY_GOTHIC_09);
  GFont f_band    = fonts_get_system_font(FONT_KEY_GOTHIC_14);   // jours + semaine
  GFont f_dseg_lg = s_font_d14_time ? s_font_d14_time
                  : fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont f_date    = s_font_d7_date ? s_font_d7_date
                  : fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  GFont f_comp    = s_font_d7_comp ? s_font_d7_comp
                  : fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  GFont f_comp_g  = s_font_comp_reg ? s_font_comp_reg : f_comp;

  // ── Y anchors ─────────────────────────────────────────────────────────
  int y_rs1     = PY(16);    // haut de l'anneau rouge
  int y_rsb     = PY(13);    // sert au calcul de la hauteur de l'anneau (le prolonge de 3 px sous y_ban)
  int y_btn     = PY(21);    // haut de la ligne de prévisions du haut
  // La ligne de prévisions du haut occupe y_btn..y_lcd (≈ 33 px), celle du bas
  // y_fc..y_rs2 (≈ 32 px) : hauteurs quasi identiques.
  int y_lcd     = PY(54);    // LCD outer frame
  int y_in      = PY(57);    // white panel
  int y_dr      = PY(59);    // date / comp row
  int dr_h      = PY(32);    // 2 px border + 2 px air around the 21 px ink
  int y_time    = PY(88);    // time row
  int y_info    = PY(146);   // info strip separator
  int y_in_end  = PY(158);   // panel end (bandeau d'info plus bas = bande de texte plus haute)
  int y_lcd_end = PY(175);   // fin du cadre (la bande des jours est entre y_in_end et y_lcd_end)
  int y_fc      = PY(176);   // 2e ligne de prévisions
  int y_rs2     = PY(208);   // bas du fond noir des prévisions / haut de la bande rouge inférieure
  int y_ban     = PY(210);   // haut de la zone ville + tendance (texte jaune)

  // ── X anchors ─────────────────────────────────────────────────────────
  int ring_s = (r_r_sides == 0) ? PX(0) : PX(3);    // thin red side rails
  int lx = PX(6),  lw = W - PX(12);       			// LCD outer frame
  int ix = PX(10), iw = W - PX(20);       			// white panel
  int x_l = ix + PX(4);                   			// content left
  int x_r = ix + iw - PX(4);              			// content right
  int comp_w = PX(86);
  int comp_x = x_r - comp_w + PX(1);
  int date_w = comp_x - x_l;

  // ── 1. Black case + red ring ──────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, col_red);
  graphics_fill_rect(ctx, GRect(0, y_rs1, W, y_ban - y_rsb), PX(14), GCornersAll);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(ring_s, y_btn, W - 2 * ring_s, y_rs2 - y_btn),
                     PX(12), GCornersAll);

  // ── 2. Libellés du haut (modifiables dans config.h) ─────────────────────────
  {
    GColor col_lbl = color_from_int(CFG_COLOR_LABEL_TOP);
    graphics_context_set_text_color(ctx, col_lbl);
    graphics_draw_text(ctx, s_label_tl, f_tiny,
                       GRect(PX(6), PY(-1), PX(90), y_rs1),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    // Libellé de droite : a short last word (e.g. "2" of "TIME 2") is drawn in
    // Casio blue like on the original.
    GRect tr_r = GRect(W - PX(96), PY(-1), PX(90), y_rs1);
    const char *sp = strrchr(s_label_tr, ' ');
    if (sp && sp[1] != '\0' && strlen(sp + 1) <= 2) {
      char prefix[32];
      int plen = (int)(sp - s_label_tr);
      if (plen > (int)sizeof(prefix) - 1) plen = sizeof(prefix) - 1;
      memcpy(prefix, s_label_tr, plen);
      prefix[plen] = '\0';
      GSize suf_sz = graphics_text_layout_get_content_size(
          sp + 1, f_tiny, tr_r, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
      graphics_draw_text(ctx, prefix, f_tiny,
                         GRect(tr_r.origin.x, tr_r.origin.y,
                               tr_r.size.w - suf_sz.w - PX(3), tr_r.size.h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
      graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite));
      graphics_draw_text(ctx, sp + 1, f_tiny,
                         GRect(tr_r.origin.x + tr_r.size.w - suf_sz.w, tr_r.origin.y,
                               suf_sz.w, tr_r.size.h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    } else {
      graphics_draw_text(ctx, s_label_tr, f_tiny, tr_r,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }

    // Texte central jaune : centré sur l'écran, sans empiéter sur les libellés
    // de gauche et de droite (décalé ou tronqué si l'espace manque).
    if (s_label_tc[0] != '\0') {
      GRect big = GRect(0, 0, 2000, 100);
      int lw_l = graphics_text_layout_get_content_size(
          s_label_tl, f_tiny, big, GTextOverflowModeFill, GTextAlignmentLeft).w;
      int lw_r = graphics_text_layout_get_content_size(
          s_label_tr, f_tiny, big, GTextOverflowModeFill, GTextAlignmentLeft).w;
      int lw_c = graphics_text_layout_get_content_size(
          s_label_tc, f_tiny, big, GTextOverflowModeFill, GTextAlignmentLeft).w;
      int gap     = 4;
      int x_min   = PX(6) + lw_l + gap;
      int x_max   = W - PX(6) - lw_r - gap;
      int avail   = x_max - x_min;
      if (avail > 0) {
        int cw = lw_c + 2;
        if (cw > avail) cw = avail;
        int x = (W - cw) / 2;                       // centré sur l'écran
        if (x < x_min) x = x_min;
        if (x + cw > x_max) x = x_max - cw;
        graphics_context_set_text_color(ctx, GColorYellow);
        graphics_draw_text(ctx, s_label_tc, f_tiny, GRect(x, PY(-1), cw, y_rs1),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      }
    }
  }

  // ── 4. LCD : cadre extérieur blanc, panneau (CFG_COLOR_BG), bande des jours ──
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_round_rect(ctx, GRect(lx, y_lcd, lw, y_lcd_end - y_lcd), PX(8));
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, col_bg);
  graphics_fill_rect(ctx, GRect(ix, y_in, iw, y_in_end - y_in), PX(5), GCornersTop);
  // optional tint behind the date + comp row
  graphics_context_set_fill_color(ctx, color_from_int(CFG_COLOR_TIME2_BG));
  graphics_fill_rect(ctx, GRect(ix + PX(2), y_dr - PY(2), iw - PX(4), dr_h + PY(3)),
                     PX(4), GCornersAll);

  // ── 5. Date ───────────────────────────────────────────────────────────
  time_t now_t = time(NULL);
  struct tm *tnow = localtime(&now_t);
  char date_str[8];
  unsigned dd = (unsigned)tnow->tm_mday, dm = (unsigned)(tnow->tm_mon + 1);
  // s_date_mdy : 0 = JJ-MM, 1 = MM-JJ
  snprintf(date_str, sizeof(date_str), "%02u-%02u",
           s_date_mdy ? dm : dd, s_date_mdy ? dd : dm);
  lcd_text(ctx, "88-88", date_str, f_date,
         GRect(x_l, y_dr + (dr_h - 20) / 2, date_w, 24),
         col_ghost, col_fg, GTextAlignmentLeft);

  // ── 6. Case de complication : pas / fréquence cardiaque / distance (+ K ou M) / soleil / secondes ──
  {
    char cstr[24];
    const char *ghost = "88888";
    int arrow = 0;   // soleil : 0 = aucune, 1 = lever (▲), 2 = coucher (▼)
    char unit_ch = 0;   // distance : 'K' (km) ou 'M' (miles), 0 = aucune
    int hr_logo = 0;    // fréquence cardiaque : logo de battement dans les 2 derniers chiffres
    switch (s_comp) {
      case 1:   // fréquence cardiaque (BPM) : 3 chiffres de gauche + logo de battement
        hr_logo = 1;
        if (s_hr > 0) snprintf(cstr, sizeof(cstr), "%d", s_hr > 999 ? 999 : s_hr);
        else          snprintf(cstr, sizeof(cstr), "---");
        ghost = "888";
        break;
      case 2:   // distance du jour : kilomètres ou miles, avec 2 décimales
        if (s_dist < 0) {
          snprintf(cstr, sizeof(cstr), "--.--");
        } else {
          // centièmes de mile (1 m = 0,000621371 mi) ou de km (arrondi au plus proche)
          int hund = s_dist_mi ? (int)((int64_t)s_dist * 621371 / 10000000)
                               : (s_dist + 5) / 10;
          if (hund > 9999) hund = 9999;
          snprintf(cstr, sizeof(cstr), "%d.%02d", hund / 100, hund % 100);
        }
        ghost = "88.88";
        unit_ch = s_dist_mi ? 'M' : 'K';
        break;
      case 3: { // prochain lever / coucher du soleil (heure locale, 24 h)
        time_t best = 0;
        for (int i = 0; i < 3; i++) {
          if (s_sunrise[i] > now_t && (best == 0 || s_sunrise[i] < best)) { best = s_sunrise[i]; arrow = 1; }
          if (s_sunset[i]  > now_t && (best == 0 || s_sunset[i]  < best)) { best = s_sunset[i];  arrow = 2; }
        }
        if (best) {
          // localtime() réutilise le même tampon que tnow : on le sauvegarde et on le restaure
          struct tm saved = *tnow;
          struct tm *ev = localtime(&best);
          int eh = ev->tm_hour;
          if (s_time12) { eh %= 12; if (eh == 0) eh = 12; }   // 12 h : pas d'indicateur A/P ici
          snprintf(cstr, sizeof(cstr), "%02d:%02d", eh, ev->tm_min);
          *tnow = saved;
        } else {
          snprintf(cstr, sizeof(cstr), "--:--");
          arrow = 0;
        }
        ghost = "88:88";
        break;
      }
      case 4:   // secondes (le tick passe alors à la seconde)
        snprintf(cstr, sizeof(cstr), "%02d", tnow->tm_sec);
        ghost = "88";
        break;
      default:  // nombre de pas
        if (s_steps >= 0) snprintf(cstr, sizeof(cstr), "%d", s_steps > 99999 ? 99999 : s_steps);
        else              snprintf(cstr, sizeof(cstr), "-----");
        break;
    }
	graphics_context_set_stroke_color(ctx, GColorBlack);
	graphics_context_set_stroke_width(ctx, 1);
	graphics_draw_round_rect(ctx, GRect(comp_x + 1, y_dr + 2, comp_w - 2, dr_h - 4), PX(5));
	graphics_context_set_stroke_width(ctx, 1);

    int ty = y_dr + (dr_h - 20) / 2 + PY(1);
    GRect all_r = GRect(comp_x + PX(3), ty, comp_w - PX(6), 24);
    GTextAlignment align = GTextAlignmentRight;
    int unit_x = 0, unit_gap = 0;   // place libre à droite des chiffres (K / M ou logo)
    if (unit_ch || hr_logo) {
      // Grille de 5 chiffres (comme les pas) : la distance utilise les 4 de gauche
      // (« 88.88 ») et le 5e emplacement reçoit la lettre K ou M ; la fréquence
      // cardiaque utilise les 3 de gauche (« 888 ») et le logo les 2 derniers.
      int w5 = graphics_text_layout_get_content_size("88888", f_comp, all_r,
                   GTextOverflowModeFill, GTextAlignmentLeft).w;   // largeur des 5 chiffres
      int wg = graphics_text_layout_get_content_size(ghost, f_comp, all_r,
                   GTextOverflowModeFill, GTextAlignmentLeft).w;   // largeur de « 88.88 »
      int right = all_r.origin.x + all_r.size.w;                   // bord droit de la grille
      all_r = GRect(right - w5, all_r.origin.y, wg, all_r.size.h); // zone des 4 chiffres
      unit_x = right;                                              // bord droit de la lettre
      unit_gap = w5 - wg;                                          // place restante (à droite des chiffres)
    }
    if (GHOST_ON && CFG_GHOST_COMP_ENABLED) {
      graphics_context_set_text_color(ctx, col_ghost);
      char gbuf[16];
      ghost_text(ghost, gbuf, sizeof(gbuf));
      graphics_draw_text(ctx, gbuf, f_comp_g, all_r, GTextOverflowModeFill,
                         align, NULL);
    }
    graphics_context_set_text_color(ctx, col_fg);
    graphics_draw_text(ctx, cstr, f_comp, all_r, GTextOverflowModeFill,
                       align, NULL);

    // Distance : K ou M dans la cellule du 5e chiffre. Grande lettre 5×7 (10×14 px)
    // si la place le permet, sinon la petite 3×5 (6×10 px).
    if (unit_ch) {
      if (unit_gap >= 11)
        draw_big_letter(ctx, unit_x - 13, y_dr + (dr_h - 14) / 2 + 1, unit_ch, col_fg);
      else
        draw_ind_letter(ctx, unit_x - 9, y_dr + (dr_h - 10) / 2 + 1, unit_ch, col_fg);
    }

    // Fréquence cardiaque : logo de battement (tracé d'électrocardiogramme) à droite
    // des 3 chiffres, environ 2 chiffres de large et 1 chiffre de haut.
    if (hr_logo && unit_gap > 8) {
      int lw = unit_gap - 3;                         // 2 px d'espace à gauche, 1 px à droite
      draw_ecg(ctx, unit_x - lw - 1, y_dr + (dr_h - 14) / 2 + 1, lw, 14, col_fg);
    }

    // Soleil : petite flèche à gauche (▲ lever, ▼ coucher) et trait d'horizon
    if (arrow) {
      int ax = comp_x + PX(5), ay = y_dr + dr_h / 2;
      graphics_context_set_stroke_color(ctx, col_fg);
      for (int r = 0; r < 4; r++) {
        int yy = (arrow == 1) ? (ay - 4 + r) : (ay + 2 - r);
        graphics_draw_line(ctx, GPoint(ax + 3 - r, yy), GPoint(ax + 3 + r, yy));
      }
      graphics_draw_line(ctx, GPoint(ax - 1, ay + 4), GPoint(ax + 7, ay + 4));
    }
  }

  // ── 7. Time HH:MM ────────────────────────────
  {
    char time_str[8];
    int hh = tnow->tm_hour;
    // 24 h ou 12 h selon le réglage (le réglage du téléphone est ignoré)
    if (s_time12) { hh %= 12; if (hh == 0) hh = 12; }
    snprintf(time_str, sizeof(time_str), "%02d:%02d", hh, tnow->tm_min);
    int t_h = y_info - y_time;

      // HH:MM @48 (≈164 px) centered
      lcd_text(ctx, "88:88", time_str, f_dseg_lg,
               GRect(ix, y_time + (t_h - 48) / 2, iw, 54),
               col_ghost, col_fg, GTextAlignmentCenter);

    // 12 h : indicateurs P (en haut) et A (en bas) à gauche de l'heure.
    // Les deux sont dessinés éteints (fantômes), celui de la période en cours allumé.
    // Aucun indicateur en 24 h.
    if (s_time12) {
      int ix_ind = ix + PX(3);      // 2 px plus à droite qu'avant
      int y_p = y_time + PY(5);     // 2 px plus bas qu'avant
      int y_a = y_p + 13;
      int pm = (tnow->tm_hour >= 12);
      if (GHOST_ON) {
        draw_ind_letter(ctx, ix_ind, y_p, 'P', col_ghost);
        draw_ind_letter(ctx, ix_ind, y_a, 'A', col_ghost);
      }
      draw_ind_letter(ctx, ix_ind, pm ? y_p : y_a, pm ? 'P' : 'A', col_fg);
    }
  }

  // ── 8. Bandeau d'info : pile | carrés des jours ─────────────────────────
  int strip_h = y_in_end - y_info;
  int mid_x   = ix + iw / 2 - PX(8);
  graphics_context_set_stroke_color(ctx, col_fg);
  graphics_draw_line(ctx, GPoint(ix + PX(2), y_info), GPoint(ix + iw - PX(3), y_info));
  graphics_draw_line(ctx, GPoint(mid_x, y_info), GPoint(mid_x, y_in_end - 1));
  {
    // Pile façon LCD Casio sur toute la largeur de la zone gauche du bandeau :
    // contour + plot à droite, segments internes (nombre adapté à la largeur).
    {
      int total_w = mid_x - PX(5) - x_l;           // zone disponible (ex-barres)
      int nub_w   = 3;
      int ib_w    = total_w - nub_w;               // corps de la pile
      int ib_h    = strip_h - 6;                   // pile moins haute (texte plus grand dessous)
      if (ib_h < 6) ib_h = 6;
      if (ib_h > strip_h - 2) ib_h = strip_h - 2;
      int ib_y    = y_info + (strip_h - 5) / 2 + 1;   // 1 px plus bas : bord bas inchangé, haut raccourci de 1 px
      int nub_h   = ib_h / 2; if (nub_h < 3) nub_h = 3;

      graphics_context_set_stroke_color(ctx, col_fg);
      graphics_draw_rect(ctx, GRect(x_l, ib_y, ib_w, ib_h));       // contour
      graphics_context_set_fill_color(ctx, col_fg);
      graphics_fill_rect(ctx, GRect(x_l + ib_w, ib_y + (ib_h - nub_h) / 2, nub_w, nub_h),
                         0, GCornerNone);                          // plot

      // Largeur de la pile inchangée : on choisit le nombre de segments (8..12)
      // dont le pas divise au mieux la largeur interne (reste minimal, puis
      // le plus proche de 10). Le dernier segment n'a pas d'espace final.
      int in_w   = ib_w - 3;                       // 1 px contour + 1 px marge + espace final
      int in_h   = ib_h - 4;
      int n_seg  = 10, best_rem = 1000;
      for (int n = 8; n <= 12; n++) {
        if (in_w / n < 3) continue;                // segment trop fin
        int rem = in_w % n;
        if (rem < best_rem ||
            (rem == best_rem && abs(n - 10) < abs(n_seg - 10))) {
          best_rem = rem;
          n_seg = n;
        }
      }
      int step   = in_w / n_seg;
      int seg_w  = step - 1; if (seg_w < 1) seg_w = 1;
      int in_x   = x_l + 2 + (in_w - step * n_seg) / 2;  // centre le petit reste
      int in_y   = ib_y + 2;
      int lit    = (s_batt_pct * n_seg + 50) / 100;
      if (lit > n_seg) lit = n_seg;
      for (int k = 0; k < n_seg; k++) {
        if (k >= lit && !GHOST_ON) continue;
        graphics_context_set_fill_color(ctx, k < lit ? col_fg : col_ghost);
        graphics_fill_rect(ctx, GRect(in_x + k * step, in_y, seg_w, in_h),
                           0, GCornerNone);
      }

      // Année + semaine ISO sous la pile (« 2026 - SEM 41 » en français,
      // « 2026-WEEK 41 » en anglais : sans espaces autour du tiret), alignée à
      // gauche avec la pile. Si le texte déborde sur les initiales de jours,
      // on retombe sur la petite police.
      int iso_y, iso_w;
      iso_week(tnow, &iso_y, &iso_w);
      char wk_str[32];
      if (s_lang == 1) snprintf(wk_str, sizeof(wk_str), "%d-WEEK %d", iso_y, iso_w);
      else                   snprintf(wk_str, sizeof(wk_str), "%d - SEM %d", iso_y, iso_w);
      int wk_avail = (mid_x + PX(4)) - x_l - 2;
      GFont f_wk = f_band;
      int wk_h = 16, wk_dy = -2;               // GOTHIC_14 : 2 px de blanc en haut
      GSize wk_sz = graphics_text_layout_get_content_size(
          wk_str, f_wk, GRect(0, 0, W, 20), GTextOverflowModeFill, GTextAlignmentLeft);
      if (wk_sz.w > wk_avail) { f_wk = f_lbl; wk_h = 11; wk_dy = -1; }
      int lb_y = y_in_end + (y_lcd_end - y_in_end - wk_h) / 2 + wk_dy;
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, wk_str, f_wk, GRect(x_l, lb_y, wk_avail, wk_h),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
  }

  // ── 9. Weekday squares (panel) + letters (dark frame band) ────────────
  // Initiales des jours, la semaine commençant le lundi. s_lang choisit la langue.
  static const char *const D_MON_FR[7] = {"L","M","M","J","V","S","D"};
  static const char *const D_MON_EN[7] = {"M","T","W","T","F","S","S"};
  const char *const *days_arr = (s_lang == 1) ? D_MON_EN : D_MON_FR;
  // Position (0 = lundi … 6 = dimanche) du jour courant
  int today_idx = (tnow->tm_wday + 6) % 7;
  {
    int wx = mid_x + PX(4);
    int step = (x_r - wx) / 7;
    int sq = PX(6);
    int sq_y = y_info + (strip_h - sq) / 2;
    for (int i = 0; i < 7; i++) {
      int dx = wx + i * step + (step - sq) / 2;
      if (i == today_idx) {
        graphics_context_set_fill_color(ctx, col_fg);
        graphics_fill_rect(ctx, GRect(dx, sq_y, sq, sq), 0, GCornerNone);
      } else if (GHOST_ON) {
        graphics_context_set_fill_color(ctx, col_ghost);
        graphics_fill_rect(ctx, GRect(dx, sq_y, sq, sq), 0, GCornerNone);
      } else {
        graphics_context_set_stroke_color(ctx, col_ghost);
        graphics_draw_rect(ctx, GRect(dx, sq_y, sq, sq));
      }
    }
    int lt_y = y_in_end + (y_lcd_end - y_in_end - 16) / 2 - 2;
    graphics_context_set_text_color(ctx, GColorWhite);
    for (int i = 0; i < 7; i++) {
      int dx = wx + i * step;
      graphics_draw_text(ctx, days_arr[i], f_band, GRect(dx, lt_y, step, 16),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }

  // ── 3 + 10. Prévisions horaires : 2 lignes × 5 colonnes ────────────────
  // Ligne du haut (au-dessus du LCD) : H+1 … H+5 ; ligne du bas : H+6 … H+10.
  // Chaque colonne (38 px) : libellé (14H…) en haut, puis le logo 16×16 dessiné
  // pixel par pixel à gauche et la température à droite. Si CFG_GHOST_FORECAST,
  // le glyphe DSEGWeather « tous segments allumés » ('0') est dessiné derrière
  // le logo pour faire office de segments éteints.
  {
    GFont f_icon = s_font_weather ? s_font_weather : f_lbl;
    // 5 colonnes centrées dans l'écran, puis décalées de 4 px (PX) vers la droite.
    int col_w = PX(38);
    int group_w = FC_PER_ROW * col_w;
    int x0 = (W - group_w) / 2 + PX(4);

    bool fresh = fc_fresh();
    // localtime() (complication soleil) réutilise le tampon de tnow : on le sauvegarde / restaure
    struct tm tsave = *tnow;
    int kinds[FC_ROWS] = { s_comp_top, s_comp_bot };
    // Deux rangées météo : elles se suivent (H+1…H+5 puis H+6…H+10) ; une seule : H+1…H+5
    bool both_wx = (kinds[0] == CK_WEATHER && kinds[1] == CK_WEATHER);
    // Deux rangées en tendance : elles se suivent (J à J+2, puis J+3 à J+5 : 6 jours)
    bool both_tr = (kinds[0] == CK_TREND && kinds[1] == CK_TREND);
    GColor c_lbl   = color_from_int(CFG_COLOR_FORECAST_LABEL);
    GColor c_icon  = color_from_int(CFG_COLOR_FORECAST_ICON);
    GColor c_ghost = color_from_int(CFG_COLOR_FORECAST_GHOST);   // séparateurs
    GColor c_ghost_seg = (s_ghost == 1) ? color_from_int(CFG_COLOR_FORECAST_GHOST_LIGHT) : c_ghost;   // segments d'icône éteints
    GColor c_temp  = color_from_int(CFG_COLOR_FORECAST_TEMP);

    for (int row = 0; row < FC_ROWS; row++) {
      int y0    = (row == 0) ? y_btn : y_fc;   // haut de la cellule
      int y_ico = y0 + FC_ICO_DY;

      if (kinds[row] != CK_WEATHER) {   // autre complication : FC + distance, calories + pas, soleil, tendance 3 jours
        draw_comp_row(ctx, kinds[row], x0, group_w, y0, ix + iw / 2, (both_tr && row == 1) ? 3 : 0, f_tiny, f_lbl, f_date, &tsave);
        continue;
      }
      int fc_base = (both_wx && row == 1) ? FC_PER_ROW : 0;

      for (int c = 0; c < FC_PER_ROW; c++) {
        int i  = fc_base + c;
        int cx = x0 + c * col_w;
        bool ok = fc_slot_valid(i);

        if (c > 0) {   // séparateur vertical discret
          graphics_context_set_stroke_color(ctx, c_ghost);
          graphics_draw_line(ctx, GPoint(cx - PX(3), y0 + FC_SEP_Y0),
                                  GPoint(cx - PX(3), y0 + FC_SEP_Y1));
        }

        // libellé
        char lbl[8];
        if (!fresh) {
          snprintf(lbl, sizeof(lbl), "--");
        } else if (s_time12) {
          // 12 h : « 10HA » (matin) / « 10HP » (après-midi) ; minuit = 12HA, midi = 12HP
          int h12 = s_fc[i].hour % 12;
          if (h12 == 0) h12 = 12;
          snprintf(lbl, sizeof(lbl), "%dH%c", h12, s_fc[i].hour < 12 ? 'A' : 'P');
        } else {
          snprintf(lbl, sizeof(lbl), "%dH", s_fc[i].hour);
        }
        graphics_context_set_text_color(ctx, c_lbl);
        graphics_draw_text(ctx, lbl, f_tiny,
                           GRect(cx + PX(2), y0 + FC_LBL_DY, col_w - PX(2), 11),
                           GTextOverflowModeFill, GTextAlignmentLeft, NULL);

        // icône (segments éteints, puis logo allumé)
        GRect ico_r = GRect(cx + PX(1), y_ico - FC_ICON_UP, PX(18), 26);   // zone du glyphe fantôme, remontée de FC_ICON_UP (la température ne bouge pas)
        if (GHOST_ON && CFG_GHOST_FORECAST) {
          graphics_context_set_text_color(ctx, c_ghost_seg);
          graphics_draw_text(ctx, "0", f_icon, ico_r, GTextOverflowModeFill,
                             GTextAlignmentLeft, NULL);
        }
        if (ok) {
          int ic = weather_icon(s_fc[i].code, s_fc[i].day);
          draw_icon(ctx, ic, cx + PX(1), y_ico + FC_ICON16_DY, c_icon);
        }

        // température
        char tb[8];
        if (ok) snprintf(tb, sizeof(tb), "%d", s_fc[i].temp);
        else    snprintf(tb, sizeof(tb), "--");
        graphics_context_set_text_color(ctx, c_temp);
        graphics_draw_text(ctx, tb, f_tiny,
                           GRect(cx + PX(18), y_ico + PY(2), col_w - PX(18), 16),
                           GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      }
    }
    *tnow = tsave;
  }

// ── 11. Bandeau du bas : ville + tendance météo (texte jaune) ──────────
graphics_context_set_text_color(ctx, GColorYellow);
draw_weather_trend(ctx, f_tiny, y_ban + PY(2), W, H);

#undef PX
#undef PY
}

// ── Demande de rafraîchissement au téléphone ─────────────────────────────
static void request_weather(void) {
  DictionaryIterator *iter;
  s_last_req_ts = time(NULL);
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
  }
}

// À la reconnexion Bluetooth, on redemande les prévisions tout de suite.
static void app_connection_handler(bool connected) {
  if (connected) request_weather();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Tick ──────────────────────────────────────────────────────────────────
// Garde-fou : si le téléphone a suspendu son JS, les prévisions ne se
// renouvellent plus. Au-delà de 45 min sans mise à jour on le réveille par un
// AppMessage, au plus une fois toutes les 10 min.
static void weather_watchdog(void) {
  time_t now = time(NULL);
  if (now - s_fc_ts < 45 * 60 || now - s_last_req_ts < 10 * 60) return;
  if (!connection_service_peek_pebble_app_connection()) return;
  request_weather();
}

// Pas, distance du jour et fréquence cardiaque : lus au démarrage, à
// chaque minute et à chaque mise à jour du service Santé.
// Mode PBL_HEALTH seulement.
static void update_health(void) {
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask m;
  m = health_service_metric_accessible(HealthMetricStepCount,
                                       time_start_of_today(), time(NULL));
  if (m & HealthServiceAccessibilityMaskAvailable)
    s_steps = (int)health_service_sum_today(HealthMetricStepCount);
  m = health_service_metric_accessible(HealthMetricWalkedDistanceMeters,
                                       time_start_of_today(), time(NULL));
  if (m & HealthServiceAccessibilityMaskAvailable)
    s_dist = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
  m = health_service_metric_accessible(HealthMetricActiveKCalories,
                                       time_start_of_today(), time(NULL));
  if (m & HealthServiceAccessibilityMaskAvailable) {
    s_cal = (int)health_service_sum_today(HealthMetricActiveKCalories);
#if CFG_CALORIES_TOTAL
    s_cal += (int)health_service_sum_today(HealthMetricRestingKCalories);
#endif
  }
  m = health_service_metric_accessible(HealthMetricHeartRateBPM,
                                       time(NULL), time(NULL));
  if (m & HealthServiceAccessibilityMaskAvailable)
    s_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
#endif
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate ||
      event == HealthEventHeartRateUpdate) {
    update_health();
    if (s_canvas) layer_mark_dirty(s_canvas);
  }
}
#endif

static void tick_handler(struct tm *tick_time, TimeUnits units) {
  if (units & MINUTE_UNIT) {
    weather_watchdog();
    update_health();
    s_batt_pct = (int)battery_state_service_peek().charge_percent;
  }
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Abonnement au tick : à la seconde seulement si la complication « secondes »
// est choisie (consomme plus de batterie), sinon à la minute ──
static void update_tick_subscription(void) {
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(s_comp == 4 ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

// ── AppMessage : prévisions, tendance météo et réglages de la page Clay ──
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  // Réglages de la page Clay : appliqués et mémorisés (même en mode test)
  if ((t = dict_find(iter, MESSAGE_KEY_LANGUAGE))) {
    s_lang = (t->value->int32 == 1) ? 1 : 0;
    persist_write_int(PERSIST_LANG, s_lang);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DATE_FORMAT))) {
    s_date_mdy = (t->value->int32 == 1) ? 1 : 0;
    persist_write_int(PERSIST_DATE_MDY, s_date_mdy);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TIME_FORMAT))) {
    s_time12 = (t->value->int32 == 1) ? 1 : 0;
    persist_write_int(PERSIST_TIME12, s_time12);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ICON_STYLE))) {
    s_icon_col = (t->value->int32 == 1) ? 1 : 0;
    persist_write_int(PERSIST_ICON_COL, s_icon_col);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_GHOST_LEVEL))) {
    s_ghost = (int)t->value->int32;
    if (s_ghost < 0 || s_ghost > 2) s_ghost = 0;
    persist_write_int(PERSIST_GHOST, s_ghost);
  }
  // Style des polices : rechargement s'il change
  {
    int old_st = s_font_style;
    if ((t = dict_find(iter, MESSAGE_KEY_FONT_STYLE))) {
      s_font_style = (int)t->value->int32;
      if (s_font_style < 0 || s_font_style > 7) s_font_style = 0;
      persist_write_int(PERSIST_FONT_ST, s_font_style);
    }
    if (old_st != s_font_style && s_canvas) load_dseg_fonts();
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COMPLICATION))) {
    s_comp = (int)t->value->int32;
    if (s_comp < 0 || s_comp > 4) s_comp = 0;
    persist_write_int(PERSIST_COMP, s_comp);
    update_tick_subscription();   // secondes : tick à la seconde, sinon à la minute
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DIST_UNIT))) {
    s_dist_mi = (t->value->int32 == 1) ? 1 : 0;
    persist_write_int(PERSIST_DIST_MI, s_dist_mi);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_RED_RING_LATERAL))) {
    r_r_sides = (t->value->int32 == 1) ? 1 : 0;
    persist_write_int(PERSIST_RED_RING, r_r_sides);
    if (s_canvas) layer_mark_dirty(s_canvas);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COMP_TOP))) {
    s_comp_top = (int)t->value->int32;
    if (s_comp_top < 0 || s_comp_top >= CK_COUNT) s_comp_top = 0;
    persist_write_int(PERSIST_COMP_TOP, s_comp_top);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COMP_BOTTOM))) {
    s_comp_bot = (int)t->value->int32;
    if (s_comp_bot < 0 || s_comp_bot >= CK_COUNT) s_comp_bot = 0;
    persist_write_int(PERSIST_COMP_BOT, s_comp_bot);
  }
  // Tendance sur 6 jours : code, max, min, jour du mois, jour de la semaine
  for (int i = 0; i < 6; i++) {
    if ((t = dict_find(iter, DAILY_KEYS[i]))) {
      uint32_t v = (uint32_t)t->value->int32;
      s_daily[i].code = (int)(v & 0x7F);
      s_daily[i].tmax = (int)((v >> 8) & 0xFF) - 128;
      s_daily[i].tmin = (int)((v >> 16) & 0xFF) - 128;
      s_daily[i].dom  = (int)((v >> 24) & 0x1F);
      s_daily[i].wd   = (int)((v >> 29) & 0x07);
    }
  }
  // Lever / coucher du soleil (0 = inconnu)
  for (int i = 0; i < 3; i++) {
    if ((t = dict_find(iter, SUNRISE_KEYS[i]))) s_sunrise[i] = (time_t)t->value->int32;
    if ((t = dict_find(iter, SUNSET_KEYS[i])))  s_sunset[i]  = (time_t)t->value->int32;
  }
#if CFG_ICON_TEST
  if (s_canvas) layer_mark_dirty(s_canvas);
  return;   // mode test : on garde les prévisions factices
#endif

  for (int i = 0; i < FC_COLS; i++) {
    if ((t = dict_find(iter, FORECAST_KEYS[i]))) {
      int32_t v = t->value->int32;
      s_fc[i].code  = (int)(v & 0x7F);
      s_fc[i].day   = ((v >> 7) & 1) != 0;
      s_fc[i].temp  = (int)((v >> 8) & 0xFF) - 128;
      s_fc[i].hour  = (int)((v >> 16) & 0xFF);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_FORECAST_TS))) s_fc_ts = (time_t)t->value->int32;
  
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_TREND))) {
  strncpy(s_weather_trend, t->value->cstring, sizeof(s_weather_trend) - 1);
  s_weather_trend[sizeof(s_weather_trend) - 1] = '\0';
  }
  
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ── Polices DSEG : 8 styles × (heure 48 px gras, 20 px gras, 20 px normal) ──
// Index = style : 0..3 = 7 segments (Classic, Classic Mini, Modern, Modern Mini),
// 4..7 = 14 segments (Classic, Classic Mini, Modern, Modern Mini).
// Seules les polices du style choisi sont chargées (économie de RAM).
// ATTENTION : la taille de la police est lue dans le NOM de la ressource (package.json) :
// il ne doit contenir qu'un seul nombre, à la fin (…_TIME48, …_BOLD20). Un nom comme
// FONT_DSEG_14C_… serait lu comme une taille de 14 px. Les styles 14 segments portent
// donc le code « F » (FC, FCM, FM, FMM) et non « 14 ».
// Date et chiffres allumés de la complication : 20 px gras ; chiffres éteints
// (fantômes) de la complication : 20 px normal.
static const uint32_t RES_TIME48[8] = {
  RESOURCE_ID_FONT_DSEG_C_TIME48, RESOURCE_ID_FONT_DSEG_CM_TIME48,
  RESOURCE_ID_FONT_DSEG_M_TIME48, RESOURCE_ID_FONT_DSEG_MM_TIME48,
  RESOURCE_ID_FONT_DSEG_FC_TIME48, RESOURCE_ID_FONT_DSEG_FCM_TIME48,
  RESOURCE_ID_FONT_DSEG_FM_TIME48, RESOURCE_ID_FONT_DSEG_FMM_TIME48
};
static const uint32_t RES_F20_BOLD[8] = {
  RESOURCE_ID_FONT_DSEG_C_BOLD20, RESOURCE_ID_FONT_DSEG_CM_BOLD20,
  RESOURCE_ID_FONT_DSEG_M_BOLD20, RESOURCE_ID_FONT_DSEG_MM_BOLD20,
  RESOURCE_ID_FONT_DSEG_FC_BOLD20, RESOURCE_ID_FONT_DSEG_FCM_BOLD20,
  RESOURCE_ID_FONT_DSEG_FM_BOLD20, RESOURCE_ID_FONT_DSEG_FMM_BOLD20
};
static const uint32_t RES_F20_REG[8] = {
  RESOURCE_ID_FONT_DSEG_C_REG20, RESOURCE_ID_FONT_DSEG_CM_REG20,
  RESOURCE_ID_FONT_DSEG_M_REG20, RESOURCE_ID_FONT_DSEG_MM_REG20,
  RESOURCE_ID_FONT_DSEG_FC_REG20, RESOURCE_ID_FONT_DSEG_FCM_REG20,
  RESOURCE_ID_FONT_DSEG_FM_REG20, RESOURCE_ID_FONT_DSEG_FMM_REG20
};

static void unload_dseg_fonts(void) {
  if (s_font_d14_time) { fonts_unload_custom_font(s_font_d14_time); s_font_d14_time = NULL; }
  if (s_font_comp_reg) { fonts_unload_custom_font(s_font_comp_reg); s_font_comp_reg = NULL; }
  if (s_font_d7_date)  { fonts_unload_custom_font(s_font_d7_date);  s_font_d7_date  = NULL; }
  s_font_d7_comp = NULL;   // simple alias de s_font_d7_date
}

static void load_dseg_fonts(void) {
  unload_dseg_fonts();
  int st = (s_font_style >= 0 && s_font_style <= 7) ? s_font_style : 0;
  s_font_d14_time = fonts_load_custom_font(resource_get_handle(RES_TIME48[st]));
  s_font_d7_date  = fonts_load_custom_font(resource_get_handle(RES_F20_BOLD[st]));
  s_font_d7_comp  = s_font_d7_date;
  s_font_comp_reg = fonts_load_custom_font(resource_get_handle(RES_F20_REG[st]));
}

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *w) {
  load_dseg_fonts();
  s_font_weather  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_WEATHER20));

#if CFG_ICON_TEST
  // Mode test (config.h) : affiche les logos sans attendre le téléphone.
  {
    static const struct { int code; bool day; } T[11] = {
      {0, true}, {0, false}, {2, true}, {2, false}, {3, true}, {45, true},
      {61, true}, {65, true}, {73, true}, {95, true}, {96, true}
    };
    for (int i = 0; i < FC_COLS; i++) {
      int k = (i + CFG_ICON_TEST - 1) % 11;
      s_fc[i].code = T[k].code;
      s_fc[i].day  = T[k].day;
      s_fc[i].temp = -5 + 3 * i;
      s_fc[i].hour = (14 + i) % 24;
    }
    s_fc_ts = time(NULL);
  }
#endif

  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  unload_dseg_fonts();
  if (s_font_weather)  { fonts_unload_custom_font(s_font_weather);  s_font_weather  = NULL; }
}

// ── App lifecycle ─────────────────────────────────────────────────────────
// Relit les réglages mémorisés (sinon valeurs par défaut de config.h)
static void load_settings(void) {
  if (persist_exists(PERSIST_LANG))     s_lang     = (persist_read_int(PERSIST_LANG) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_DATE_MDY)) s_date_mdy = (persist_read_int(PERSIST_DATE_MDY) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_TIME12))   s_time12   = (persist_read_int(PERSIST_TIME12) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_ICON_COL)) s_icon_col = (persist_read_int(PERSIST_ICON_COL) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_GHOST))    s_ghost    = persist_read_int(PERSIST_GHOST);
  if (s_ghost < 0 || s_ghost > 2) s_ghost = CFG_GHOST_LEVEL;
  if (persist_exists(PERSIST_FONT_ST))  s_font_style   = persist_read_int(PERSIST_FONT_ST);
  if (s_font_style < 0 || s_font_style > 7) s_font_style = CFG_FONT_STYLE;
  if (persist_exists(PERSIST_COMP))     s_comp     = persist_read_int(PERSIST_COMP);
  if (persist_exists(PERSIST_DIST_MI))  s_dist_mi  = (persist_read_int(PERSIST_DIST_MI) == 1) ? 1 : 0;
  if (s_comp < 0 || s_comp > 4) s_comp = CFG_COMPLICATION;
  if (persist_exists(PERSIST_RED_RING)) r_r_sides = (persist_read_int(PERSIST_RED_RING) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_COMP_TOP)) s_comp_top = persist_read_int(PERSIST_COMP_TOP);
  if (s_comp_top < 0 || s_comp_top >= CK_COUNT) s_comp_top = CFG_COMP_TOP;
  if (persist_exists(PERSIST_COMP_BOT)) s_comp_bot = persist_read_int(PERSIST_COMP_BOT);
  if (s_comp_bot < 0 || s_comp_bot >= CK_COUNT) s_comp_bot = CFG_COMP_BOTTOM;
}

static void init(void) {
  init_forecast_keys();
  for (int i = 0; i < 6; i++) s_daily[i].code = FC_UNKNOWN;   // pas encore de tendance
  load_settings();
  battery_state_service_subscribe(battery_state_handler);
  s_batt_pct = (int)battery_state_service_peek().charge_percent;
  update_health();
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);
  update_tick_subscription();
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = app_connection_handler
  });
  {
    uint32_t in_max  = app_message_inbox_size_maximum();
    uint32_t out_max = app_message_outbox_size_maximum();
    app_message_open(in_max  < 768u ? in_max  : 768u,   // 512 → 768 : tendance 3 jours + 3 réglages en plus
                     out_max < 256u ? out_max : 256u);
  }
  app_message_register_inbox_received(inbox_received);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
