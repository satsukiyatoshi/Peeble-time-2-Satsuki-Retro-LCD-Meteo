/*
 * Satsuki Retro LCD Meteo – code côté téléphone
 * Récupère les prévisions Open-Meteo et les envoie à la montre.
 * Réglages : page de configuration Clay (langue, format de date, unité de
 * température, localisations ; voir clay-config.js) et config.js (reprise après échec).
 *
 * Localisations : jusqu'à 5 localisations enregistrées, une ligne chacune dans la page de réglages (« Ville, Pays »,
 * « latitude, longitude » ou « GPS » = position actuelle). La case à côté de la date (n° 1), la rangée du haut (n° 2)
 * et la rangée du bas (n° 3) choisissent chacune leur localisation (position actuelle ou l'une des 5). Si le GPS est
 * indisponible, la dernière position connue est utilisée. Au rafraîchissement, UNE seule requête est faite par
 * localisation réellement affichée (rien pour une localisation que personne n'affiche ni pour une complication sans
 * météo) ; le résultat est mis en cache par localisation et envoyé dans un seul message avec le masque des
 * emplacements concernés (WX_SLOTS).
 * Bandeau du bas (WEATHER_TREND, aucun réglage) : si tous les affichages météo partagent la même localisation →
 * « VILLE : TENDANCE » ; sinon « #1|2 BORDEAUX;3 PARIS » (le « # » annonce ce format à la montre, qui ajuste les noms
 * à la largeur disponible).
 *
 * La montre reçoit 10 créneaux météo : à partir de la prochaine heure pleine locale, tous
 * les 1, 2, 3 ou 4 h selon la « période météo » choisie dans Clay
 * (ex. à 13h33, période 1 h : 14h, 15h, … 23h ; période 2 h : 14h, 16h, … 08h).
 * Elle les affiche (selon les complications choisies) sur 1 ou 2 rangées de 5.
 * Elle reçoit aussi la tendance des 6 jours (DAILY_0..5 : code météo, max, min, date).
 * Il envoie aussi les heures de lever/coucher du soleil (complication) et le texte du bandeau du bas (ex. « BORDEAUX : PLUIE 8% »).
 * Chaque colonne est envoyée dans un seul entier :
 *   bits 0-6   code météo WMO (127 = inconnu)
 *   bit  7     1 = jour, 0 = nuit
 *   bits 8-15  température arrondie + 128
 *   bits 16-23 heure locale du créneau (0-23)
 */

var CFG = require('./config');
var Clay = require('pebble-clay');
var clayConfig = require('./clay-config');
// autoHandleEvents:false : on gère nous-mêmes l'ouverture et la fermeture de la
// page, pour appliquer les réglages côté téléphone ET les envoyer à la montre.
// Fonction exécutée DANS la page de réglages (Clay la sérialise : elle doit être autonome, sans variable
// extérieure, en ES5). Elle fait deux choses :
//  - les réglages « cycles » (groupe "cycles") ne sont visibles que si « Mouvement du poignet » = cycle des complications ;
//  - bouton « Charger ce set » : marque le champ caché SET_LOAD_FLAG puis valide la page (comme « Enregistrer »),
//    ce qui déclenche webviewclosed → processSetActions().
function clayCustom(minified) {
  var clayConfig = this;
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var wrist = clayConfig.getItemByMessageKey('WRIST_ACTION');
    if (wrist) {
      var showCycles = function() {
        var on = String(wrist.get()) === '1';
        var items = clayConfig.getItemsByGroup('cycles') || [];
        for (var i = 0; i < items.length; i++) { if (on) items[i].show(); else items[i].hide(); }
      };
      wrist.on('change', showCycles);
      showCycles();
    }
    // Textes personnalisés : seuls les champs utiles à la disposition choisie sont visibles (et, pour le bandeau,
    // seulement si son contenu est « Texte personnalisé »)
    ['TOP', 'BOT', 'BAN'].forEach(function(id) {
      var use = (id === 'BAN') ? clayConfig.getItemByMessageKey('CX_BAN_USE') : null;
      var mode = clayConfig.getItemByMessageKey('CX_' + id + '_MODE');
      if (!mode) return;
      var apply = function() {
        var on = !use || String(use.get()) === '1';
        var m = String(mode.get());
        var used = { L: m === '2' || m === '3', C: m === '1' || m === '3', R: m === '2' || m === '3' };
        var mi = clayConfig.getItemsByGroup('cx_' + id + '_M') || [];
        for (var i = 0; i < mi.length; i++) { if (on) mi[i].show(); else mi[i].hide(); }
        ['L', 'C', 'R'].forEach(function(p) {
          var its = clayConfig.getItemsByGroup('cx_' + id + '_' + p) || [];
          for (var j = 0; j < its.length; j++) { if (on && used[p]) its[j].show(); else its[j].hide(); }
        });
      };
      mode.on('change', apply);
      if (use) use.on('change', apply);
      apply();
    });
    var flag = clayConfig.getItemByMessageKey('SET_LOAD_FLAG');
    var sel = clayConfig.getItemByMessageKey('SET_LOAD');
    var btn = clayConfig.getItemById('setLoadBtn');
    if (flag) { flag.set(''); flag.hide(); }
    if (!flag || !sel || !btn) return;               // aucun set enregistré : champs retirés de la page
    btn.on('click', function() {
      if (String(sel.get()) === '-1') return;        // aucun set choisi
      flag.set('1');
      var submit = document.querySelector('button[type="submit"]');
      if (submit) submit.click();
    });
  });
}
var clay = new Clay(clayConfig(0), clayCustom, { autoHandleEvents: false });   // reconstruit à chaque ouverture (buildClayConfig)

var COLS = 10;
var weatherTimer = null;
var fetching     = false;
var lastFetchMs  = 0;

// ── Clés AppMessage (noms déclarés dans messageKeys de package.json) ─────
// Le SDK attribue lui-même les numéros : on utilise les noms des deux côtés.
var K = {
  FORECAST_TS: 'FORECAST_TS',          // heure (s Unix) du calcul des prévisions
  WX_SLOTS: 'WX_SLOTS',                // téléphone → montre : emplacements servis par ce message (bit 0 case date, 1 rangée haut, 2 rangée bas)
  WX_UTC_OFF: 'WX_UTC_OFF',            // téléphone → montre : décalage UTC (s) de la localisation des données
  REQUEST_WEATHER: 'REQUEST_WEATHER',  // montre → téléphone : rafraîchir maintenant
  WEATHER_TREND: 'WEATHER_TREND',      // texte « VILLE : TENDANCE »
  LANGUAGE: 'LANGUAGE',                // téléphone → montre : 0 = français, 1 = English
  DATE_FORMAT: 'DATE_FORMAT',          // téléphone → montre : 0 = JJ-MM, 1 = MM-JJ
  TIME_FORMAT: 'TIME_FORMAT',          // téléphone → montre : 0 = 24 h, 1 = 12 h
  ICON_STYLE: 'ICON_STYLE',            // téléphone → montre : 0 = monochrome, 1 = couleurs
  GHOST_LEVEL: 'GHOST_LEVEL',          // téléphone → montre : 0 = défaut, 1 = léger, 2 = désactivé
  FONT_STYLE: 'FONT_STYLE',            // téléphone → montre : 0..3 = 7 segments (Classic, Classic Mini, Modern, Modern Mini), 4..7 = 14 segments (mêmes), 8 = 7 segments spécial D
  COMPLICATION: 'COMPLICATION',        // téléphone → montre : 0 pas, 1 FC, 2 distance, 3 soleil, 4 secondes, 5 batterie, 6 températures min / max, 7 humidité, 8 UV, 9 qualité de l'air, 10 vent
  HUMIDITY: 'HUMIDITY',                // téléphone → montre : humidité %, UV_INDEX ×10, AIR_QUALITY (AQI européen), WIND_SPEED km/h ×10 (-1 = inconnu)
  TEMP_UNIT: 'TEMP_UNIT',              // téléphone → montre : 0 = °C, 1 = °F (lettre de la complication températures)
  DIST_UNIT: 'DIST_UNIT',              // téléphone → montre : 0 = kilomètres, 1 = miles
  RED_RING_LATERAL: 'RED_RING_LATERAL', // téléphone → montre : 0 = bandes latérales masquées, 1 = affichées
  BATT_STYLE: 'BATT_STYLE',            // téléphone → montre : segments de la pile 0 = monochrome (rouge < 10 %), 1 = couleur
  COMP_TOP: 'COMP_TOP',                // téléphone → montre : rangée du haut : 0 météo horaire, 1 FC + distance, 2 calories + pas, 3 soleil, 4 tendance 3 jours, 5 FC + calories, 6 FC + pas, 7 distance + calories, 8 pas + distance, 9 UV + air, 10 humidité + vent
  COMP_BOTTOM: 'COMP_BOTTOM',          // téléphone → montre : rangée du bas (mêmes choix)
  WEATHER_PERIOD: 'WEATHER_PERIOD',    // téléphone → montre : période météo 1..4 h (la montre la garde pour ses sets)
  WRIST_ACTION: 'WRIST_ACTION',        // téléphone → montre : mouvement du poignet : 0 aucune (détection coupée), 1 cycle complications
  CYCLE_DATA: 'CYCLE_DATA',            // téléphone → montre : 15 octets = 5 cycles × (case date, rangée haut, rangée bas) ; 255 = inchangé
  // (sets de configuration : uniquement sur le téléphone, rien n'est envoyé à la montre)
  CUSTOM_TOP: 'CUSTOM_TOP',            // téléphone → montre : textes personnalisés (rangée du haut, du bas, bandeau) : voir customString
  CUSTOM_BOT: 'CUSTOM_BOT',
  CUSTOM_BAN: 'CUSTOM_BAN',
  STATE_PACK: 'STATE_PACK'             // montre → téléphone : état d'affichage courant après un cycle (voir packSettings)
};

