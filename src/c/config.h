#pragma once
/*
 * Satsuki Retro LCD Meteo – CONFIGURATION DE LA MONTRE (modifier ici, puis recompiler)
 *
 * La langue, le format de la date et l'unité de température se choisissent dans
 * la page de réglages de l'appli Pebble du téléphone (voir src/pkjs/clay-config.js).
 * Ici, CFG_LANGUAGE, CFG_DATE_MDY, CFG_TIME_12H, CFG_ICON_COLOR, CFG_GHOST_LEVEL, CFG_FONT_STYLE, CFG_COMPLICATION et CFG_DIST_MILES ne sont que les valeurs PAR DÉFAUT, utilisées
 * tant que rien n'a été choisi dans cette page. Tout le reste se règle dans ce
 * fichier avant `pebble build`. La fréquence de reprise après échec se règle dans
 * src/pkjs/config.js.
 *
 * Les couleurs sont au format 0xRRGGBB. La Pebble Time 2 n'affiche que 64
 * couleurs : chaque valeur est arrondie à la couleur la plus proche.
 */

// ── Langue, formats et complication : VALEURS PAR DÉFAUT ────────────────────
// Modifiables ensuite depuis la page de réglages du téléphone (qui l'emporte,
// et dont le choix est mémorisé par la montre). Gardez clay-config.js en accord.
//
// Langue : 0 = français ; 1 = English. Elle change :
//   - « SEM » / « WEEK » et les initiales des jours (L M M J V S D / M T W T F S S) ;
//     en anglais, l'année et la semaine s'écrivent sans espaces autour du tiret
//     (« 2026-WEEK 41 ») pour compenser la longueur de « WEEK » ;
//   - le bandeau du bas (PLUIE → RAIN, ORAGE → STORM, ENSOLEILLE → SUNNY…) ;
//   - le nom de la ville (ex. LONDON et non LONDRES), choisi par le téléphone.
#define CFG_LANGUAGE           0

// Format de l'heure : 0 = 24 h ; 1 = 12 h. En 12 h :
//   - un petit « P » (en haut) et « A » (en bas) apparaissent à gauche de l'heure,
//     les deux en segments éteints, celui de la période en cours allumé ;
//   - les libellés des prévisions deviennent « 10HA » / « 10HP » (au lieu de « 10H ») ;
//   - l'heure du lever / coucher du soleil est aussi en 12 h.
// En 24 h : aucun indicateur A/P. Le réglage 12 h / 24 h du téléphone est ignoré.
#define CFG_TIME_12H           0

// Format de la date : 0 = JJ-MM (ex. 07-10) ; 1 = MM-JJ (ex. 10-07).
#define CFG_DATE_MDY           0

// Icônes météo : 0 = monochrome (couleur CFG_COLOR_FORECAST_ICON, jaune) ;
// 1 = en couleurs (couleurs CFG_COLOR_ICON_* plus bas).
#define CFG_ICON_COLOR         1

// Style des chiffres (polices DSEG du dossier resources/fonts). Concerne l'heure,
// la date et la complication :
//   0 = 7 segments Classic ; 1 = 7 segments Classic Mini ; 2 = 7 segments Modern ;
//   3 = 7 segments Modern Mini ; 4 = 14 segments Classic ; 5 = 14 segments Classic Mini ;
//   6 = 14 segments Modern ; 7 = 14 segments Modern Mini.
#define CFG_FONT_STYLE         0

// Complication (case à droite de la date) :
//   0 = nombre de pas ; 1 = fréquence cardiaque ; 2 = distance parcourue ;
//   3 = prochain lever / coucher du soleil (▲ = lever, ▼ = coucher) ;
//   4 = secondes (le cadran se met alors à jour chaque seconde : plus de batterie).
// La distance occupe les 4 chiffres de gauche de la case, suivie d'un grand K (kilomètres) ou M (miles) à la place du 5e chiffre.
#define CFG_COMPLICATION       0

// Unité de la distance (complication 2) : 0 = kilomètres ; 1 = miles.
#define CFG_DIST_MILES         0

// L'unité de température (°C par défaut, °F possible) ne figure pas ici : elle
// se choisit uniquement dans la page de réglages du téléphone.

// ── Complications des deux rangées de prévisions (au-dessus / sous l'heure) ──
// VALEURS PAR DÉFAUT (la page de réglages du téléphone l'emporte) :
//   0 = météo horaire (5 colonnes) ; 1 = fréquence cardiaque + distance ;
//   2 = calories + pas ; 3 = lever + coucher du soleil (prochains) ;
//   4 = tendance météo sur 3 jours (jour + date, icône de la journée, min / max) ; les deux rangées
//       en tendance se suivent : 6 jours (J à J+2, puis J+3 à J+5) ;
//   5 = FC + calories ; 6 = FC + pas ; 7 = distance + calories ; 8 = pas + distance.
// Si les DEUX rangées sont en météo horaire, elles se suivent (H+1…H+5, puis H+6…H+10) ;
// si une seule l'est, elle affiche H+1…H+5.
// La période météo (1, 2, 3 ou 4 h entre deux colonnes) ne se règle que dans la page
// de réglages du téléphone (1 h par défaut) : voir src/pkjs/clay-config.js.
#define CFG_COMP_TOP           0
#define CFG_COMP_BOTTOM        0
// Calories affichées : 0 = actives seulement ; 1 = actives + au repos.
#define CFG_CALORIES_TOTAL     0

