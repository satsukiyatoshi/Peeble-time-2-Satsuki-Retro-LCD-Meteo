/*
 * Satsuki Retro LCD Meteo – page de réglages (Clay), ouverte depuis l'appli Pebble du
 * téléphone : Watchfaces > Satsuki Retro LCD Meteo > Réglages.
 *
 * Tous les textes affichés sont bilingues « français / English ».
 *
 * Les « defaultValue » ci-dessous doivent rester en accord avec les valeurs par
 * défaut de la montre (CFG_* dans src/c/config.h) et de index.js :
 *   langue "0" = français, date "0" = JJ-MM, heure "0" = 24 h,
 *   icônes "1" = couleurs, ghost "0" = défaut, chiffres "0" = 7 segments Classic, complication "0" = nombre de pas,
 *   unité de distance "0" = kilomètres, température "0" = °C,
 *   période météo "1" = 1 h, complications du haut et du bas "0" = météo horaire.
 */
module.exports = [
  { "type": "heading", "defaultValue": "Satsuki Retro LCD Meteo" },
  { "type": "text", "defaultValue": "Réglages / Settings" },
  {
    "type": "section",
    "items": [
      {
        "type": "select",
        "messageKey": "LANGUAGE",
        "label": "Langue / Language",
        "defaultValue": "0",
        "options": [
          { "label": "Français / French", "value": "0" },
          { "label": "English / Anglais", "value": "1" }
        ]
      },
      {
        "type": "select",
        "messageKey": "DATE_FORMAT",
        "label": "Format de la date / Date format",
        "defaultValue": "0",
        "options": [
          { "label": "JJ-MM / DD-MM (07-10)", "value": "0" },
          { "label": "MM-JJ / MM-DD (10-07)", "value": "1" }
        ]
      },
      {
        "type": "select",
        "messageKey": "TIME_FORMAT",
        "label": "Format de l'heure / Time format",
        "defaultValue": "0",
        "options": [
          { "label": "24 h", "value": "0" },
          { "label": "12 h (A / P)", "value": "1" }
        ]
      },
      {
        "type": "select",
        "messageKey": "ICON_STYLE",
        "label": "Icônes météo / Weather icons",
        "defaultValue": "1",
        "options": [
          { "label": "Monochrome (jaune / yellow)", "value": "0" },
          { "label": "Couleurs / Colors",           "value": "1" }
        ]
      },
      {
        "type": "select",
        "messageKey": "GHOST_LEVEL",
        "label": "Segments éteints (ghost) / Unlit segments (ghost)",
        "defaultValue": "0",
        "options": [
          { "label": "Défaut / Default",    "value": "0" },
          { "label": "Léger / Light",       "value": "1" },
          { "label": "Désactivé / Off",     "value": "2" }
        ]
      },
	  {
        "type": "select",
        "messageKey": "RED_RING_LATERAL",
        "label": "Bandes rouges latérales / Red side stripes",
        "defaultValue": "0",
        "options": [
          { "label": "Afficher / Show", "value": "1" },
          { "label": "Cacher / Hide", "value": "0" }
        ]
      },
      {
        "type": "select",
        "messageKey": "FONT_STYLE",
        "label": "Style des chiffres / Digit style",
        "defaultValue": "0",
        "options": [
          { "label": "7 segments : Classic (défaut / default)", "value": "0" },
          { "label": "7 segments : Classic Mini",               "value": "1" },
          { "label": "7 segments : Modern",                     "value": "2" },
          { "label": "7 segments : Modern Mini",                "value": "3" },
          { "label": "14 segments : Classic",                   "value": "4" },
          { "label": "14 segments : Classic Mini",              "value": "5" },
          { "label": "14 segments : Modern",                    "value": "6" },
          { "label": "14 segments : Modern Mini",               "value": "7" }
        ]
      },
      {
        "type": "select",
        "messageKey": "COMPLICATION",
        "label": "Complication (case à côté de la date / box next to the date)",
        "defaultValue": "0",
        "options": [
          { "label": "Pas / Steps",                                 "value": "0" },
          { "label": "Fréquence cardiaque / Heart rate",            "value": "1" },
          { "label": "Distance parcourue / Distance walked",        "value": "2" },
          { "label": "Lever / coucher du soleil / Sunrise / sunset", "value": "3" },
          { "label": "Secondes / Seconds",                          "value": "4" }
        ]
      },
      {
        "type": "select",
        "messageKey": "DIST_UNIT",
        "label": "Unité de distance (complication Distance) / Distance unit (Distance complication)",
        "defaultValue": "0",
        "options": [
          { "label": "Kilomètres / Kilometers", "value": "0" },
          { "label": "Miles",                   "value": "1" }
        ]
      },
      {
        "type": "select",
        "messageKey": "TEMP_UNIT",
        "label": "Unité de température / Temperature unit",
        "defaultValue": "0",
        "options": [
          { "label": "°C (Celsius)",    "value": "0" },
          { "label": "°F (Fahrenheit)", "value": "1" }
        ]
      },
      {
        "type": "select",
        "messageKey": "WEATHER_PERIOD",
        "label": "Période météo (météo horaire) / Weather period (hourly weather)",
        "defaultValue": "1",
        "options": [
          { "label": "1 h (défaut / default)", "value": "1" },
          { "label": "2 h", "value": "2" },
          { "label": "3 h", "value": "3" },
          { "label": "4 h", "value": "4" }
        ]
      },
      {
        "type": "select",
        "messageKey": "COMP_TOP",
        "label": "Complication au-dessus de l'heure / Complication above the time",
        "defaultValue": "0",
        "options": [
          { "label": "Météo horaire / Hourly weather",                         "value": "0" },
          { "label": "Tendance météo 3 jours (6 si les 2) / 3-day trend (6 if both)", "value": "4" },
          { "label": "Lever + coucher du soleil / Sunrise + sunset",           "value": "3" },
          { "label": "Rythme cardiaque + distance / Heart rate + distance",    "value": "1" },
          { "label": "Rythme cardiaque + calories / Heart rate + calories",    "value": "5" },
          { "label": "Rythme cardiaque + pas / Heart rate + steps",            "value": "6" },
          { "label": "Distance + calories / Distance + calories",              "value": "7" },
          { "label": "Pas + distance / Steps + distance",                      "value": "8" },
          { "label": "Calories + pas / Calories + steps",                      "value": "2" }
        ]
      },
      {
        "type": "select",
        "messageKey": "COMP_BOTTOM",
        "label": "Complication sous l'heure / Complication below the time",
        "defaultValue": "0",
        "options": [
          { "label": "Météo horaire / Hourly weather",                         "value": "0" },
          { "label": "Tendance météo 3 jours (6 si les 2) / 3-day trend (6 if both)", "value": "4" },
          { "label": "Lever + coucher du soleil / Sunrise + sunset",           "value": "3" },
          { "label": "Rythme cardiaque + distance / Heart rate + distance",    "value": "1" },
          { "label": "Rythme cardiaque + calories / Heart rate + calories",    "value": "5" },
          { "label": "Rythme cardiaque + pas / Heart rate + steps",            "value": "6" },
          { "label": "Distance + calories / Distance + calories",              "value": "7" },
          { "label": "Pas + distance / Steps + distance",                      "value": "8" },
          { "label": "Calories + pas / Calories + steps",                      "value": "2" }
        ]
      }
    ]
  },
  { "type": "submit", "defaultValue": "Enregistrer / Save" }
];