function forecastKey(i) {
  return 'FORECAST_' + i;   // colonnes 0..9
}

// ── Réglages choisis dans la page Clay ───────────────────────────────────
// Clay les mémorise dans localStorage ('clay-settings'). Valeurs par défaut
// (rien d'enregistré) : français, JJ-MM, °C.
function loadRawSettings() {
  var s = {};
  try { s = JSON.parse(localStorage.getItem('clay-settings') || '{}') || {}; } catch (e) { s = {}; }
  return (s && typeof s === 'object') ? s : {};
}

function storeRawSettings(s) {
  try { localStorage.setItem('clay-settings', JSON.stringify(s)); } catch (e) {}
}

// 5 cycles × 3 emplacements (case date, rangée haut, rangée bas) → 15 octets ; 255 = emplacement inchangé
function cycleBytes(s) {
  var slots = ['SIDE', 'TOP', 'BOT'], out = [];
  for (var k = 1; k <= 5; k++) {
    for (var i = 0; i < 3; i++) {
      var v = parseInt(s['CYC' + k + '_' + slots[i]], 10);
      out.push((isNaN(v) || v < 0 || v > (i === 0 ? 10 : 11)) ? 255 : v);   // case date 0..10, rangées 0..11 (11 = texte personnalisé)
    }
  }
  return out;
}

function locIdx(v) {
  var n = parseInt(v, 10);
  return (n >= 0 && n < 5) ? n : -1;
}

function actionFrom(v) {
  var a = parseInt(v, 10);
  return (a === 1) ? 1 : 0;
}

// ── Textes personnalisés ─────────────────────────────────────────────────
// Un texte par emplacement (rangée du haut « TOP », du bas « BOT », bandeau « BAN »), envoyé à la montre sous la forme
//   RRGGBB texte  [ 0x1F RRGGBB texte  [ 0x1F RRGGBB texte ] ]
// 1 segment = centré, 2 = gauche + droite, 3 = gauche + centre + droite (selon la « disposition » choisie).
// 16 caractères et 33 octets UTF-8 au plus par texte (tampon de la montre).
var CUSTOM_COLOR_DEF = { TOP: 'FFFFFF', BOT: 'FFFFFF', BAN: 'FFFF00' };

function cleanCustomText(v) {
  var t = String(v === undefined || v === null ? '' : v).replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim().substring(0, 16);
  function bytes(x) { try { return unescape(encodeURIComponent(x)).length; } catch (e) { return x.length; } }
  while (t.length && bytes(t) > 33) t = t.substring(0, t.length - 1);
  return t.trim();
}

function customString(s, id) {
  var mode = parseInt(s['CX_' + id + '_MODE'], 10);
  var parts = (mode === 2) ? ['L', 'R'] : (mode === 3) ? ['L', 'C', 'R'] : ['C'];
  return parts.map(function(p) {
    var col = String(s['CX_' + id + '_' + p + '_COL'] || '').toUpperCase();
    if (!/^[0-9A-F]{6}$/.test(col)) col = CUSTOM_COLOR_DEF[id];
    return col + cleanCustomText(s['CX_' + id + '_' + p]);
  }).join('\u001f');
}

function readSettings() {
  var s = loadRawSettings();
  return {
    saved: (s.WRIST_ACTION !== undefined || s.CYC1_SIDE !== undefined ||
            s.LANGUAGE !== undefined || s.DATE_FORMAT !== undefined || s.TIME_FORMAT !== undefined || s.ICON_STYLE !== undefined || s.GHOST_LEVEL !== undefined || s.FONT_STYLE !== undefined ||
            s.TEMP_UNIT !== undefined || s.RED_RING_LATERAL !== undefined ||
            s.COMPLICATION !== undefined || s.DIST_UNIT !== undefined ||
            s.WEATHER_PERIOD !== undefined || s.BATT_STYLE !== undefined || s.COMP_TOP !== undefined || s.COMP_BOTTOM !== undefined),
    // icônes en couleurs par défaut (aussi si ce réglage n'a jamais été enregistré)
    iconColor: (s.ICON_STYLE === undefined || parseInt(s.ICON_STYLE, 10) === 1) ? 1 : 0,
    ghost: Math.max(0, Math.min(2, parseInt(s.GHOST_LEVEL, 10) || 0)),
    fontStyle: Math.max(0, Math.min(8, parseInt(s.FONT_STYLE, 10) || 0)),
    time12: parseInt(s.TIME_FORMAT, 10) === 1 ? 1 : 0,
    comp: Math.max(0, Math.min(10, parseInt(s.COMPLICATION, 10) || 0)),
    dist: parseInt(s.DIST_UNIT, 10) === 1 ? 1 : 0,
    battStyle: parseInt(s.BATT_STYLE, 10) === 1 ? 1 : 0,
    period: Math.max(1, Math.min(4, parseInt(s.WEATHER_PERIOD, 10) || 1)),
    compTop: Math.max(0, Math.min(11, parseInt(s.COMP_TOP, 10) || 0)),
    compBot: Math.max(0, Math.min(11, parseInt(s.COMP_BOTTOM, 10) || 0)),
    banCustom: parseInt(s.CX_BAN_USE, 10) === 1,                       // bandeau du bas : texte personnalisé au lieu de ville + tendance
    custom: [customString(s, 'TOP'), customString(s, 'BOT'), parseInt(s.CX_BAN_USE, 10) === 1 ? customString(s, 'BAN') : ''],
    redRing: parseInt(s.RED_RING_LATERAL, 10) === 1 ? 1 : 0,
    // localisation de chaque affichage : -1 = position GPS actuelle, 0..4 = localisation enregistrée
    locSide: locIdx(s.LOC_SIDE), locTop: locIdx(s.LOC_TOP), locBot: locIdx(s.LOC_BOTTOM),
    lang: parseInt(s.LANGUAGE, 10) === 1 ? 1 : 0,
    date: parseInt(s.DATE_FORMAT, 10) === 1 ? 1 : 0,
    fahrenheit: parseInt(s.TEMP_UNIT, 10) === 1,
    wrist: actionFrom(s.WRIST_ACTION),
    cycData: cycleBytes(s)
  };
}

// ── État d'affichage packé sur 28 bits (même format que pack_state / apply_state de main.c) ──
//   0 langue · 1 date · 2 heure 12 h · 3 icônes couleur · 4-5 ghost · 6 anneau latéral · 7-10 police
//   11-14 complication · 15 distance en miles · 16 °F · 17 pile couleur · 18-21 rangée haut · 22-25 rangée bas
//   26-27 période météo - 1
function packSettings(s) {
  return (s.lang & 1) | ((s.date & 1) << 1) | ((s.time12 & 1) << 2) | ((s.iconColor & 1) << 3) |
         ((s.ghost & 3) << 4) | ((s.redRing & 1) << 6) | ((s.fontStyle & 15) << 7) | ((s.comp & 15) << 11) |
         ((s.dist & 1) << 15) | ((s.fahrenheit ? 1 : 0) << 16) | ((s.battStyle & 1) << 17) |
         ((s.compTop & 15) << 18) | ((s.compBot & 15) << 22) | (((s.period - 1) & 3) << 26);
}

// État reçu de la montre (après un cycle) → réglages Clay mémorisés (la page de réglages les affichera)
function applyStateToSettings(v) {
  var s = loadRawSettings();
  s.LANGUAGE         = String(v & 1);
  s.DATE_FORMAT      = String((v >> 1) & 1);
  s.TIME_FORMAT      = String((v >> 2) & 1);
  s.ICON_STYLE       = String((v >> 3) & 1);
  s.GHOST_LEVEL      = String(Math.min(2, (v >> 4) & 3));
  s.RED_RING_LATERAL = String((v >> 6) & 1);
  s.FONT_STYLE       = String(Math.min(8, (v >> 7) & 15));
  s.COMPLICATION     = String(Math.min(10, (v >> 11) & 15));
  s.DIST_UNIT        = String((v >> 15) & 1);
  s.TEMP_UNIT        = String((v >> 16) & 1);
  s.BATT_STYLE       = String((v >> 17) & 1);
  s.COMP_TOP         = String(Math.min(11, (v >> 18) & 15));
  s.COMP_BOTTOM      = String(Math.min(11, (v >> 22) & 15));
  s.WEATHER_PERIOD   = String(((v >> 26) & 3) + 1);
  storeRawSettings(s);
}

// ── Sets de configuration (5 maximum), mémorisés sur le téléphone ────────
var SETS_KEY   = 'satsuki-sets';        // [{ n: NOM, p: état packé }]
var SETMSG_KEY = 'satsuki-set-msg';     // résultat de la dernière opération, affiché une fois dans la page de réglages
var MAX_SETS   = 5;
var SET_NAME_MAX = 14;

function loadSets() {
  try {
    var a = JSON.parse(localStorage.getItem(SETS_KEY) || '[]');
    if (!Array.isArray(a)) return [];
    var out = [];
    for (var i = 0; i < a.length && out.length < MAX_SETS; i++) {
      if (a[i] && typeof a[i].n === 'string' && a[i].n && typeof a[i].p === 'number') {
        var set = { n: a[i].n, p: a[i].p };
        if (Array.isArray(a[i].c) && a[i].c.length === 15) set.c = a[i].c;   // cycles de complications (255 = inchangé)
        out.push(set);
      }
    }
    return out;
  } catch (e) { return []; }
}