// ── Mode test des logos météo (émulateur) ─────────────────────────────────
// 0 = normal (vraies prévisions) ; 1 = affiche 10 logos de test (les 10 colonnes) ;
// 2 = décalé d'un cran pour voir le 11e (goutte + éclair), etc. Remettre 0 avant
// l'installation !
#define CFG_ICON_TEST          0

// ── Segments fantômes (segments LCD éteints) ──────────────────────────────
// Niveau par défaut (modifiable ensuite depuis la page de réglages du téléphone) :
//   0 = normal ; 1 = léger (plus discret, couleurs CFG_COLOR_*_GHOST_LIGHT) ; 2 = désactivé.
#define CFG_GHOST_LEVEL        0
#define CFG_GHOST_COMP_ENABLED 1     // 1 = aussi dans la case de complication (sauf niveau 2)
#define CFG_GHOST_FORECAST     0     // 1 = aussi derrière les icônes météo (2 lignes de prévisions ; sauf niveau 2)

// ── Rétroéclairage (secousse) – NON UTILISÉ pour l'instant ────────────────
// Ces deux réglages ne sont lus nulle part dans main.c (aucun abonnement à la
// secousse) : les modifier n'a aucun effet.
#define CFG_BACKLIGHT_CUSTOM   1     // 1 = utiliser la couleur ci-dessous, 0 = blanc du système
#define CFG_COLOR_BACKLIGHT    0xFFFFFF

// ── Libellés (32 caractères maximum, majuscules sans accent conseillées) ──
// Si le libellé de droite se termine par un mot de 1 ou 2 caractères
// (ex. « TIME 2 »), ce dernier mot est affiché en bleu, comme sur la Casio.
// Le libellé central est affiché en jaune (couleur fixe dans main.c).
#define CFG_LABEL_TOP_LEFT     "RETRO"
#define CFG_LABEL_TOP_RIGHT    "TIME 2"
#define CFG_LABEL_CENTER       "SATSUKI" // au centre de la ligne du haut

// ── Affichage des parties latérales de l'anneaux rouge.
#define CFG_RED_RING_LATERAL   0  // 1 affichées, 0 masquées

// ── Couleurs ──────────────────────────────────────────────────────────────
#define CFG_COLOR_BG           0xFFFFFF  // fond de l'écran LCD
#define CFG_COLOR_FG           0x000055  // chiffres, bordures, libellés du LCD
#define CFG_COLOR_ACCENT       0xFF0000  // anneau rouge du boîtier

#define CFG_COLOR_GHOST        0xAAAAFF  // segments éteints (plus clair = plus discret)
#define CFG_COLOR_GHOST_LIGHT  0xAAFFFF  // segments éteints, niveau « léger » (la palette de la montre n'a que
                                         // 4 niveaux par couleur : 0xAAFFFF est le plus clair visible sur fond blanc)
#define CFG_COLOR_TIME2_BG     0xFFFFFF  // bande derrière date + complication (= fond : sans effet)
#define CFG_COLOR_LABEL_TOP    0xFFFFFF  // libellés de gauche et de droite en haut (ex. CASIO / TIME 2)

// Couleurs des icônes météo en mode « couleurs » (CFG_ICON_COLOR = 1)
#define CFG_COLOR_ICON_SUN     0xFFFF00  // soleil (et morceaux de soleil derrière un nuage) : jaune
#define CFG_COLOR_ICON_MOON    0xFFFF55  // lune (et morceaux de lune derrière un nuage) : jaune clair
#define CFG_COLOR_ICON_RAIN    0x55AAFF  // pluie : bleu clair
#define CFG_COLOR_ICON_SNOW    0xFFFFFF  // neige : blanc
#define CFG_COLOR_ICON_CLOUD   0xAAAAAA  // nuages et brouillard : gris clair
#define CFG_COLOR_ICON_BOLT    0xFFFF00  // éclair : jaune

// Prévisions météo (2 lignes de 5 colonnes, sur le boîtier noir)
#define CFG_COLOR_FORECAST_LABEL 0x55AAFF  // « 14H », « 15H »…
#define CFG_COLOR_FORECAST_ICON  0xFFFF00  // icône météo allumée
#define CFG_COLOR_FORECAST_GHOST 0xAAAAFF  // segments d'icône éteints et séparateurs
#define CFG_COLOR_FORECAST_GHOST_LIGHT 0x5555AA  // segments d'icône éteints, niveau « léger » (plus sombre = plus discret sur fond noir)
#define CFG_COLOR_FORECAST_TEMP  0xFFFFFF  // température (et valeurs des complications)

// Icônes des complications des rangées de prévisions (mode « couleurs » ; en monochrome :
// CFG_COLOR_FORECAST_ICON). Le soleil (lever / coucher) utilise CFG_COLOR_ICON_SUN.
#define CFG_COLOR_ICON_HEART   0xFF0000  // cœur (fréquence cardiaque)
#define CFG_COLOR_ICON_FLAME   0xFF5500  // flamme (calories)
#define CFG_COLOR_ICON_STEPS   0x55FF55  // empreintes (pas)
#define CFG_COLOR_ICON_PIN     0xFFAA00  // repère (distance)
