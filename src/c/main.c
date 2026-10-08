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
#ifndef CFG_ICON_COLOR
#define CFG_ICON_COLOR        1
#endif
#ifndef CFG_COMPLICATION
#define CFG_COMPLICATION      0
#endif
#ifndef CFG_DIST_MILES
#define CFG_DIST_MILES        0
#endif

// ── Clés AppMessage (téléphone ↔ montre) ─────────────────────────────────
// Les noms sont déclarés dans messageKeys de package.json ; le SDK en fait les
// constantes MESSAGE_KEY_xxx (numéros attribués automatiquement).
//   FORECAST_0 … FORECAST_9  colonnes de prévisions (voir ci-dessous)
//   FORECAST_TS              heure (s Unix) du calcul des prévisions
//   REQUEST_WEATHER          montre → téléphone : rafraîchir maintenant
//   WEATHER_TREND            texte « VILLE : TENDANCE » (téléphone → montre)
//   LANGUAGE, DATE_FORMAT, TIME_FORMAT, ICON_STYLE, COMPLICATION, DIST_UNIT
//                            réglages de la page Clay (téléphone → montre)
//   SUNRISE_0..2, SUNSET_0..2  lever / coucher du soleil (s Unix) des 3 prochains jours
//   TEMP_UNIT                réglage Clay utilisé seulement par le téléphone
// ATTENTION : dans le SDK, les MESSAGE_KEY_xxx sont des variables (extern), pas des
// constantes : on ne peut pas les mettre dans un initialiseur statique. Le tableau
// est donc rempli au démarrage par init_forecast_keys().
#define FORECAST_KEY_COUNT 10
static uint32_t FORECAST_KEYS[FORECAST_KEY_COUNT];

// Clés lever / coucher du soleil (même raison : variables, remplies au démarrage)
static uint32_t SUNRISE_KEYS[3], SUNSET_KEYS[3];