// Cycles de complications mémorisés avec un set : 15 valeurs (255 = emplacement inchangé)
function currentCycles() { return cycleBytes(loadRawSettings()); }

// Remet les cycles d'un set dans les réglages mémorisés (CYC1_SIDE … CYC5_BOT : "-1" = inchangé)
function applyCyclesToSettings(c) {
  if (!Array.isArray(c) || c.length !== 15) return;   // ancien set sans cycles : les cycles actuels restent
  var s = loadRawSettings(), slots = ['SIDE', 'TOP', 'BOT'];
  for (var k = 0; k < 5; k++) {
    for (var i = 0; i < 3; i++) {
      var v = c[k * 3 + i];
      s['CYC' + (k + 1) + '_' + slots[i]] = String((typeof v === 'number' && v >= 0 && v <= (i === 0 ? 10 : 11)) ? v : -1);
    }
  }
  storeRawSettings(s);
}

function storeSets(a) {
  try { localStorage.setItem(SETS_KEY, JSON.stringify(a)); } catch (e) {}
}

// Nom saisi → majuscules sans accent, seulement A-Z 0-9 espace - _ . (la police du bandeau et le séparateur « | » l'exigent)
function cleanSetName(n) {
  var t = String(n === undefined || n === null ? '' : n).toUpperCase()
    .replace(/[ÀÁÂÃÄÅ]/g, 'A').replace(/Ç/g, 'C').replace(/[ÈÉÊË]/g, 'E').replace(/[ÌÍÎÏ]/g, 'I')
    .replace(/[ÒÓÔÕÖ]/g, 'O').replace(/[ÙÚÛÜ]/g, 'U').replace(/Ÿ/g, 'Y').replace(/Ñ/g, 'N')
    .replace(/[^A-Z0-9 \-_.]/g, '').replace(/\s+/g, ' ').trim();
  return t.substring(0, SET_NAME_MAX).trim();
}

// Enregistrement / remplacement / suppression demandés dans la page de réglages.
// Retourne l'index du set actif à envoyer à la montre (-1 = aucun), ou null si les sets n'ont pas changé.
function processSetActions() {
  var raw = loadRawSettings();
  var name = cleanSetName(raw.SET_NAME_NEW);
  var del = parseInt(raw.SET_DELETE, 10);
  var rep = parseInt(raw.SET_REPLACE, 10);
  var load = (String(raw.SET_LOAD_FLAG) === '1') ? parseInt(raw.SET_LOAD, 10) : NaN;   // bouton « Charger ce set »
  var sets = loadSets();
  var notes = [];
  var changed = false, cur = -1;
  var lang = readSettings().lang;                       // messages dans la langue des réglages
  function T(fr, en) { return lang === 1 ? en : fr; }

  var loadSet = null;                                   // set à charger (objet), choisi AVANT une éventuelle suppression
  if (!isNaN(load) && load >= 0 && load < sets.length) loadSet = sets[load];
  if (!isNaN(del) && del >= 0 && del < sets.length) {
    notes.push(T('Set « ' + sets[del].n + ' » supprimé', 'Set « ' + sets[del].n + ' » deleted'));
    if (loadSet === sets[del]) loadSet = null;          // on ne charge pas un set qu'on supprime
    sets.splice(del, 1);
    changed = true;
  }
  if (loadSet) {
    // Chargement : le set remplace les réglages d'affichage ; le nom saisi éventuel est ignoré
    applyStateToSettings(loadSet.p);
    applyCyclesToSettings(loadSet.c);
    lang = readSettings().lang;                         // le set chargé peut changer la langue
    cur = sets.indexOf(loadSet);
    notes.push(T('Set « ' + loadSet.n + ' » chargé' + (name ? ' (nom ignoré)' : ''), 'Set « ' + loadSet.n + ' » loaded' + (name ? ' (name ignored)' : '')));
    name = '';
  }
  if (name) {
    var pack = packSettings(readSettings());
    var cyc = currentCycles();
    var found = -1;
    for (var i = 0; i < sets.length; i++) if (sets[i].n === name) found = i;
    if (found >= 0) {                                   // nom déjà utilisé : le set est mis à jour (noms uniques)
      sets[found].p = pack; sets[found].c = cyc; cur = found; changed = true;
      notes.push(T('Set « ' + name + ' » mis à jour', 'Set « ' + name + ' » updated'));
    } else if (sets.length < MAX_SETS) {
      sets.push({ n: name, p: pack, c: cyc }); cur = sets.length - 1; changed = true;
      notes.push(T('Set « ' + name + ' » enregistré', 'Set « ' + name + ' » saved'));
    } else if (!isNaN(rep) && rep >= 0 && rep < sets.length) {
      notes.push(T('Set « ' + sets[rep].n + ' » remplacé par « ' + name + ' »', 'Set « ' + sets[rep].n + ' » replaced by « ' + name + ' »'));
      sets[rep] = { n: name, p: pack, c: cyc }; cur = rep; changed = true;
    } else {
      notes.push(T('Non enregistré : 5 sets déjà pleins, choisissez celui à remplacer', 'Not saved: 5 sets full, pick one to replace'));
    }
  }
  if (changed) storeSets(sets);
  try { if (notes.length) localStorage.setItem(SETMSG_KEY, notes.join(' — ')); } catch (e) {}

  // Les champs d'action ne doivent pas rester pré-remplis à la prochaine ouverture
  // (raw relu : applyStateToSettings a pu modifier les réglages stockés)
  raw = loadRawSettings();
  delete raw.SET_NAME_NEW; delete raw.SET_REPLACE; delete raw.SET_DELETE; delete raw.SET_LOAD; delete raw.SET_LOAD_FLAG;
  storeRawSettings(raw);
  return changed ? cur : null;
}

// Page de réglages, reconstruite à chaque ouverture : une seule langue (celle des réglages), liste des
// sets à jour, et champs inutiles retirés (rien à charger / supprimer sans set ; « remplacer » seulement
// quand les 5 sets sont pleins).
function buildClayConfig() {
  var lang = readSettings().lang;
  var cfg = clayConfig(lang);
  var sets = loadSets();
  var full = sets.length >= MAX_SETS;
  var msg = '';
  try { msg = localStorage.getItem(SETMSG_KEY) || ''; localStorage.removeItem(SETMSG_KEY); } catch (e) {}
  var list = [];
  for (var i = 0; i < sets.length; i++) list.push((i + 1) + '. ' + sets[i].n);
  var info = (lang === 1 ? 'Saved sets' : 'Sets enregistrés') + ' (' + sets.length + '/' + MAX_SETS + ') : ' + (list.length ? list.join('  ') : '—');
  if (msg) info += '<br>' + msg;
  function opts(first, prefix) {
    var o = [{ label: first, value: '-1' }];
    for (var k = 0; k < sets.length; k++) o.push({ label: prefix + (k + 1) + ' : ' + sets[k].n, value: String(k) });
    return o;
  }
  // Localisations : une seule liste pour les 3 choix d'affichage ; un choix qui pointe vers une localisation supprimée retombe sur le GPS
  var locs = getLocations();
  var locOpts = [{ label: lang === 1 ? 'Current position (GPS)' : 'Position actuelle (GPS)', value: '-1' }];
  for (var q = 0; q < locs.length; q++) {
    if (!locs[q]) continue;
    locOpts.push({ label: (q + 1) + ' : ' + locDisplayName(locs[q], q) + (locs[q].city && locs[q].country ? ' (' + locs[q].country + ')' : ''),
                   value: String(q) });
  }
  var raw = loadRawSettings(), fixed = false;
  ['LOC_SIDE', 'LOC_TOP', 'LOC_BOTTOM'].forEach(function(key) {
    var n = parseInt(raw[key], 10);
    if (raw[key] !== undefined && !(n === -1 || (n >= 0 && n < MAX_LOCS && locs[n]))) { raw[key] = '-1'; fixed = true; }
  });
  delete raw.LOC_CITY;   // ancien réglage (ville du bandeau), supprimé : le bandeau se déduit des autres choix
  if (fixed) storeRawSettings(raw);
  var locMsg = '';
  try { locMsg = localStorage.getItem(LOCMSG_KEY) || ''; localStorage.removeItem(LOCMSG_KEY); } catch (e) {}
  function drop(it) {   // champ retiré de la page quand il ne sert à rien
    if (it.id === 'locsInfo' && !locMsg) return true;   // rien à signaler : le texte d'information est retiré
    if (!sets.length && (it.messageKey === 'SET_LOAD' || it.messageKey === 'SET_DELETE' ||
                         it.messageKey === 'SET_LOAD_FLAG' || it.id === 'setLoadBtn')) return true;
    return (!full && it.messageKey === 'SET_REPLACE');
  }
  (function walk(items) {
    for (var k = items.length - 1; k >= 0; k--) {
      var it = items[k];
      if (drop(it)) { items.splice(k, 1); continue; }
      if (it.items) walk(it.items);
      if (it.id === 'setsInfo') it.defaultValue = info;
      if (it.id === 'locsInfo') it.defaultValue = locMsg;
      if (it.messageKey === 'LOC_SIDE' || it.messageKey === 'LOC_TOP' || it.messageKey === 'LOC_BOTTOM') it.options = locOpts;
      if (it.messageKey === 'SET_LOAD')    it.options = opts(lang === 1 ? '— Choose a set' : '— Choisir un set', '');
      if (it.messageKey === 'SET_REPLACE') it.options = opts(lang === 1 ? 'Do not replace' : 'Ne pas remplacer', '');
      if (it.messageKey === 'SET_DELETE')  it.options = opts(lang === 1 ? 'None' : 'Aucun', lang === 1 ? 'Delete ' : 'Supprimer ');
    }
  })(cfg);
  return cfg;
}

