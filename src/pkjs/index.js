/*
 * Satsuki Retro LCD Meteo – code côté téléphone
 * Récupère les prévisions Open-Meteo et les envoie à la montre.
 * Réglages : page de configuration Clay (langue, format de date, unité de
 * température ; voir clay-config.js) et config.js (reprise après échec).
 *
 * La montre reçoit 10 créneaux météo : à partir de la prochaine heure pleine locale, tous
 * les 1, 2, 3 ou 4 h selon la « période météo » choisie dans Clay
 * (ex. à 13h33, période 1 h : 14h, 15h, … 23h ; période 2 h : 14h, 16h, … 08h).
 * Elle les affiche (selon les complications choisies) sur 1 ou 2 rangées de 5.
 * Elle reçoit aussi la tendance des 6 jours (DAILY_0..5 : code météo, max, min, date).
 * Il envoie aussi les heures de lever/coucher du soleil (complication) et un texte « VILLE : TENDANCE » (ex. « BORDEAUX : PLUIE 8% »).
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
      out.push((isNaN(v) || v < 0 || v > 10) ? 255 : v);
    }
  }
  return out;
}

function actionFrom(v) {
  var a = parseInt(v, 10);
  return (a === 1) ? 1 : 0;
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
    compTop: Math.max(0, Math.min(10, parseInt(s.COMP_TOP, 10) || 0)),
    compBot: Math.max(0, Math.min(10, parseInt(s.COMP_BOTTOM, 10) || 0)),
    redRing: parseInt(s.RED_RING_LATERAL, 10) === 1 ? 1 : 0,
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
  s.COMP_TOP         = String(Math.min(10, (v >> 18) & 15));
  s.COMP_BOTTOM      = String(Math.min(10, (v >> 22) & 15));
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
      s['CYC' + (k + 1) + '_' + slots[i]] = String((typeof v === 'number' && v >= 0 && v <= 10) ? v : -1);
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
  function drop(it) {   // champ retiré de la page quand il ne sert à rien
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
    }
    sending = false;
    setTimeout(pump, 1000);
  });
}

function sendMsg(msg, label) {
  outbox.push({ msg: msg, label: label, tries: 0 });
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

// Instants (secondes Unix) des COLS créneaux : heures pleines LOCALES, la
// première étant l'heure pleine strictement suivante (13h00 ou 13h42 → 14h00),
// puis un créneau tous les « period » heures (1 à 4, réglage Clay).
function slotTimes(nowSec, period) {
  var t = new Date(nowSec * 1000);
  t.setMinutes(0, 0, 0);                         // heure pleine locale courante
  var first = Math.round(t.getTime() / 1000) + 3600;
  var out = [];
  for (var i = 0; i < COLS; i++) out.push(first + i * period * 3600);
  return out;
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
// Retourne « VILLE : TENDANCE », par exemple :
//   "BORDEAUX : ENSOLEILLE"
//   "BORDEAUX : NUAGEUX"
//   "BORDEAUX : PLUIE 8%"
//   "BORDEAUX : NEIGE 12%"
//   "BORDEAUX : BROUILLARD 5%"
//   "BORDEAUX : ORAGE"
//
// Priorité au phénomène le plus dégradé : orage > neige > pluie > brouillard >
// nuageux > ensoleillé. Le pourcentage (neige, pluie, brouillard) est la part des
// heures concernées parmi les heures d'aujourd'hui présentes dans les données
// (24 h en temps normal). Orage, nuageux et ensoleillé n'ont pas de pourcentage ;
// « nuageux » s'affiche dès qu'une seule heure est nuageuse (codes 2 ou 3).
// Sans donnée pour aujourd'hui : « VILLE : METEO ».

function buildWeatherTrend(data, cityName) {
  cityName = formatCityName(cityName);
  var hr = data.hourly || {};
  var times = hr.time || [];
  var codes = hr.weather_code || [];
  var precip = hr.precipitation || [];

  var today = new Date();
  var year = today.getFullYear();
  var month = today.getMonth();
  var day = today.getDate();

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
    var d = new Date(times[i] * 1000);

    if (d.getFullYear() !== year ||
        d.getMonth() !== month ||
        d.getDate() !== day) {
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
    return cityName + ' : METEO';
  }

  // Priorité au phénomène le plus dégradé.
  if (counts.storm > 0) {
    return cityName + ' : ORAGE';
  }

  if (counts.snow > 0) {
    var snowPct = Math.round(counts.snow * 100 / totalHours);
    return cityName + ' : NEIGE ' + snowPct + '%';
  }

  if (counts.rain > 0) {
    var rainPct = Math.round(counts.rain * 100 / totalHours);
    return cityName + ' : PLUIE ' + rainPct + '%';
  }

  if (counts.fog > 0) {
    var fogPct = Math.round(counts.fog * 100 / totalHours);
    return cityName + ' : BROUILLARD ' + fogPct + '%';
  }

  if (counts.cloudy > 0) {
    return cityName + ' : NUAGEUX';
  }

  return cityName + ' : ENSOLEILLE';
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

// Humidité (%), UV (×10), vent (km/h ×10) à l'heure courante ; qualité de l'air (AQI européen) fournie à part.
// -1 = indisponible.
function addEnv(msg, data, aqi) {
  var hr = data.hourly || {};
  var idx = nearestHour(hr.time || [], Math.floor(Date.now() / 1000));
  function val(arr, mul) {
    var v = (idx >= 0 && arr) ? arr[idx] : undefined;
    return (typeof v === 'number' && isFinite(v)) ? Math.max(0, Math.round(v * mul)) : -1;
  }
  msg['HUMIDITY']    = val(hr.relative_humidity_2m, 1);
  msg['UV_INDEX']    = val(hr.uv_index, 10);
  msg['WIND_SPEED']  = val(hr.wind_speed_10m, 10);   // km/h (unité par défaut d'Open-Meteo) ; la montre convertit en mph
  msg['AIR_QUALITY'] = aqi;
}

// Qualité de l'air : API air-quality d'Open-Meteo (gratuite, sans clé), AQI européen courant.
// Appelle toujours « callback » (-1 en cas d'échec ou de zone non couverte).
function fetchAirQuality(lat, lon, callback) {
  var url = 'https://air-quality-api.open-meteo.com/v1/air-quality?latitude=' + lat.toFixed(3) +
            '&longitude=' + lon.toFixed(3) + '&current=european_aqi';
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.timeout = 10000;
  var done = false;
  var guard = setTimeout(function() {
    try { req.abort(); } catch (e) {}
    end(-1);
  }, 15000);
  function end(v) {
    if (done) return;
    done = true;
    clearTimeout(guard);
    callback(v);
  }
  req.onload = function() {
    var v = -1;
    try {
      if (req.status === 200) {
        var d = JSON.parse(req.responseText);
        var a = d && d.current ? d.current.european_aqi : null;
        if (typeof a === 'number' && isFinite(a)) v = Math.max(0, Math.round(a));
      }
    } catch (e) {}
    end(v);
  };
  req.onerror   = function() { end(-1); };
  req.ontimeout = function() { end(-1); };
  try { req.send(); } catch (ex) { end(-1); }
}

function addSun(msg, data) {
  var d = data.daily || {};
  var sr = d.sunrise || [], ss = d.sunset || [];
  for (var i = 0; i < 3; i++) {
    msg['SUNRISE_' + i] = (typeof sr[i] === 'number' && isFinite(sr[i])) ? sr[i] : 0;
    msg['SUNSET_' + i]  = (typeof ss[i] === 'number' && isFinite(ss[i])) ? ss[i] : 0;
  }
}

function buildSlots(data) {
  var hr  = data.hourly  || {};
  var times = hr.time || [];
  var nowSec = Math.floor(Date.now() / 1000);
  var slots = [];

  var targets = slotTimes(nowSec, readSettings().period);
  for (var i = 0; i < targets.length; i++) {
    var idx = nearestHour(times, targets[i]);
    var code, day, temp;
    if (idx >= 0) {
      code = (hr.weather_code || [])[idx];
      day  = (hr.is_day || [])[idx];
      temp = (hr.temperature_2m || [])[idx];
    }
    slots.push(pack(code, day === 1, temp, new Date(targets[i] * 1000).getHours()));
  }
  return slots;
}

function planWeather(ms) {
  if (weatherTimer) clearTimeout(weatherTimer);
  weatherTimer = setTimeout(fetchWeather, ms);
}

// ── Cache de la dernière prévision ───────────────────────────────────────
// Ouvrir le menu ou les notifications ferme puis relance le cadran, donc le JS
// redémarre (évènement « ready »). Au lieu de tout recharger à chaque fois, on
// renvoie la dernière prévision si elle a moins de 60 min.
var CACHE_KEY = 'satsuki-last-forecast-v2';   // v2 : le cache ne contient plus les réglages
var CACHE_TTL = 60 * 60 * 1000;   // = délai normal de rafraîchissement

function saveCache(msg) {
  try {
    localStorage.setItem(CACHE_KEY, JSON.stringify({ ts: Date.now(), msg: msg }));
  } catch (e) {}
}

function loadCache() {
  try {
    var c = JSON.parse(localStorage.getItem(CACHE_KEY));
    var age = c ? Date.now() - c.ts : -1;
    if (c && c.msg && age >= 0 && age < CACHE_TTL) return c;
  } catch (e) {}
  return null;
}

// Réutilise la dernière prévision si elle a moins de 60 min, sinon récupère
function fetchOrReuse() {
  var c = loadCache();
  if (!c) { fetchWeather(); return; }
  sendMsg(c.msg, 'prévisions (cache)');
  planWeather(CACHE_TTL - (Date.now() - c.ts));   // prochain fetch à l'échéance d'origine
}

function refreshMs() {
  return Math.max(5, parseInt(CFG.REFRESH_MIN, 10) || 30) * 60 * 1000;
}

function fetchFailed(why) {
  console.log('[Satsuki Retro LCD Meteo] ' + why);
  fetching = false;
  planWeather(Math.min(5 * 60 * 1000, refreshMs()));  // nouvel essai dans 5 min (REFRESH_MIN ≥ 5)
}

function requestForecast(lat, lon) {
  var unit = readSettings().fahrenheit ? 'fahrenheit' : 'celsius';
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat.toFixed(3) +
            '&longitude=' + lon.toFixed(3) +
            '&hourly=temperature_2m,weather_code,is_day,precipitation,relative_humidity_2m,uv_index,wind_speed_10m' +
            '&daily=sunrise,sunset,weather_code,temperature_2m_max,temperature_2m_min' +
            '&forecast_days=6&timezone=auto&timeformat=unixtime&temperature_unit=' + unit;

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.timeout = 15000;
  var done = false;
  // Garde-fou : sur certains téléphones une requête n'appelle jamais ses callbacks
  var guard = setTimeout(function() {
    if (done) return;
    done = true;
    try { req.abort(); } catch (e) {}
    fetchFailed('requête météo bloquée');
  }, 25000);
  function finish() {
    if (done) return false;
    done = true;
    clearTimeout(guard);
    return true;
  }
  req.onload = function() {
    if (!finish()) return;
    if (req.status !== 200) { fetchFailed('erreur HTTP ' + req.status); return; }
    try {
		var data = JSON.parse(req.responseText);
		var slots = buildSlots(data);

		reverseGeocode(lat, lon, function(cityName) {
		  fetchAirQuality(lat, lon, function(aqi) {
			
		  var trend = buildWeatherTrend(data, cityName);

		  var msg = {};

		  for (var i = 0; i < COLS; i++) {
			msg[forecastKey(i)] = slots[i];
		  }

		  msg[K.FORECAST_TS] = Math.floor(Date.now() / 1000);
		  msg[K.WEATHER_TREND] = trend;
		  addSun(msg, data);
		  addDaily(msg, data);
		  addEnv(msg, data, aqi);
		  // Les réglages ne sont plus joints aux prévisions : ils partent à l'ouverture (ready) et à la
		  // fermeture de la page de réglages, pour ne pas écraser un cycle fait avec les boutons de la montre.

		  saveCache(msg);
		  sendMsg(msg, 'prévisions + tendance');

		  fetching = false;

		  // Prochain rafraîchissement dans 60 min (fixe : REFRESH_MIN ne sert qu'en cas d'échec)
		  planWeather(60 * 60 * 1000);

		  // Réglages changés pendant cette récupération : on la refait avec les nouveaux
		  if (pendingRefetch) { pendingRefetch = false; fetchWeather(); }
		  });
		});
    } catch (ex) {
      fetchFailed('réponse illisible : ' + ex);
    }
  };
  req.onerror   = function() { if (finish()) fetchFailed('erreur réseau'); };
  req.ontimeout = function() { if (finish()) fetchFailed('délai dépassé'); };
  try {
    req.send();
  } catch (ex) {
    if (finish()) fetchFailed('envoi impossible : ' + ex);
  }
}

// Nom de la ville via BigDataCloud (langue choisie dans la page de réglages) ;
// « METEO » en cas d'échec (la montre le traduit en « WEATHER » en anglais).
// Les mots de tendance (PLUIE, ORAGE…) sont toujours envoyés en français : la
// montre les traduit selon CFG_LANGUAGE.
function reverseGeocode(lat, lon, callback) {
  var lang = (readSettings().lang === 1) ? 'en' : 'fr';
  var url = 'https://api.bigdatacloud.net/data/reverse-geocode-client' +
            '?latitude=' + encodeURIComponent(lat) +
            '&longitude=' + encodeURIComponent(lon) +
            '&localityLanguage=' + encodeURIComponent(lang);

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.timeout = 10000;

  req.onload = function() {
    if (req.status !== 200) {
      callback('METEO');
      return;
    }

    try {
      var data = JSON.parse(req.responseText);
      var city = data.city ||
                 data.locality ||
                 data.principalSubdivision ||
                 'METEO';

      callback(city.toUpperCase());
    } catch (e) {
      callback('METEO');
    }
  };

  req.onerror = function() {
    callback('METEO');
  };

  req.ontimeout = function() {
    callback('METEO');
  };

  try {
    req.send();
  } catch (e) {
    callback('METEO');
  }
}

function fetchWeather() {
  // Un essai bloqué depuis plus d'une minute ne doit pas empêcher le suivant
  if (fetching && Date.now() - lastFetchMs < 60 * 1000) return;
  fetching = true;
  lastFetchMs = Date.now();
  navigator.geolocation.getCurrentPosition(function(pos) {
    requestForecast(pos.coords.latitude, pos.coords.longitude);
  }, function(err) {
    fetchFailed('position indisponible : ' + err.message);
  }, { timeout: 15000, maximumAge: 15 * 60 * 1000 });
}

// ── Événements Pebble ────────────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log('[Satsuki Retro LCD Meteo] JS prêt');
  fetchOrReuse();
  // Renvoie les réglages enregistrés (montre réinstallée, par exemple)
  sendSettings();
});

// Réglages seuls (sans prévisions) : la montre se redessine tout de suite
function sendSettings() {
  var m = addSettings({});
  if (Object.keys(m).length > 0) {
    sendMsg(m, 'réglages');
  }
}

// Ouverture de la page de réglages (appli Pebble → Réglages)
Pebble.addEventListener('showConfiguration', function() {
  // Page reconstruite à chaque ouverture : liste des sets et choix remplacer / supprimer à jour
  clay = new Clay(buildClayConfig(), clayCustom, { autoHandleEvents: false });
  Pebble.openURL(clay.generateUrl());
});

// Fermeture : Clay enregistre les choix (getSettings écrit 'clay-settings')
Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  try {
    clay.getSettings(e.response, false);
  } catch (ex) {
    console.log('[Satsuki Retro LCD Meteo] réglages illisibles : ' + ex);
    return;
  }
  try { processSetActions(); } catch (ex) { console.log('[Satsuki Retro LCD Meteo] sets : ' + ex); }
  sendSettings();                          // réglages → montre (un set chargé a déjà modifié les réglages)
  if (fetching) pendingRefetch = true;     // unité °C/°F et langue de la ville :
  else fetchWeather();                     // on refait la récupération
});

Pebble.addEventListener('appmessage', function(e) {
  // La montre demande un rafraîchissement (reconnexion, prévisions trop vieilles)
  var p = e.payload || {};
  if (p[K.REQUEST_WEATHER] !== undefined) {
    if (Date.now() - lastFetchMs > 60 * 1000) fetchWeather();
  }
  // Cycle fait avec un mouvement du poignet : on mémorise le nouvel état comme réglages courants.
  // Si °C/°F, langue (nom de la ville) ou période météo ont changé, on refait la récupération.
  if (p[K.STATE_PACK] !== undefined) {
    var before = readSettings();
    applyStateToSettings(parseInt(p[K.STATE_PACK], 10) || 0);
    var after = readSettings();
    if (before.fahrenheit !== after.fahrenheit || before.lang !== after.lang || before.period !== after.period) {
      if (fetching) pendingRefetch = true; else fetchWeather();
    }
  }
});
