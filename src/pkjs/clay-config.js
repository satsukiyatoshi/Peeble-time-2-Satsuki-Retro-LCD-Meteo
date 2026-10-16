/*
 * Satsuki Retro LCD Meteo – page de réglages (Clay), ouverte depuis l'appli Pebble du
 * téléphone : Watchfaces > Satsuki Retro LCD Meteo > Réglages.
 *
 * La page est monolingue : module.exports est une FONCTION qui reçoit la langue (0 = français,
 * 1 = English) et renvoie la configuration dans cette seule langue. index.js l'appelle à chaque
 * ouverture (buildClayConfig) avec le réglage LANGUAGE mémorisé ; ce réglage est le tout premier
 * de la page et sert à la fois pour la montre et pour la page (il prend effet à la réouverture).
 *
 * Les « defaultValue » ci-dessous doivent rester en accord avec les valeurs par défaut de la montre
 * (CFG_* dans src/c/config.h) et de index.js :
 *   langue "0" = français, date "0" = JJ-MM, heure "0" = 24 h, icônes "1" = couleurs, ghost "0" = défaut,
 *   chiffres "0" = 7 segments Classic, complication "0" = nombre de pas, unité de distance "0" = kilomètres,
 *   température "0" = °C, période météo "1" = 1 h, rangées haut et bas "0" = météo horaire,
 *   mouvement du poignet "0" = aucune action, cycles 1 à 5 : tout « Inchangé » ("-1").
 *
 * Organisation : Langue / Affichage / Unités / Complications / Localisations (UN seul cadre : choix par affichage +
 * 5 localisations enregistrées, une ligne chacune) / Mouvement du poignet + cycles / Sets.
 * Localisations : LOC_SIDE, LOC_TOP, LOC_BOTTOM choisissent la localisation de la case à côté de la date (n° 1), de la
 * rangée du haut (n° 2) et de la rangée du bas (n° 3) (\"-1\" = position GPS actuelle, \"0\"..\"4\" = localisation
 * enregistrée) ; leurs options sont reconstruites à chaque ouverture par buildClayConfig (index.js). Le bandeau du bas
 * n'a plus de réglage : « ville + tendance » si tous les affichages météo partagent la même localisation, sinon les
 * numéros d'affichage + leur ville (ex. « 1|2 BORDEAUX 3|PARIS »). Chaque localisation k = 1..5 tient dans UN champ
 * LOCk (« Ville, Pays » · « latitude, longitude » · « GPS ») résolu à l'enregistrement par processLocations (index.js) ;
 * le résultat (texte normalisé) apparaît à la réouverture de la page.
 * Les éléments du groupe "cycles" ne sont visibles que si « Mouvement du poignet » = cycle des
 * complications (clayCustom dans index.js).
 * Les champs SET_NAME_NEW / SET_REPLACE / SET_DELETE / SET_LOAD / SET_LOAD_FLAG sont traités puis effacés
 * par index.js (processSetActions) ; SET_LOAD, SET_REPLACE et SET_DELETE sont reconstruits (et retirés
 * quand ils ne servent à rien) à chaque ouverture par buildClayConfig.
 */