// Ajoute langue et format de date à un message pour la montre (seulement si
// l'utilisateur a enregistré des réglages : sinon la montre garde ses défauts).
function addSettings(msg) {
  var s = readSettings();
  if (s.saved) {
    msg[K.LANGUAGE] = s.lang;
    msg[K.DATE_FORMAT] = s.date;
    msg[K.TIME_FORMAT] = s.time12;
    msg[K.ICON_STYLE] = s.iconColor;
    msg[K.GHOST_LEVEL] = s.ghost;
    msg[K.FONT_STYLE] = s.fontStyle;
    msg[K.COMPLICATION] = s.comp;
    msg[K.DIST_UNIT] = s.dist;
    msg[K.TEMP_UNIT] = s.fahrenheit ? 1 : 0;
    msg[K.RED_RING_LATERAL] = s.redRing;
    msg[K.BATT_STYLE] = s.battStyle;
    msg[K.COMP_TOP] = s.compTop;
    msg[K.COMP_BOTTOM] = s.compBot;
    msg[K.WEATHER_PERIOD] = s.period;
    msg[K.WRIST_ACTION] = s.wrist;
    msg[K.CYCLE_DATA] = s.cycData;
  }
  return msg;
}

var pendingRefetch = false;   // réglages changés pendant une récupération en cours

// ── File d'envoi AppMessage : un message à la fois, 3 essais ─────────────
var outbox = [];
var sending = false;

function pump() {
  if (sending || outbox.length === 0) return;
  sending = true;
  var item = outbox[0];
  Pebble.sendAppMessage(item.msg, function() {
    outbox.shift();
    sending = false;
    pump();
  }, function() {
    item.tries++;
    if (item.tries >= 3) {
      outbox.shift();
      console.log('[Satsuki Retro LCD Meteo] envoi « ' + item.label + ' » échoué');
      if (item.onFail) item.onFail();
    }
    sending = false;
    setTimeout(pump, 1000);
  });
}

function sendMsg(msg, label, onFail) {
  outbox.push({ msg: msg, label: label, tries: 0, onFail: onFail });
  pump();
}

// ── Prévisions ───────────────────────────────────────────────────────────
function pack(code, isDay, temp, hourField) {
  var c = (code === null || code === undefined || !isFinite(code)) ? 127 : (code & 0x7F);
  var t = (temp === null || temp === undefined || !isFinite(temp)) ? 0 : Math.round(temp);
  t = Math.max(-128, Math.min(127, t)) + 128;
  var h = Math.max(0, Math.min(255, hourField | 0));
  return c | (isDay ? 0x80 : 0) | (t << 8) | (h << 16);
}

// Instants (secondes Unix) des COLS créneaux : heures pleines LOCALES DE LA LOCALISATION (« off » = décalage UTC en
// secondes, fourni par Open-Meteo), la première étant l'heure pleine strictement suivante (13h00 ou 13h42 → 14h00),
// puis un créneau tous les « period » heures (1 à 4, réglage Clay).
function slotTimes(nowSec, period, off) {
  var first = Math.floor((nowSec + off) / 3600) * 3600 + 3600 - off;
  var out = [];
  for (var i = 0; i < COLS; i++) out.push(first + i * period * 3600);
  return out;
}

// Décalage UTC (s) de la localisation des données (Open-Meteo, timezone=auto) ; à défaut celui du téléphone
function locOffset(data) {
  return (data && typeof data.utc_offset_seconds === 'number') ? data.utc_offset_seconds
                                                                : -new Date().getTimezoneOffset() * 60;
}

// Entrée horaire la plus proche de l'instant cible (secondes Unix)
function nearestHour(times, targetSec) {
  var best = -1, bestDiff = Infinity;
  for (var i = 0; i < times.length; i++) {
    var d = Math.abs(times[i] - targetSec);
    if (d < bestDiff) { bestDiff = d; best = i; }
  }
  return best;
}

// Nom de ville → majuscules sans accents, « SAINT(E)- » abrégé en « ST(E)- »
// (pour tenir dans le bandeau du bas de la montre).
function formatCityName(cityName) {
  var city = (cityName || 'METEO').toUpperCase();

  city = city
    .replace(/[ÀÁÂÃÄÅ]/g, 'A')
    .replace(/Ç/g, 'C')
    .replace(/[ÈÉÊË]/g, 'E')
    .replace(/[ÌÍÎÏ]/g, 'I')
    .replace(/[ÒÓÔÕÖ]/g, 'O')
    .replace(/[ÙÚÛÜ]/g, 'U')
    .replace(/Ÿ/g, 'Y');

  city = city
    .replace(/\bSAINT[- ]/g, 'ST-')
    .replace(/\bSAINTE[- ]/g, 'STE-');

  city = city.replace(/\s+/g, ' ').trim();

  return city;
}

// ── Tendance météo de la journée ────────────────────────────────────────
// Retourne la TENDANCE seule (la ville est ajoutée par bannerText), par exemple :
//   "ENSOLEILLE", "NUAGEUX", "PLUIE 8%", "NEIGE 12%", "BROUILLARD 5%", "ORAGE"
//
// Priorité au phénomène le plus dégradé : orage > neige > pluie > brouillard >
// nuageux > ensoleillé. Le pourcentage (neige, pluie, brouillard) est la part des
// heures concernées parmi les heures d'aujourd'hui présentes dans les données
// (24 h en temps normal). Orage, nuageux et ensoleillé n'ont pas de pourcentage ;
// « nuageux » s'affiche dès qu'une seule heure est nuageuse (codes 2 ou 3).
// Sans donnée pour aujourd'hui : « METEO ».

function buildTrendWord(data) {
  var hr = data.hourly || {};
  var times = hr.time || [];
  var codes = hr.weather_code || [];
  var precip = hr.precipitation || [];

  // « Aujourd'hui » = le jour LOCAL de la localisation (pas celui du téléphone) : on lit les dates en UTC après
  // ajout du décalage UTC fourni par Open-Meteo.
  var off = locOffset(data);
  var today = new Date(Date.now() + off * 1000);
  var year = today.getUTCFullYear();
  var month = today.getUTCMonth();
  var day = today.getUTCDate();

  var counts = {
    storm: 0,
    snow: 0,
    rain: 0,
    fog: 0,
    cloudy: 0,
    clear: 0
  };

  var totalHours = 0;

  for (var i = 0; i < times.length; i++) {
    var d = new Date((times[i] + off) * 1000);

    if (d.getUTCFullYear() !== year ||
        d.getUTCMonth() !== month ||
        d.getUTCDate() !== day) {
      continue;
    }

    totalHours++;

    var code = parseInt(codes[i], 10);
    var p = parseFloat(precip[i]) || 0;

    // Orage
    if (code === 95 || code === 96 || code === 99) {
      counts.storm++;
    }
    // Neige
    else if (code === 71 || code === 73 || code === 75 ||
             code === 77 || code === 85 || code === 86) {
      counts.snow++;
    }
    // Pluie : on considère également les codes bruine / pluie
    // et toute précipitation mesurable.
    else if ((code >= 51 && code <= 67) ||
             (code >= 80 && code <= 82) ||
             p > 0.1) {
      counts.rain++;
    }
    // Brouillard
    else if (code === 45 || code === 48) {
      counts.fog++;
    }
    // Nuageux
    else if (code === 2 || code === 3) {
      counts.cloudy++;
    }
    // Sinon : ciel dégagé
    else {
      counts.clear++;
    }
  }

  if (totalHours <= 0) {
    return 'METEO';
  }

  // Priorité au phénomène le plus dégradé.
  if (counts.storm > 0) {
    return 'ORAGE';
  }

  if (counts.snow > 0) {
    var snowPct = Math.round(counts.snow * 100 / totalHours);
    return 'NEIGE ' + snowPct + '%';
  }

  if (counts.rain > 0) {
    var rainPct = Math.round(counts.rain * 100 / totalHours);
    return 'PLUIE ' + rainPct + '%';
  }

  if (counts.fog > 0) {
    var fogPct = Math.round(counts.fog * 100 / totalHours);
    return 'BROUILLARD ' + fogPct + '%';
  }

  if (counts.cloudy > 0) {
    return 'NUAGEUX';
  }

  return 'ENSOLEILLE';
}

