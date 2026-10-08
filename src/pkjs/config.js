/*
 * Satsuki Retro LCD Meteo – RÉGLAGES CÔTÉ TÉLÉPHONE (modifier ici, puis recompiler)
 *
 * La langue, le format de la date et l'unité de température se choisissent
 * dans la page de réglages de l'appli Pebble (voir clay-config.js). Ce fichier
 * ne contient plus que les réglages techniques. Il est empaqueté dans l'appli au
 * moment de `pebble build`. Les réglages d'affichage (couleurs, libellés,
 * complications…) sont dans src/c/config.h.
 *
 * La météo vient d'Open-Meteo (gratuit, sans clé) et le nom de la ville de
 * BigDataCloud (reverse geocoding, gratuit, sans clé). La position est donnée
 * par le téléphone : autorisez la localisation pour l'appli Pebble.
 */
module.exports = {
  // Délai de reprise, en minutes (minimum 5), après un échec de récupération :
  // nouvel essai au bout de min(5 min, REFRESH_MIN). Après un succès, les
  // prévisions sont rafraîchies toutes les 60 min (valeur fixe dans index.js).
  REFRESH_MIN: 30
};