module.exports = function(lang) {
  var EN = (lang === 1);
  function t(fr, en) { return EN ? en : fr; }
  function opt(value, fr, en) { return { "label": t(fr, en), "value": String(value) }; }

  // Complication de la case à côté de la date : valeurs 0..10 (COMPLICATION)
  var SIDE_OPTS = [
    opt(0,  "Pas",                                      "Steps"),
    opt(1,  "Fréquence cardiaque",                      "Heart rate"),
    opt(2,  "Distance parcourue",                       "Distance walked"),
    opt(3,  "Lever / coucher du soleil",                "Sunrise / sunset"),
    opt(4,  "Secondes",                                 "Seconds"),
    opt(5,  "Pourcentage de batterie",                  "Battery percentage"),
    opt(6,  "Températures min / max du jour",           "Today's min / max temperature"),
    opt(7,  "Humidité",                                 "Humidity"),
    opt(8,  "Indice UV",                                "UV index"),
    opt(9,  "Qualité de l'air",                         "Air quality"),
    opt(10, "Vitesse du vent",                          "Wind speed")
  ];
  // Complications des rangées au-dessus / sous l'heure : valeurs 0..10 (COMP_TOP / COMP_BOTTOM), même ordre que CK_* dans main.c
  var ROW_OPTS = [
    opt(0,  "Météo horaire",                            "Hourly weather"),
    opt(4,  "Tendance météo 3 jours (6 si les 2 rangées)", "3-day weather trend (6 if both rows)"),
    opt(3,  "Lever + coucher du soleil",                "Sunrise + sunset"),
    opt(1,  "Rythme cardiaque + distance",              "Heart rate + distance"),
    opt(5,  "Rythme cardiaque + calories",              "Heart rate + calories"),
    opt(6,  "Rythme cardiaque + pas",                   "Heart rate + steps"),
    opt(7,  "Distance + calories",                      "Distance + calories"),
    opt(8,  "Pas + distance",                           "Steps + distance"),
    opt(2,  "Calories + pas",                           "Calories + steps"),
    opt(9,  "Indice UV + qualité de l'air",             "UV index + air quality"),
    opt(10, "Humidité + vitesse du vent",               "Humidity + wind speed")
  ];
  var UNCHANGED = opt(-1, "— Inchangé", "— Unchanged");

  // Les 5 cycles : cycle n°k = une complication pour la case date, une pour la rangée du haut, une pour
  // la rangée du bas (« Inchangé » = l'emplacement ne bouge pas). Clés CYC1_SIDE … CYC5_BOT.
  function cycleItems() {
    var items = [
      { "type": "heading", "size": 4, "group": "cycles", "defaultValue": t("Cycles de complications", "Complication cycles") },
      { "type": "text", "group": "cycles", "defaultValue": t(
          "Chaque mouvement du poignet applique le cycle suivant (1 → 2 → … → 5 → 1). « Inchangé » : l'emplacement ne bouge pas ; un cycle entièrement « Inchangé » est sauté.",
          "Each wrist movement applies the next cycle (1 → 2 → … → 5 → 1). « Unchanged »: the slot stays as is; a cycle that is entirely « Unchanged » is skipped.") }
    ];
    for (var k = 1; k <= 5; k++) {
      items.push({ "type": "heading", "size": 5, "group": "cycles", "defaultValue": t("Cycle ", "Cycle ") + k });
      items.push({ "type": "select", "group": "cycles", "messageKey": "CYC" + k + "_SIDE", "label": t("Case à côté de la date", "Box next to the date"),
                   "defaultValue": "-1", "options": [UNCHANGED].concat(SIDE_OPTS) });
      items.push({ "type": "select", "group": "cycles", "messageKey": "CYC" + k + "_TOP",  "label": t("Au-dessus de l'heure", "Above the time"),
                   "defaultValue": "-1", "options": [UNCHANGED].concat(ROW_OPTS) });
      items.push({ "type": "select", "group": "cycles", "messageKey": "CYC" + k + "_BOT",  "label": t("Sous l'heure", "Below the time"),
                   "defaultValue": "-1", "options": [UNCHANGED].concat(ROW_OPTS) });
    }
    return items;
  }

  // Un seul cadre : d'où vient la météo de chaque affichage (n° 1 = case date, 2 = rangée du haut, 3 = rangée du bas)
  // puis les 5 localisations enregistrées, UNE ligne chacune (ville, coordonnées ou « GPS »).
  function locationItems() {
    var gps = opt(-1, "Position actuelle (GPS)", "Current position (GPS)");
    var items = [
      { "type": "heading", "size": 4, "defaultValue": t("Localisations", "Locations") },
      { "type": "text", "defaultValue": t(
          "Choisissez d'où vient la météo de chaque affichage : la position GPS actuelle ou l'une de vos 5 localisations. Si le GPS est indisponible, la dernière position connue est utilisée.",
          "Choose where each display's weather comes from: the current GPS position or one of your 5 locations. If GPS is unavailable, the last known position is used.") },
      { "type": "select", "messageKey": "LOC_SIDE", "label": t("1 · Case à côté de la date", "1 · Box next to the date"),
        "defaultValue": "-1", "options": [ gps ] },
      { "type": "select", "messageKey": "LOC_TOP", "label": t("2 · Au-dessus de l'heure", "2 · Above the time"),
        "defaultValue": "-1", "options": [ gps ] },
      { "type": "select", "messageKey": "LOC_BOTTOM", "label": t("3 · Sous l'heure", "3 · Below the time"),
        "defaultValue": "-1", "options": [ gps ] },
      { "type": "text", "defaultValue": t(
          "Bandeau du bas : « ville + tendance » si tous les affichages utilisent la même localisation ; sinon les numéros des affichages suivis de leur ville (ex. « 1|2 BORDEAUX 3|PARIS »).",
          "Bottom banner: « city + trend » when all displays use the same location; otherwise the display numbers followed by their city (e.g. « 1|2 BORDEAUX 3|PARIS »).") },
      { "type": "text", "defaultValue": t(
          "Une ligne par localisation : « Ville, Pays » (ex. Bordeaux, France), OU des coordonnées (ex. 44.84, -0.58), OU « GPS » pour y enregistrer la position actuelle. Tout est traité à l'enregistrement : rouvrez la page pour voir le résultat. Ligne vide = localisation supprimée.",
          "One line per location: « City, Country » (e.g. Bordeaux, France), OR coordinates (e.g. 44.84, -0.58), OR « GPS » to store the current position. Everything is processed on save: reopen the page to see the result. Empty line = location deleted.") },
      { "type": "text", "id": "locsInfo", "defaultValue": "—" }
    ];
    var ph = [ "Bordeaux, France", "44.84, -0.58", "GPS", "", "" ];
    for (var k = 1; k <= 5; k++) {
      items.push({ "type": "input", "messageKey": "LOC" + k, "label": t("Localisation ", "Location ") + k, "defaultValue": "",
                   "attributes": { "placeholder": ph[k - 1], "type": "text" } });
    }
    return items;
  }

  return [
    { "type": "heading", "defaultValue": "Satsuki Retro LCD Meteo" },

    // ── Langue (toujours en premier) ──────────────────────────────────────
    {
      "type": "section",
      "items": [
        {
          "type": "select",
          "messageKey": "LANGUAGE",
          "label": "Langue / Language",
          "description": t("Langue de la montre et de cette page. La page change après « Enregistrer » et réouverture.",
                           "Language of the watch and of this page. The page changes after « Save » and reopening."),
          "defaultValue": "0",
          "options": [
            { "label": "Français", "value": "0" },
            { "label": "English",  "value": "1" }
          ]
        }
      ]
    },

    // ── Affichage ─────────────────────────────────────────────────────────
    {
      "type": "section",
      "items": [
        { "type": "heading", "size": 4, "defaultValue": t("Affichage", "Display") },
        {
          "type": "select",
          "messageKey": "DATE_FORMAT",
          "label": t("Format de la date", "Date format"),
          "defaultValue": "0",
          "options": [
            opt(0, "JJ-MM (07-10)", "DD-MM (07-10)"),
            opt(1, "MM-JJ (10-07)", "MM-DD (10-07)")
          ]
        },
        {
          "type": "select",
          "messageKey": "TIME_FORMAT",
          "label": t("Format de l'heure", "Time format"),
          "defaultValue": "0",
          "options": [
            opt(0, "24 h", "24 h"),
            opt(1, "12 h (A / P)", "12 h (A / P)")
          ]
        },
        {
          "type": "select",
          "messageKey": "FONT_STYLE",
          "label": t("Style des chiffres", "Digit style"),
          "defaultValue": "0",
          "options": [
            opt(0, "7 segments : Classic (défaut)", "7 segments: Classic (default)"),
            opt(1, "7 segments : Classic Mini",     "7 segments: Classic Mini"),
            opt(2, "7 segments : Modern",           "7 segments: Modern"),
            opt(3, "7 segments : Modern Mini",      "7 segments: Modern Mini"),
            opt(4, "14 segments : Classic",         "14 segments: Classic"),
            opt(5, "14 segments : Classic Mini",    "14 segments: Classic Mini"),
            opt(6, "14 segments : Modern",          "14 segments: Modern"),
            opt(7, "14 segments : Modern Mini",     "14 segments: Modern Mini"),
            opt(8, "7 segments : spécial D",        "7 segments: special D")
          ]
        },
        {
          "type": "select",
          "messageKey": "ICON_STYLE",
          "label": t("Icônes météo", "Weather icons"),
          "defaultValue": "1",
          "options": [
            opt(0, "Monochrome (jaune)", "Monochrome (yellow)"),
            opt(1, "Couleurs",           "Colors")
          ]
        },
        {
          "type": "select",
          "messageKey": "BATT_STYLE",
          "label": t("Pile et icônes d'indicateurs", "Battery & indicator icons"),
          "description": t("Segments de la pile et icônes des complications (humidité, UV, air, vent).",
                           "Battery segments and complication icons (humidity, UV, air, wind)."),
          "defaultValue": "0",
          "options": [
            opt(0, "Monochrome (rouge < 10 %)",             "Monochrome (red < 10%)"),
            opt(1, "Couleur (vert / jaune / rouge)",        "Color (green / yellow / red)")
          ]
        },
        {
          "type": "select",
          "messageKey": "GHOST_LEVEL",
          "label": t("Segments éteints (ghost)", "Unlit segments (ghost)"),
          "defaultValue": "0",
          "options": [
            opt(0, "Défaut",   "Default"),
            opt(1, "Léger",    "Light"),
            opt(2, "Désactivé", "Off")
          ]
        },
        {
          "type": "select",
          "messageKey": "RED_RING_LATERAL",
          "label": t("Bandes rouges latérales", "Red side stripes"),
          "defaultValue": "0",
          "options": [
            opt(1, "Afficher", "Show"),
            opt(0, "Cacher",   "Hide")
          ]
        }
      ]
    },

    // ── Unités ────────────────────────────────────────────────────────────
    {
      "type": "section",
      "items": [
        { "type": "heading", "size": 4, "defaultValue": t("Unités", "Units") },
        {
          "type": "select",
          "messageKey": "TEMP_UNIT",
          "label": t("Température", "Temperature"),
          "defaultValue": "0",
          "options": [
            opt(0, "°C (Celsius)",    "°C (Celsius)"),
            opt(1, "°F (Fahrenheit)", "°F (Fahrenheit)")
          ]
        },
        {
          "type": "select",
          "messageKey": "DIST_UNIT",
          "label": t("Distance", "Distance"),
          "description": t("Pour les complications « distance » et « vitesse du vent ».",
                           "For the « distance » and « wind speed » complications."),
          "defaultValue": "0",
          "options": [
            opt(0, "Kilomètres", "Kilometers"),
            opt(1, "Miles",      "Miles")
          ]
        }
      ]
    },

    // ── Complications ─────────────────────────────────────────────────────
    {
      "type": "section",
      "items": [
        { "type": "heading", "size": 4, "defaultValue": t("Complications", "Complications") },
        {
          "type": "select",
          "messageKey": "COMPLICATION",
          "label": t("Case à côté de la date", "Box next to the date"),
          "defaultValue": "0",
          "options": SIDE_OPTS
        },
        {
          "type": "select",
          "messageKey": "COMP_TOP",
          "label": t("Au-dessus de l'heure", "Above the time"),
          "defaultValue": "0",
          "options": ROW_OPTS
        },
        {
          "type": "select",
          "messageKey": "COMP_BOTTOM",
          "label": t("Sous l'heure", "Below the time"),
          "defaultValue": "0",
          "options": ROW_OPTS
        },
        {
          "type": "select",
          "messageKey": "WEATHER_PERIOD",
          "label": t("Période de la météo horaire", "Hourly weather period"),
          "description": t("Écart entre deux colonnes de la météo horaire.", "Gap between two columns of the hourly weather."),
          "defaultValue": "1",
          "options": [
            opt(1, "1 h (défaut)", "1 h (default)"),
            opt(2, "2 h", "2 h"),
            opt(3, "3 h", "3 h"),
            opt(4, "4 h", "4 h")
          ]
        }
      ]
    },

    // ── Localisations (un seul cadre) ─────────────────────────────────────
    {
      "type": "section",
      "items": locationItems()
    },

    // ── Mouvement du poignet + cycles ─────────────────────────────────────
    {
      "type": "section",
      "items": [
        { "type": "heading", "size": 4, "defaultValue": t("Mouvement du poignet", "Wrist movement") },
        {
          "type": "select",
          "messageKey": "WRIST_ACTION",
          "label": t("Action d'un mouvement du poignet", "Wrist movement action"),
          "description": t("Les boutons Haut / Bas sont réservés par le système dans un cadran. La détection de mouvement n'est activée que si une action est choisie.",
                           "Up / Down buttons are reserved by the system in a watchface. Motion detection is only enabled when an action is chosen."),
          "defaultValue": "0",
          "options": [
            opt(0, "Aucune (détection coupée)",  "None (detection off)"),
            opt(1, "Cycle des complications",    "Cycle complications")
          ]
        }
      ].concat(cycleItems())
    },

    // ── Sets de configuration ─────────────────────────────────────────────
    {
      "type": "section",
      "items": [
        { "type": "heading", "size": 4, "defaultValue": t("Sets de configuration", "Configuration sets") },
        { "type": "text", "id": "setsInfo", "defaultValue": "—" },
        { "type": "text", "defaultValue": t(
            "Un set mémorise les réglages d'affichage de cette page et les cycles de complications (pas l'action du mouvement du poignet). Un nom déjà utilisé met à jour le set existant ; 5 sets maximum.",
            "A set stores the display settings of this page and the complication cycles (not the wrist movement action). An existing name updates that set; 5 sets maximum.") },
        {
          "type": "input",
          "messageKey": "SET_NAME_NEW",
          "label": t("Enregistrer les réglages actuels sous le nom", "Save current settings as"),
          "description": t("Vide = ne pas enregistrer. 14 caractères max, majuscules sans accent.",
                           "Empty = do not save. 14 chars max, uppercase, no accents."),
          "defaultValue": "",
          "attributes": { "placeholder": "SPORT", "limit": 14, "type": "text" }
        },
        {
          "type": "select",
          "messageKey": "SET_REPLACE",
          "label": t("Sets pleins : remplacer", "Sets full: replace"),
          "defaultValue": "-1",
          "options": [ opt(-1, "Ne pas remplacer", "Do not replace") ]
        },
        {
          "type": "select",
          "messageKey": "SET_LOAD",
          "label": t("Set à charger", "Set to load"),
          "defaultValue": "-1",
          "options": [ opt(-1, "— Choisir un set", "— Choose a set") ]
        },
        { "type": "button", "id": "setLoadBtn", "primary": false, "defaultValue": t("Charger ce set", "Load this set") },
        { "type": "input", "messageKey": "SET_LOAD_FLAG", "label": "", "defaultValue": "" },
        {
          "type": "select",
          "messageKey": "SET_DELETE",
          "label": t("Supprimer un set", "Delete a set"),
          "defaultValue": "-1",
          "options": [ opt(-1, "Aucun", "None") ]
        }
      ]
    },

    { "type": "submit", "defaultValue": t("Enregistrer", "Save") }
  ];
};