// Lever / coucher du soleil des 3 jours (secondes Unix, 0 = pas de lever ou de
// coucher ce jour-là, ex. jour ou nuit polaire). La montre choisit le prochain.
// Tendance des 6 jours (J … J+5), un entier par jour (3 par rangée, 6 si les deux rangées sont en tendance) :
//   bits 0-6 code météo WMO du jour (le plus sévère de la journée, 127 = inconnu)
//   bits 8-15 température max + 128, bits 16-23 min + 128
//   bits 24-28 jour du mois, bits 29-31 jour de la semaine (0 = dimanche)
function addDaily(msg, data) {
  var d = data.daily || {};
  var tm = d.time || [], wc = d.weather_code || [];
  var mx = d.temperature_2m_max || [], mn = d.temperature_2m_min || [];
  // les heures daily sont des minuits LOCAUX du lieu : on ajoute le décalage UTC du lieu pour lire la date
  var off = (typeof data.utc_offset_seconds === 'number') ? data.utc_offset_seconds
                                                          : -new Date().getTimezoneOffset() * 60;
  function t8(v) {
    if (typeof v !== 'number' || !isFinite(v)) return 128;
    return Math.max(-128, Math.min(127, Math.round(v))) + 128;
  }
  for (var i = 0; i < 6; i++) {
    var code = (typeof wc[i] === 'number' && isFinite(wc[i])) ? (wc[i] & 0x7F) : 127;
    var dom = 0, wd = 0;
    if (typeof tm[i] === 'number' && isFinite(tm[i])) {
      var dt = new Date((tm[i] + off) * 1000);
      dom = dt.getUTCDate();
      wd = dt.getUTCDay();
    }
    msg['DAILY_' + i] = code | (t8(mx[i]) << 8) | (t8(mn[i]) << 16) | ((dom & 0x1F) << 24) | ((wd & 7) << 29);
  }
}

// Humidité (%), UV (×10), vent (km/h ×10) à l'heure courante (seulement si affichés : need.env) ;
// qualité de l'air (AQI européen) fournie à part (seulement si affichée : need.aqi). Une valeur absente du message
// est « indisponible » pour la montre.
function addEnv(msg, data, aqi, need) {
  if (need.env) {
    var hr = data.hourly || {};
    var idx = nearestHour(hr.time || [], Math.floor(Date.now() / 1000));
    var val = function(arr, mul) {
      var v = (idx >= 0 && arr) ? arr[idx] : undefined;
      return (typeof v === 'number' && isFinite(v)) ? Math.max(0, Math.round(v * mul)) : -1;
    };
    msg['HUMIDITY']   = val(hr.relative_humidity_2m, 1);
    msg['UV_INDEX']   = val(hr.uv_index, 10);
    msg['WIND_SPEED'] = val(hr.wind_speed_10m, 10);   // km/h (unité par défaut d'Open-Meteo) ; la montre convertit en mph
  }
  if (need.aqi) msg['AIR_QUALITY'] = aqi;
}

function addSun(msg, data) {
  var d = data.daily || {};
  var sr = d.sunrise || [], ss = d.sunset || [];
  for (var i = 0; i < 3; i++) {
    msg['SUNRISE_' + i] = (typeof sr[i] === 'number' && isFinite(sr[i])) ? sr[i] : 0;
    msg['SUNSET_' + i]  = (typeof ss[i] === 'number' && isFinite(ss[i])) ? ss[i] : 0;
  }
}

function buildSlots(data, period) {
  var hr  = data.hourly  || {};
  var times = hr.time || [];
  var nowSec = Math.floor(Date.now() / 1000);
  var off = locOffset(data);
  var slots = [];

  var targets = slotTimes(nowSec, period, off);
  for (var i = 0; i < targets.length; i++) {
    var idx = nearestHour(times, targets[i]);
    var code, day, temp;
    if (idx >= 0) {
      code = (hr.weather_code || [])[idx];
      day  = (hr.is_day || [])[idx];
      temp = (hr.temperature_2m || [])[idx];
    }
    // heure affichée : heure locale de la localisation
    slots.push(pack(code, day === 1, temp, Math.floor(((targets[i] + off) % 86400) / 3600)));
  }
  return slots;
}

function planWeather(ms) {
  if (weatherTimer) clearTimeout(weatherTimer);
  weatherTimer = setTimeout(function() { fetchWeather(); }, ms);
}

function refreshMs() {
  return Math.max(5, parseInt(CFG.REFRESH_MIN, 10) || 30) * 60 * 1000;
}

var LOG = '[Satsuki Retro LCD Meteo] ';

// ── Requête HTTP JSON (une seule implémentation pour Open-Meteo, BigDataCloud…) ──
// Appelle toujours « cb » une fois : l'objet JSON, ou null (erreur réseau, HTTP ≠ 200, JSON illisible, blocage).
function getJson(url, timeoutMs, cb) {
  var req = new XMLHttpRequest();
  var done = false;
  var guard = null;
  function end(v) {
    if (done) return;
    done = true;
    clearTimeout(guard);
    cb(v);
  }
  // Garde-fou : sur certains téléphones une requête n'appelle jamais ses callbacks
  guard = setTimeout(function() {
    try { req.abort(); } catch (e) {}
    end(null);
  }, timeoutMs + 10000);
  req.open('GET', url, true);
  req.timeout = timeoutMs;
  req.onload = function() {
    var v = null;
    try { if (req.status === 200) v = JSON.parse(req.responseText); } catch (e) { v = null; }
    end(v);
  };
  req.onerror   = function() { end(null); };
  req.ontimeout = function() { end(null); };
  try { req.send(); } catch (ex) { end(null); }
}

// ═════════════════════════════════════════════════════════════════════════
// Localisations
// ═════════════════════════════════════════════════════════════════════════
// Jusqu'à 5 localisations enregistrées dans la page de réglages (un champ LOCk par localisation, k = 1..5, résolu à
// l'enregistrement en coordonnées + ville + pays : voir loadLocStore). Chaque emplacement d'affichage (case à côté de
// la date, rangée du haut, rangée du bas) choisit :
//   -1 = position GPS actuelle (« cur »), 0..4 = localisation enregistrée.
var MAX_LOCS    = 5;
var LOCMSG_KEY  = 'satsuki-loc-msg';        // résultat de la dernière résolution, affiché une fois dans la page
var LASTPOS_KEY = 'satsuki-last-pos';       // dernière position GPS connue (+ ville associée)

function parseCoord(v, max) {
  if (v === undefined || v === null) return null;
  var t = String(v).replace(',', '.').trim();
  if (t === '') return null;
  var n = Number(t);
  return (isFinite(n) && Math.abs(n) <= max) ? n : null;
}

// Localisations résolues, mémorisées à part de Clay (Clay ne garde que les champs de la page) :
// [{ txt, lat, lon, city, country } | null] × 5, « txt » = texte normalisé affiché dans le champ LOCk.
var LOCS_KEY = 'satsuki-locs';

function saveLocStore(a) {
  try { localStorage.setItem(LOCS_KEY, JSON.stringify(a)); } catch (e) {}
}

// Texte normalisé d'une localisation (ce que la page affiche à la réouverture)
function locText(e) {
  if (e.city && e.country) return e.city + ', ' + e.country;
  return e.city || e.country || (e.lat.toFixed(4) + ', ' + e.lon.toFixed(4));
}

function loadLocStore() {
  var out = [], a = null, k;
  try { a = JSON.parse(localStorage.getItem(LOCS_KEY)); } catch (e) { a = null; }
  if (Array.isArray(a)) {
    for (k = 0; k < MAX_LOCS; k++) {
      var e = a[k];
      out.push((e && isFinite(e.lat) && isFinite(e.lon) && typeof e.txt === 'string')
               ? { txt: e.txt, lat: e.lat, lon: e.lon, city: String(e.city || ''), country: String(e.country || '') } : null);
    }
    return out;
  }
  // Migration depuis l'ancienne page (champs LOCk_COUNTRY / _CITY / _LAT / _LON)
  var raw = loadRawSettings(), changed = false;
  for (k = 1; k <= MAX_LOCS; k++) {
    var la = parseCoord(raw['LOC' + k + '_LAT'], 90), lo = parseCoord(raw['LOC' + k + '_LON'], 180);
    if (la === null || lo === null) { out.push(null); continue; }
    var en = { lat: la, lon: lo, city: String(raw['LOC' + k + '_CITY'] || '').trim(), country: String(raw['LOC' + k + '_COUNTRY'] || '').trim() };
    en.txt = locText(en);
    raw['LOC' + k] = en.txt;
    out.push(en);
    changed = true;
  }
  if (changed) storeRawSettings(raw);
  saveLocStore(out);
  return out;
}

// Les 5 emplacements : { lat, lon, city, country } ou null si vide / invalide
function getLocations() {
  return loadLocStore().map(function(e) {
    return e ? { lat: e.lat, lon: e.lon, city: e.city, country: e.country } : null;
  });
}

// Clé de localisation d'un choix de réglage : 'cur' (GPS) ou '0'..'4' ; une localisation supprimée retombe sur 'cur'
function resolveLoc(v, locs) {
  var n = parseInt(v, 10);
  return (n >= 0 && n < MAX_LOCS && locs[n]) ? String(n) : 'cur';
}

function locDisplayName(loc, idx) {
  return loc.city || loc.country || ('LOC' + (idx + 1));
}