static void init_forecast_keys(void) {
  SUNRISE_KEYS[0] = MESSAGE_KEY_SUNRISE_0; SUNSET_KEYS[0] = MESSAGE_KEY_SUNSET_0;
  SUNRISE_KEYS[1] = MESSAGE_KEY_SUNRISE_1; SUNSET_KEYS[1] = MESSAGE_KEY_SUNSET_1;
  SUNRISE_KEYS[2] = MESSAGE_KEY_SUNRISE_2; SUNSET_KEYS[2] = MESSAGE_KEY_SUNSET_2;
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
#define PERSIST_DIST_MI   4

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
#define FC_SEP_Y0     1            // séparateurs verticaux : début / fin dans la cellule
#define FC_SEP_Y1     27
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
static int  s_icon_col = CFG_ICON_COLOR; // 0 = icônes monochromes, 1 = icônes en couleurs
static int  s_comp     = CFG_COMPLICATION; // 0 pas, 1 FC, 2 distance, 3 soleil, 4 secondes
static int  s_dist_mi  = CFG_DIST_MILES; // 0 = kilomètres, 1 = miles

static char s_weather_trend[64] = "METEO"; // « VILLE : TENDANCE » reçu du téléphone

static int  s_steps           = -1;   // -1 = indisponible (affiche -----)
static int  s_hr              = 0;   // fréquence cardiaque (0 = pas de mesure)
static int  s_dist            = -1;  // distance du jour en mètres (-1 = indisponible)
static time_t s_sunrise[3];          // levers du soleil (s Unix, 0 = inconnu)
static time_t s_sunset[3];           // couchers du soleil
static int  s_batt_pct        = 100;

// ── Custom fonts (loaded in window_load) ──────────────────────────────────
static GFont s_font_d14_time   = NULL;  // DSEG7 Classic Bold 48 px : heure HH:MM
static GFont s_font_d14_time38 = NULL;  // DSEG7 Classic Bold 38 px : chargée mais NON UTILISÉE (pas d'affichage des secondes)
static GFont s_font_d7_date    = NULL;  // DSEG7 Classic Bold 20 px : date
static GFont s_font_d7_comp    = NULL;  // DSEG7 Classic Bold 20 px : chiffres allumés de la complication
static GFont s_font_comp_reg   = NULL;  // DSEG7 Classic Regular 20 px : chiffres éteints (fantômes) de la complication
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
// Respects the CFG_GHOST_ENABLED config toggle.
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

static void lcd_text(GContext *ctx, const char *ghost, const char *real,
                     GFont font, GRect r, GColor gc, GColor rc,
                     GTextAlignment align) {
  if (CFG_GHOST_ENABLED) {
    graphics_context_set_text_color(ctx, gc);
    graphics_draw_text(ctx, ghost, font, r,
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

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w;
  int H = bounds.size.h;

#define PX(x) ((x)*W/200)
#define PY(y) ((y)*H/228)

  // ── Colors ────────────────────────────────────────────────────────────
  GColor col_bg    = color_from_int(CFG_COLOR_BG);
  GColor col_fg    = color_from_int(CFG_COLOR_FG);
  GColor col_ghost = color_from_int(CFG_COLOR_GHOST);
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
  int y_btn     = PY(19);    // haut de la ligne de prévisions du haut
  // La ligne de prévisions du haut occupe y_btn..y_lcd (≈ 33 px), celle du bas
  // y_fc..y_rs2 (≈ 32 px) : hauteurs quasi identiques.
  int y_lcd     = PY(52);    // LCD outer frame
  int y_in      = PY(55);    // white panel
  int y_dr      = PY(57);    // date / comp row
  int dr_h      = PY(30);    // 2 px border + 2 px air around the 21 px ink
  int y_time    = PY(86);    // time row
  int y_info    = PY(144);   // info strip separator
  int y_in_end  = PY(156);   // panel end (bandeau d'info plus bas = bande de texte plus haute)
  int y_lcd_end = PY(173);   // fin du cadre (la bande des jours est entre y_in_end et y_lcd_end)
  int y_fc      = PY(175);   // 2e ligne de prévisions
  int y_rs2     = PY(207);   // bas du fond noir des prévisions / haut de la bande rouge inférieure
  int y_ban     = PY(207);   // haut de la zone ville + tendance (texte jaune)

  // ── X anchors ─────────────────────────────────────────────────────────
  int ring_s = PX(3);                 // thin red side rails
  int lx = PX(6),  lw = W - PX(12);   // LCD outer frame
  int ix = PX(10), iw = W - PX(20);   // white panel
  int x_l = ix + PX(4);               // content left
  int x_r = ix + iw - PX(4);          // content right
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
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(col_ghost, col_fg));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, GRect(comp_x, y_dr, comp_w, dr_h), PX(5));
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
    if (CFG_GHOST_ENABLED && CFG_GHOST_COMP_ENABLED) {
      graphics_context_set_text_color(ctx, col_ghost);
      graphics_draw_text(ctx, ghost, f_comp_g, all_r, GTextOverflowModeFill,
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
      if (CFG_GHOST_ENABLED) {
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
        if (k >= lit && !CFG_GHOST_ENABLED) continue;
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
      } else if (CFG_GHOST_ENABLED) {
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
    GColor c_lbl   = color_from_int(CFG_COLOR_FORECAST_LABEL);
    GColor c_icon  = color_from_int(CFG_COLOR_FORECAST_ICON);
    GColor c_ghost = color_from_int(CFG_COLOR_FORECAST_GHOST);
    GColor c_temp  = color_from_int(CFG_COLOR_FORECAST_TEMP);

    for (int row = 0; row < FC_ROWS; row++) {
      int y0    = (row == 0) ? y_btn : y_fc;   // haut de la cellule
      int y_ico = y0 + FC_ICO_DY;

      for (int c = 0; c < FC_PER_ROW; c++) {
        int i  = row * FC_PER_ROW + c;
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
        if (CFG_GHOST_ENABLED && CFG_GHOST_FORECAST) {
          graphics_context_set_text_color(ctx, c_ghost);
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

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *w) {
  s_font_d14_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME48));
  s_font_d14_time38 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_TIME38));
  s_font_d7_date  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_DATE20));
  s_font_d7_comp = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_COMP20));
  s_font_comp_reg = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DSEG_COMP20_REG));
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
  if (s_font_d14_time) { fonts_unload_custom_font(s_font_d14_time); s_font_d14_time = NULL; }
  if (s_font_d14_time38) { fonts_unload_custom_font(s_font_d14_time38); s_font_d14_time38 = NULL; }
  if (s_font_d7_date)  { fonts_unload_custom_font(s_font_d7_date);  s_font_d7_date  = NULL; }
  if (s_font_d7_comp)  { fonts_unload_custom_font(s_font_d7_comp);  s_font_d7_comp  = NULL; }
  if (s_font_comp_reg) { fonts_unload_custom_font(s_font_comp_reg); s_font_comp_reg = NULL; }
  if (s_font_weather)  { fonts_unload_custom_font(s_font_weather);  s_font_weather  = NULL; }
}

// ── App lifecycle ─────────────────────────────────────────────────────────
// Relit les réglages mémorisés (sinon valeurs par défaut de config.h)
static void load_settings(void) {
  if (persist_exists(PERSIST_LANG))     s_lang     = (persist_read_int(PERSIST_LANG) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_DATE_MDY)) s_date_mdy = (persist_read_int(PERSIST_DATE_MDY) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_TIME12))   s_time12   = (persist_read_int(PERSIST_TIME12) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_ICON_COL)) s_icon_col = (persist_read_int(PERSIST_ICON_COL) == 1) ? 1 : 0;
  if (persist_exists(PERSIST_COMP))     s_comp     = persist_read_int(PERSIST_COMP);
  if (persist_exists(PERSIST_DIST_MI))  s_dist_mi  = (persist_read_int(PERSIST_DIST_MI) == 1) ? 1 : 0;
  if (s_comp < 0 || s_comp > 4) s_comp = CFG_COMPLICATION;
}

static void init(void) {
  init_forecast_keys();
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
    app_message_open(in_max  < 512u ? in_max  : 512u,
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
