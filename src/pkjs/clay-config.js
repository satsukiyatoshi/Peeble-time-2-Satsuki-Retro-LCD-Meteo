/*
 * Satsuki Retro LCD Meteo – page de réglages (Clay), ouverte depuis l'appli Pebble du
 * téléphone : Watchfaces > Satsuki Retro LCD Meteo > Réglages.
 *
 * Tous les textes affichés sont bilingues « français / English ».
 *
 * Les « defaultValue » ci-dessous doivent rester en accord avec les valeurs par
 * défaut de la montre (CFG_* dans src/c/config.h) et de index.js :
 *   langue "0" = français, date "0" = JJ-MM, heure "0" = 24 h,
 *   icônes "1" = couleurs, complication "0" = nombre de pas,
 *   unité de distance "0" = kilomètres, température "0" = °C.
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
      }
    ]
  },
  { "type": "submit", "defaultValue": "Enregistrer / Save" }
];