function distKm(la1, lo1, la2, lo2) {
  var r = Math.PI / 180, dLa = (la2 - la1) * r, dLo = (lo2 - lo1) * r;
  var a = Math.sin(dLa / 2) * Math.sin(dLa / 2) +
          Math.cos(la1 * r) * Math.cos(la2 * r) * Math.sin(dLo / 2) * Math.sin(dLo / 2);
  return 6371 * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

// Minuscules sans accents (comparaison des noms de pays)
function norm(s) {
  return String(s === undefined || s === null ? '' : s).toLowerCase()
    .replace(/[àáâãäå]/g, 'a').replace(/ç/g, 'c').replace(/[èéêë]/g, 'e').replace(/[ìíîï]/g, 'i')
    .replace(/[òóôõö]/g, 'o').replace(/[ùúûü]/g, 'u').replace(/ÿ/g, 'y').replace(/ñ/g, 'n')
    .replace(/[^a-z0-9]+/g, ' ').trim();
}

// ── Position GPS actuelle, avec repli sur la dernière position connue ───
function loadLastPos() {
  try {
    var p = JSON.parse(localStorage.getItem(LASTPOS_KEY));
    if (p && isFinite(p.lat) && isFinite(p.lon)) return p;
  } catch (e) {}
  return null;
}

function saveLastPos(p) {
  try { localStorage.setItem(LASTPOS_KEY, JSON.stringify(p)); } catch (e) {}
}

// cb(pos, repli) : pos = { lat, lon } ; repli = true si c'est la dernière position connue (GPS indisponible).
// pos = null seulement si le GPS échoue ET qu'aucune position n'a jamais été obtenue.
function getCurrentPos(cb) {
  var done = false, guard = null;
  function end(pos, fallback) {
    if (done) return;
    done = true;
    clearTimeout(guard);
    cb(pos, fallback);
  }
  function useLast(why) {
    var last = loadLastPos();
    console.log(LOG + why + (last ? ' : dernière position connue utilisée' : ' : aucune position connue'));
    end(last, true);
  }
  // Certains téléphones n'appellent jamais les callbacks de géolocalisation
  guard = setTimeout(function() { useLast('géolocalisation bloquée'); }, 25000);
  try {
    navigator.geolocation.getCurrentPosition(function(pos) {
      var last = loadLastPos() || {};
      var p = { lat: pos.coords.latitude, lon: pos.coords.longitude, ts: Date.now(),
                city: last.city, country: last.country, cLat: last.cLat, cLon: last.cLon, cLang: last.cLang };
      saveLastPos(p);
      end(p, false);
    }, function(err) {
      useLast('position indisponible : ' + (err && err.message));
    }, { timeout: 15000, maximumAge: 15 * 60 * 1000 });
  } catch (ex) {
    useLast('géolocalisation impossible');
  }
}

// ── Géocodage ───────────────────────────────────────────────────────────
// Coordonnées → ville et pays (BigDataCloud, langue choisie dans la page de réglages). cb({ city, country }) ou cb(null).
function reverseGeocode(lat, lon, lang, cb) {
  var url = 'https://api.bigdatacloud.net/data/reverse-geocode-client' +
            '?latitude=' + encodeURIComponent(lat) +
            '&longitude=' + encodeURIComponent(lon) +
            '&localityLanguage=' + (lang === 1 ? 'en' : 'fr');
  getJson(url, 10000, function(d) {
    if (!d) { cb(null); return; }
    var city = d.city || d.locality || d.principalSubdivision || '';
    if (!city && !d.countryName) { cb(null); return; }
    cb({ city: String(city), country: String(d.countryName || '') });
  });
}

// Ville (+ pays facultatif) → coordonnées (Open-Meteo geocoding, gratuit, sans clé).
// cb('ok', { name, country, lat, lon }) · cb('none') : introuvable · cb('error') : réseau.
function forwardGeocode(city, country, lang, cb) {
  var url = 'https://geocoding-api.open-meteo.com/v1/search?name=' + encodeURIComponent(city) +
            '&count=10&format=json&language=' + (lang === 1 ? 'en' : 'fr');
  getJson(url, 10000, function(d) {
    if (!d) { cb('error'); return; }
    var res = d.results || [];
    var c = norm(country), pick = null;
    for (var i = 0; i < res.length; i++) {
      var r = res[i];
      if (!c) { pick = r; break; }
      var rc = norm(r.country);
      if (rc === c || norm(r.country_code) === c || (c.length >= 3 && rc.indexOf(c) === 0)) { pick = r; break; }
    }
    if (!pick) { cb('none'); return; }
    cb('ok', { name: pick.name, country: pick.country || '', lat: pick.latitude, lon: pick.longitude });
  });
}

// Nom de la ville de la position GPS actuelle. Réutilise le nom déjà trouvé si on a bougé de moins de 3 km
// (pas de requête inutile à chaque rafraîchissement) ; en cas d'échec du service, ce nom, sinon « METEO ».
function currentCityName(pos, lang, cb) {
  var L = loadLastPos() || {};
  var l = (lang === 1) ? 'en' : 'fr';
  var near = L.city && isFinite(L.cLat) && isFinite(L.cLon) && distKm(pos.lat, pos.lon, L.cLat, L.cLon) < 3;
  if (near && L.cLang === l) { cb(L.city); return; }
  reverseGeocode(pos.lat, pos.lon, lang, function(g) {
    if (g && g.city) {
      var p = loadLastPos() || { lat: pos.lat, lon: pos.lon, ts: Date.now() };
      p.city = g.city; p.country = g.country; p.cLat = pos.lat; p.cLon = pos.lon; p.cLang = l;
      saveLastPos(p);
      cb(g.city);
    } else {
      cb(near ? L.city : 'METEO');
    }
  });
}

// ── Enregistrement des localisations (à la fermeture de la page de réglages) ──
// Chaque champ LOCk est analysé (parseLocInput) puis résolu SEULEMENT s'il a changé depuis la dernière fois :
//  - « GPS » : coordonnées de la position actuelle (ou la dernière connue) + ville / pays trouvés ;
//  - deux nombres (« 44.84, -0.58 ») : coordonnées ; la ville / le pays sont cherchés ;
//  - « Ville » ou « Ville, Pays » : les coordonnées sont cherchées ;
//  - vide : la localisation est supprimée (les affichages qui l'utilisaient retombent sur la position actuelle).
// Le champ est ensuite réécrit sous forme normalisée (« Bordeaux, France ») : la page l'affiche à la réouverture.
function parseLocInput(txt) {
  var t = String(txt === undefined || txt === null ? '' : txt).trim();
  if (!t) return { kind: 'empty' };
  if (/^(gps|@|ici|here)$/i.test(t)) return { kind: 'gps' };
  var m = t.match(/^(-?\d+(?:[.,]\d+)?)\s*[;,\s]\s*(-?\d+(?:[.,]\d+)?)$/);
  if (m) {
    var la = parseCoord(m[1], 90), lo = parseCoord(m[2], 180);
    return (la !== null && lo !== null) ? { kind: 'coords', lat: la, lon: lo } : { kind: 'bad' };
  }
  var i = t.indexOf(','), city = (i < 0 ? t : t.substring(0, i)).trim(), country = (i < 0 ? '' : t.substring(i + 1)).trim();
  return city ? { kind: 'city', city: city, country: country } : { kind: 'bad' };
}

function processLocations(done) {
  var msgs = [], n = 0;
  var lang = readSettings().lang;
  function T(fr, en) { return lang === 1 ? en : fr; }
  function r4(v) { return Math.round(v * 10000) / 10000; }

  function resolveOne(idx, cb) {
    var key = 'LOC' + (idx + 1);
    var label = T('Localisation ', 'Location ') + (idx + 1);
    var txt = String(loadRawSettings()[key] || '').trim();
    var store = loadLocStore(), old = store[idx];
    if (old ? txt === old.txt : !txt) { cb(); return; }          // inchangée : rien à faire

    // entry : objet = nouvelle localisation · null = supprimée · undefined = inchangée (échec) ;
    // le champ est réécrit avec le texte normalisé (ou « text » si fourni)
    function finish(entry, note, text) {
      if (entry !== undefined) { store[idx] = entry; saveLocStore(store); }
      var r = loadRawSettings();
      if (entry) r[key] = entry.txt; else if (text !== undefined) r[key] = text;
      storeRawSettings(r);
      if (note) msgs.push(label + ' : ' + note);
      cb();
    }
    function make(lat, lon, city, country) {
      var e = { lat: lat, lon: lon, city: city || '', country: country || '' };
      e.txt = locText(e);
      return e;
    }

    var p = parseLocInput(txt);
    if (p.kind === 'empty') { finish(null, null, ''); return; }
    if (p.kind === 'bad') {
      finish(undefined, T('saisie non reconnue (« Ville, Pays », coordonnées ou « GPS »)', 'input not recognized (« City, Country », coordinates or « GPS »)'));
      return;
    }
    if (p.kind === 'gps') {
      getCurrentPos(function(pos, fallback) {
        if (!pos) { finish(undefined, T('position actuelle indisponible', 'current position unavailable'), old ? old.txt : ''); return; }
        reverseGeocode(pos.lat, pos.lon, lang, function(g) {
          finish(make(r4(pos.lat), r4(pos.lon), g ? g.city : '', g ? g.country : ''),
                 fallback ? T('remplie avec la dernière position connue', 'filled with the last known position')
                          : T('remplie avec la position actuelle', 'filled with the current position'));
        });
      });
      return;
    }
    if (p.kind === 'coords') {
      reverseGeocode(p.lat, p.lon, lang, function(g) {
        finish(make(p.lat, p.lon, g ? g.city : '', g ? g.country : ''),
               g ? T('ville trouvée à partir des coordonnées', 'city found from the coordinates') : null);
      });
      return;
    }
    forwardGeocode(p.city, p.country, lang, function(status, r) {   // ville (+ pays facultatif)
      if (status === 'ok') {
        finish(make(r.lat, r.lon, r.name || p.city, r.country || p.country), T('coordonnées trouvées', 'coordinates found'));
      } else if (status === 'none') {
        finish(null, T('ville introuvable (vérifiez le nom et le pays, ou saisissez les coordonnées)', 'city not found (check the name and country, or enter the coordinates)'), txt);
      } else {
        finish(undefined, T('recherche impossible (réseau), réessayez en enregistrant à nouveau', 'search failed (network), save again to retry'));
      }
    });
  }

  function next() {
    if (n >= MAX_LOCS) {
      try { if (msgs.length) localStorage.setItem(LOCMSG_KEY, msgs.join(' — ')); } catch (e) {}
      done();
      return;
    }
    resolveOne(n++, next);
  }
  next();
}

// ═════════════════════════════════════════════════════════════════════════
// Plan de récupération : une seule requête par localisation RÉELLEMENT affichée
// ═════════════════════════════════════════════════════════════════════════
// Complications qui affichent de la météo (valeurs de COMPLICATION / COMP_TOP / COMP_BOTTOM, voir main.c)
function sideUsesWx(c) { return c === 3 || c === 6 || c === 7 || c === 8 || c === 9 || c === 10; }
function rowUsesWx(c)  { return c === 0 || c === 3 || c === 4 || c === 9 || c === 10; }

// Retourne { keys: [clés de localisation], loc: { clé: { mask, aqi, env, name, trend } }, banner } :
//   mask  = emplacements de la montre alimentés par cette localisation (bit 0 case, bit 1 haut, bit 2 bas)
//   aqi   = qualité de l'air affichée quelque part (requête air-quality en plus)
//   env   = humidité / UV / vent affichés quelque part (variables horaires en plus)
//   name  = le nom de la ville est affiché dans le bandeau du bas
//   trend = la tendance du jour est affichée dans le bandeau (une seule localisation pour tous les affichages)
//   banner = { single: clé } (« VILLE : TENDANCE ») ou { groups: [{ key, nums: [1, 2, 3] }] } (« 1|2 VILLE 3|VILLE »)
// Affichages concernés par le bandeau : ceux dont la complication utilise la météo ; si aucun n'en utilise, les trois.
// Une localisation que rien n'affiche n'est jamais interrogée ; une complication sans météo (pas, FC…) non plus.
function buildPlan(s, locs) {
  var plan = { keys: [], loc: {}, banner: null };
  function need(key) {
    if (!plan.loc[key]) { plan.loc[key] = { mask: 0, aqi: false, env: false, name: false, trend: false }; plan.keys.push(key); }
    return plan.loc[key];
  }
  var shown = [];   // { n: numéro d'affichage 1..3, key }
  if (sideUsesWx(s.comp)) {
    var a = need(resolveLoc(s.locSide, locs));
    a.mask |= 1;
    if (s.comp === 9) a.aqi = true;
    if (s.comp === 7 || s.comp === 8 || s.comp === 10) a.env = true;
    shown.push({ n: 1, key: resolveLoc(s.locSide, locs) });
  }
  if (rowUsesWx(s.compTop)) {
    var b = need(resolveLoc(s.locTop, locs));
    b.mask |= 2;
    if (s.compTop === 9) b.aqi = true;
    if (s.compTop === 9 || s.compTop === 10) b.env = true;
    shown.push({ n: 2, key: resolveLoc(s.locTop, locs) });
  }
  if (rowUsesWx(s.compBot)) {
    var c = need(resolveLoc(s.locBot, locs));
    c.mask |= 4;
    if (s.compBot === 9) c.aqi = true;
    if (s.compBot === 9 || s.compBot === 10) c.env = true;
    shown.push({ n: 3, key: resolveLoc(s.locBot, locs) });
  }
  if (s.banCustom) return plan;   // bandeau en texte personnalisé : ni ville ni tendance à récupérer
  if (!shown.length) {   // aucune complication météo : le bandeau se base sur les trois choix de localisation
    shown = [{ n: 1, key: resolveLoc(s.locSide, locs) }, { n: 2, key: resolveLoc(s.locTop, locs) }, { n: 3, key: resolveLoc(s.locBot, locs) }];
  }
  var groups = [];
  shown.forEach(function(d) {
    for (var i = 0; i < groups.length; i++) if (groups[i].key === d.key) { groups[i].nums.push(d.n); return; }
    groups.push({ key: d.key, nums: [d.n] });
  });
  if (groups.length === 1) {
    var one = need(groups[0].key);
    one.name = true; one.trend = true;
    plan.banner = { single: groups[0].key };
  } else {
    groups.forEach(function(g) { need(g.key).name = true; });
    plan.banner = { groups: groups };
  }
  return plan;
}

// Signature du plan : change dès qu'un cycle de la montre ou un réglage modifie ce qui doit être récupéré ou affiché
function planSig() {
  var p = buildPlan(readSettings(), getLocations());
  var parts = [], keys = p.keys.slice().sort();
  for (var i = 0; i < keys.length; i++) {
    var n = p.loc[keys[i]];
    parts.push(keys[i] + ':' + n.mask + (n.aqi ? 'a' : '') + (n.env ? 'e' : '') + (n.name ? 'n' : '') + (n.trend ? 't' : ''));
  }
  var b = !p.banner ? 'C' : p.banner.single !== undefined ? 'S' + p.banner.single
        : p.banner.groups.map(function(g) { return g.key + '=' + g.nums.join(''); }).join(',');
  return parts.join(';') + '|' + b;
}

// Texte du bandeau du bas, ou null s'il manque des données (récupération échouée : on garde l'ancien bandeau)
//   une seule localisation : « VILLE : TENDANCE »
//   plusieurs : « #1|2 BORDEAUX;3 PARIS » (« # » = format à groupes ; les affichages de même ville sont fusionnés)
function bannerText(plan, cache) {
  var b = plan.banner;
  if (!b) return null;
  function clean(nm) { return String(nm || '').replace(/[;:|#]/g, ' ').replace(/\s+/g, ' ').trim().substring(0, 16).trim(); }
  if (b.single !== undefined) {
    var e = cache[b.single];
    if (!e || !e.name) return null;
    return clean(e.name) + ' : ' + (e.trend || 'METEO');
  }
  var out = [];
  for (var i = 0; i < b.groups.length; i++) {
    var en = cache[b.groups[i].key];
    if (!en || !en.name) return null;
    var nm = clean(en.name), hit = null;
    for (var j = 0; j < out.length; j++) if (out[j].name === nm) hit = out[j];
    if (hit) hit.nums = hit.nums.concat(b.groups[i].nums).sort();
    else out.push({ name: nm, nums: b.groups[i].nums.slice() });
  }
  return '#' + out.map(function(g) { return g.nums.join('|') + ' ' + g.name; }).join(';');
}

// ── Cache : un enregistrement par localisation ──────────────────────────
// Ouvrir le menu ou les notifications ferme puis relance le cadran, donc le JS redémarre (évènement « ready »).
// Au lieu de tout recharger à chaque fois, on renvoie la dernière prévision de chaque localisation si elle a moins
// de 60 min ET si elle contient tout ce qui est affiché maintenant (qualité de l'air, indicateurs, ville, tendance).
var CACHE_KEY = 'satsuki-wx-cache-v4';   // { clé de localisation : { ts, sig, unit, period, lang, has, f, name, trend } }
var CACHE_TTL = 60 * 60 * 1000;          // = délai normal de rafraîchissement

function loadCache() {
  try {
    var c = JSON.parse(localStorage.getItem(CACHE_KEY));
    if (c && typeof c === 'object') return c;
  } catch (e) {}
  return {};
}

function saveCache(c) {
  try { localStorage.setItem(CACHE_KEY, JSON.stringify(c)); } catch (e) {}
}

// Identité d'une localisation dans le cache : ses coordonnées et son nom (modifiés → cache invalide)
function locSig(key, locs) {
  if (key === 'cur') return 'cur';
  var i = parseInt(key, 10);
  return locs[i].lat + ',' + locs[i].lon + ',' + formatCityName(locDisplayName(locs[i], i));
}

function cacheOk(e, key, need, ctx, locs, maxAge) {
  if (!e || !e.f || !e.has) return false;
  var age = Date.now() - e.ts;
  if (!(age >= 0 && age < maxAge)) return false;
  return e.sig === locSig(key, locs) && e.unit === ctx.unit && e.period === ctx.period && e.lang === ctx.lang &&
         (!need.aqi || e.has.aqi) && (!need.env || e.has.env) && (!need.name || e.has.name) && (!need.trend || e.has.trend);
}

// ── Requêtes météo d'une localisation ───────────────────────────────────
// Qualité de l'air : API air-quality d'Open-Meteo (AQI européen courant), -1 en cas d'échec ou de zone non couverte.
function fetchAirQuality(lat, lon, cb) {
  var url = 'https://air-quality-api.open-meteo.com/v1/air-quality?latitude=' + lat.toFixed(3) +
            '&longitude=' + lon.toFixed(3) + '&current=european_aqi';
  getJson(url, 10000, function(d) {
    var a = (d && d.current) ? d.current.european_aqi : null;
    cb((typeof a === 'number' && isFinite(a)) ? Math.max(0, Math.round(a)) : -1);
  });
}

// Prévisions Open-Meteo : les variables d'humidité / UV / vent ne sont demandées que si elles sont affichées
function requestForecast(lat, lon, need, ctx, cb) {
  var hourly = 'temperature_2m,weather_code,is_day,precipitation' +
               (need.env ? ',relative_humidity_2m,uv_index,wind_speed_10m' : '');
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat.toFixed(3) +
            '&longitude=' + lon.toFixed(3) +
            '&hourly=' + hourly +
            '&daily=sunrise,sunset,weather_code,temperature_2m_max,temperature_2m_min' +
            '&forecast_days=6&timezone=auto&timeformat=unixtime&temperature_unit=' + ctx.unit;
  getJson(url, 15000, function(d) {
    if (!d || !d.hourly) console.log(LOG + 'prévisions indisponibles (' + lat.toFixed(2) + ', ' + lon.toFixed(2) + ')');
    cb(d && d.hourly ? d : null);
  });
}

// Entrée de cache complète d'une localisation (messages construits plus tard selon les emplacements servis).
// city = nom de la ville du bandeau (null : pas demandé) ; la tendance du jour n'est calculée que si need.trend.
function buildEntry(key, locs, need, ctx, data, aqi, city) {
  var f = {}, slots = buildSlots(data, ctx.period);
  for (var i = 0; i < COLS; i++) f[forecastKey(i)] = slots[i];
  f[K.FORECAST_TS] = Math.floor(Date.now() / 1000);
  f[K.WX_UTC_OFF] = locOffset(data);
  addSun(f, data);
  addDaily(f, data);
  addEnv(f, data, aqi, need);
  return {
    ts: Date.now(), sig: locSig(key, locs), unit: ctx.unit, period: ctx.period, lang: ctx.lang,
    has: { aqi: need.aqi, env: need.env, name: city !== null, trend: need.trend },
    f: f,
    name: city !== null ? formatCityName(city) : null,
    trend: need.trend ? buildTrendWord(data) : null
  };
}

// cb(entrée) ou cb(null) en cas d'échec
function fetchLocation(key, need, ctx, locs, cb) {
  function withPos(pos) {
    if (!pos) { console.log(LOG + 'localisation ' + key + ' : aucune position'); cb(null); return; }
    requestForecast(pos.lat, pos.lon, need, ctx, function(data) {
      if (!data) { cb(null); return; }
      function withAqi(aqi) {
        function withCity(city) { cb(buildEntry(key, locs, need, ctx, data, aqi, city)); }
        if (key !== 'cur') {
          var i = parseInt(key, 10);
          withCity(locDisplayName(locs[i], i));
        } else if (need.name) {
          currentCityName(pos, ctx.lang, withCity);      // une requête seulement si on a bougé de plus de 3 km
        } else {
          withCity(null);
        }
      }
      if (need.aqi) fetchAirQuality(pos.lat, pos.lon, withAqi); else withAqi(-1);
    });
  }
  if (key === 'cur') getCurrentPos(function(pos) { withPos(pos); });   // GPS, sinon dernière position connue
  else withPos(locs[parseInt(key, 10)]);
}

// ── Envoi à la montre ───────────────────────────────────────────────────
// Un seul message par localisation : données + WX_SLOTS (emplacements servis). Le bandeau du bas est envoyé à part.
var sentSig = {};      // clé de localisation → signature du dernier message livré (évite de renvoyer la même chose)
var sentBanner = '';   // dernier texte de bandeau livré

function sendLoc(key, entry, need) {
  if (!need.mask) return;                  // localisation utilisée seulement par le bandeau : rien pour les complications
  var sig = entry.ts + '|' + need.mask;
  if (sentSig[key] === sig) return;
  var m = {};
  for (var k in entry.f) if (entry.f.hasOwnProperty(k)) m[k] = entry.f[k];
  m[K.WX_SLOTS] = need.mask;
  sentSig[key] = sig;
  sendMsg(m, 'météo ' + key, function() { delete sentSig[key]; });
}

function sendBanner(plan, cache) {
  var txt = bannerText(plan, cache);
  if (!txt || txt === sentBanner) return;
  sentBanner = txt;
  var m = {};
  m[K.WEATHER_TREND] = txt;
  sendMsg(m, 'bandeau', function() { sentBanner = ''; });
}

// ── Rafraîchissement ────────────────────────────────────────────────────
// Appelé au démarrage du JS, à l'échéance du cache, à la demande de la montre, après un changement de réglages.
// maxAgeMs : âge maximal d'une entrée de cache réutilisable (défaut 60 min).
function fetchWeather(maxAgeMs) {
  // Un essai bloqué depuis plus d'une minute ne doit pas empêcher le suivant
  if (fetching && Date.now() - lastFetchMs < 60 * 1000) return;
  fetching = true;
  lastFetchMs = Date.now();

  var maxAge = (typeof maxAgeMs === 'number') ? maxAgeMs : CACHE_TTL;
  var s = readSettings(), locs = getLocations(), plan = buildPlan(s, locs);
  var ctx = { unit: s.fahrenheit ? 'fahrenheit' : 'celsius', period: s.period, lang: s.lang };
  var cache = loadCache();
  var queue = plan.keys.slice(), failed = 0, nextAt = 0;

  function finish() {
    fetching = false;
    for (var k in cache) {            // les entrées de plus de 3 h n'ont plus d'intérêt (la montre les juge périmées)
      if (cache.hasOwnProperty(k) && !(Date.now() - cache[k].ts < 3 * CACHE_TTL)) delete cache[k];
    }
    saveCache(cache);
    sendBanner(plan, cache);          // ville + tendance, ou numéros d'affichage + villes
    if (failed) {
      // nouvel essai dans 5 min (REFRESH_MIN ≥ 5) : le cache fournit les localisations déjà à jour, seules les autres sont redemandées
      planWeather(Math.min(5 * 60 * 1000, refreshMs()));
    } else {
      // prochain rafraîchissement à l'échéance de la plus ancienne localisation
      planWeather(nextAt ? Math.max(30 * 1000, nextAt - Date.now()) : CACHE_TTL);
    }
    // Réglages changés pendant cette récupération : on la refait avec les nouveaux (le cache évite les requêtes inutiles)
    if (pendingRefetch) { pendingRefetch = false; fetchWeather(); }
  }

  function next() {
    if (!queue.length) { finish(); return; }
    var key = queue.shift(), need = plan.loc[key];
    var e = cache[key];
    function got(entry) {
      if (entry) {
        cache[key] = entry;
        saveCache(cache);
        sendLoc(key, entry, need);
        var exp = entry.ts + CACHE_TTL + 1000;
        if (!nextAt || exp < nextAt) nextAt = exp;
      } else {
        failed++;
        console.log(LOG + 'localisation ' + key + ' : récupération échouée');
      }
      next();
    }
    if (cacheOk(e, key, need, ctx, locs, maxAge)) got(e);
    else fetchLocation(key, need, ctx, locs, got);
  }
  next();
}

// ── Événements Pebble ────────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log(LOG + 'JS prêt');
  try { localStorage.removeItem('satsuki-last-forecast-v2'); localStorage.removeItem('satsuki-wx-cache-v3'); } catch (e) {}   // anciens caches
  fetchWeather();
  // Renvoie les réglages enregistrés (montre réinstallée, par exemple)
  sendSettings();
});

// Réglages seuls (sans prévisions) : la montre se redessine tout de suite
function sendSettings() {
  var m = addSettings({});
  if (Object.keys(m).length > 0) {
    sendMsg(m, 'réglages');
    // textes personnalisés : message séparé (tampon de réception de la montre)
    var s = readSettings(), c = {};
    c[K.CUSTOM_TOP] = s.custom[0]; c[K.CUSTOM_BOT] = s.custom[1]; c[K.CUSTOM_BAN] = s.custom[2];
    sendMsg(c, 'textes');
  }
}

// Ouverture de la page de réglages (appli Pebble → Réglages)
Pebble.addEventListener('showConfiguration', function() {
  // Page reconstruite à chaque ouverture : liste des sets et des localisations à jour
  clay = new Clay(buildClayConfig(), clayCustom, { autoHandleEvents: false });
  Pebble.openURL(clay.generateUrl());
});

// Fermeture : Clay enregistre les choix (getSettings écrit 'clay-settings')
Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  try {
    clay.getSettings(e.response, false);
  } catch (ex) {
    console.log(LOG + 'réglages illisibles : ' + ex);
    return;
  }
  try { processSetActions(); } catch (ex) { console.log(LOG + 'sets : ' + ex); }
  // Localisations : résolution (GPS, géocodage) puis réglages → montre, puis récupération.
  // Le cache fait que seules les localisations / données réellement modifiées sont redemandées
  // (unité °C/°F, langue, période, localisation ou complication changées).
  processLocations(function() {
    sendSettings();                          // un set chargé a déjà modifié les réglages
    if (fetching) pendingRefetch = true;
    else fetchWeather();
  });
});

