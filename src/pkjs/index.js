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
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

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
  FONT_STYLE: 'FONT_STYLE',            // téléphone → montre : 0..3 = 7 segments (Classic, Classic Mini, Modern, Modern Mini), 4..7 = 14 segments (mêmes)
  COMPLICATION: 'COMPLICATION',        // téléphone → montre : 0 pas, 1 FC, 2 distance, 3 soleil, 4 secondes, 5 batterie
  DIST_UNIT: 'DIST_UNIT',              // téléphone → montre : 0 = kilomètres, 1 = miles
  RED_RING_LATERAL: 'RED_RING_LATERAL', // téléphone → montre : 0 = bandes latérales masquées, 1 = affichées
  BATT_STYLE: 'BATT_STYLE',            // téléphone → montre : segments de la pile 0 = monochrome (rouge < 10 %), 1 = couleur
  COMP_TOP: 'COMP_TOP',                // téléphone → montre : rangée du haut : 0 météo horaire, 1 FC + distance, 2 calories + pas, 3 soleil, 4 tendance 3 jours, 5 FC + calories, 6 FC + pas, 7 distance + calories, 8 pas + distance
  COMP_BOTTOM: 'COMP_BOTTOM'           // téléphone → montre : rangée du bas (mêmes choix)
  // WEATHER_PERIOD (1..4 h) ne sert qu'au téléphone : les libellés d'heure arrivent déjà calculés
};

function forecastKey(i) {
  return 'FORECAST_' + i;   // colonnes 0..9
}

// ── Réglages choisis dans la page Clay ───────────────────────────────────
// Clay les mémorise dans localStorage ('clay-settings'). Valeurs par défaut
// (rien d'enregistré) : français, JJ-MM, °C.
function readSettings() {
  var s = {};
  try { s = JSON.parse(localStorage.getItem('clay-settings') || '{}') || {}; } catch (e) { s = {}; }
  return {
    saved: (s.LANGUAGE !== undefined || s.DATE_FORMAT !== undefined || s.TIME_FORMAT !== undefined || s.ICON_STYLE !== undefined || s.GHOST_LEVEL !== undefined || s.FONT_STYLE !== undefined ||
            s.TEMP_UNIT !== undefined || s.RED_RING_LATERAL !== undefined ||
            s.COMPLICATION !== undefined || s.DIST_UNIT !== undefined ||
            s.WEATHER_PERIOD !== undefined || s.BATT_STYLE !== undefined || s.COMP_TOP !== undefined || s.COMP_BOTTOM !== undefined),
    // icônes en couleurs par défaut (aussi si ce réglage n'a jamais été enregistré)
    iconColor: (s.ICON_STYLE === undefined || parseInt(s.ICON_STYLE, 10) === 1) ? 1 : 0,
    ghost: Math.max(0, Math.min(2, parseInt(s.GHOST_LEVEL, 10) || 0)),
    fontStyle: Math.max(0, Math.min(7, parseInt(s.FONT_STYLE, 10) || 0)),
    time12: parseInt(s.TIME_FORMAT, 10) === 1 ? 1 : 0,
    comp: Math.max(0, Math.min(5, parseInt(s.COMPLICATION, 10) || 0)),
    dist: parseInt(s.DIST_UNIT, 10) === 1 ? 1 : 0,
    battStyle: parseInt(s.BATT_STYLE, 10) === 1 ? 1 : 0,
    period: Math.max(1, Math.min(4, parseInt(s.WEATHER_PERIOD, 10) || 1)),
    compTop: Math.max(0, Math.min(8, parseInt(s.COMP_TOP, 10) || 0)),
    compBot: Math.max(0, Math.min(8, parseInt(s.COMP_BOTTOM, 10) || 0)),
    redRing: parseInt(s.RED_RING_LATERAL, 10) === 1 ? 1 : 0,
    lang: parseInt(s.LANGUAGE, 10) === 1 ? 1 : 0,
    date: parseInt(s.DATE_FORMAT, 10) === 1 ? 1 : 0,
    fahrenheit: parseInt(s.TEMP_UNIT, 10) === 1
  };
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
    msg[K.RED_RING_LATERAL] = s.redRing;
    msg[K.BATT_STYLE] = s.battStyle;
    msg[K.COMP_TOP] = s.compTop;
    msg[K.COMP_BOTTOM] = s.compBot;
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
            '&hourly=temperature_2m,weather_code,is_day,precipitation' +
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
			
		  var trend = buildWeatherTrend(data, cityName);

		  var msg = {};

		  for (var i = 0; i < COLS; i++) {
			msg[forecastKey(i)] = slots[i];
		  }

		  msg[K.FORECAST_TS] = Math.floor(Date.now() / 1000);
		  msg[K.WEATHER_TREND] = trend;
		  addSun(msg, data);
		  addDaily(msg, data);
		  addSettings(msg);

		  sendMsg(msg, 'prévisions + tendance');

		  fetching = false;

		  // Prochain rafraîchissement dans 60 min (fixe : REFRESH_MIN ne sert qu'en cas d'échec)
		  planWeather(60 * 60 * 1000);

		  // Réglages changés pendant cette récupération : on la refait avec les nouveaux
		  if (pendingRefetch) { pendingRefetch = false; fetchWeather(); }
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
  fetchWeather();
  // Renvoie les réglages enregistrés (montre réinstallée, par exemple)
  sendSettings();
});

// Réglages seuls (sans prévisions) : la montre se redessine tout de suite
function sendSettings() {
  var m = addSettings({});
  if (Object.keys(m).length > 0) sendMsg(m, 'réglages');
}

// Ouverture de la page de réglages (appli Pebble → Réglages)
Pebble.addEventListener('showConfiguration', function() {
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
  sendSettings();                          // langue + format de date → montre
  if (fetching) pendingRefetch = true;     // unité °C/°F et langue de la ville :
  else fetchWeather();                     // on refait la récupération
});

Pebble.addEventListener('appmessage', function(e) {
  // La montre demande un rafraîchissement (reconnexion, prévisions trop vieilles)
  var p = e.payload || {};
  if (p[K.REQUEST_WEATHER] !== undefined) {
    if (Date.now() - lastFetchMs > 60 * 1000) fetchWeather();
  }
});