Pebble.addEventListener('appmessage', function(e) {
  // La montre demande un rafraîchissement (reconnexion, prévisions trop vieilles) : données de plus de 15 min refaites
  // et tout renvoyé à la montre
  var p = e.payload || {};
  if (p[K.REQUEST_WEATHER] !== undefined) {
    sentSig = {};
    sentBanner = '';
    // données de plus de 15 min refaites, sauf si une récupération vient d'avoir lieu (le cache est alors renvoyé tel quel)
    fetchWeather(Date.now() - lastFetchMs > 60 * 1000 ? 15 * 60 * 1000 : CACHE_TTL);
  }
  // Cycle fait avec un mouvement du poignet : on mémorise le nouvel état comme réglages courants.
  // Si °C/°F, langue (nom de la ville), période météo ou ce qui doit être récupéré (nouvelle complication
  // météo, qualité de l'air…) ont changé, on relance la récupération : le cache évite de redemander le reste.
  if (p[K.STATE_PACK] !== undefined) {
    var before = readSettings(), sigBefore = planSig();
    applyStateToSettings(parseInt(p[K.STATE_PACK], 10) || 0);
    var after = readSettings();
    if (before.fahrenheit !== after.fahrenheit || before.lang !== after.lang || before.period !== after.period ||
        sigBefore !== planSig()) {
      if (fetching) pendingRefetch = true; else fetchWeather();
    }
  }
});
