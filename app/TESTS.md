# Tests sur le Cardputer ADV

À faire quand le matériel est disponible. Garder le moniteur série ouvert (115200 bauds) pendant
chaque test et copier **tout** le log dans la session suivante : les lignes `[CPADV]` disent ce
que le firmware a détecté.

## Avant tout

- [ ] Antenne LoRa vissée sur le Cap **avant** de brancher le Cardputer (émettre sans antenne abîme la radio).
- [ ] Télécharger les fichiers depuis GitHub : onglet **Actions** du dépôt, dernière exécution
      « Cardputer ADV » réussie, section **Artifacts**.
- [ ] Flasher le fichier `...-merged.bin` à l'adresse `0x0` avec ESPConnect
      (https://thelastoutpostworkshop.github.io/ESPConnect/) ou :
      `esptool.py --chip esp32s3 write_flash 0x0 <fichier>-merged.bin`

## Diagnostic série (tous les firmwares) — à faire en premier

- [ ] Ouvrir le moniteur série **après** avoir allumé : le log de démarrage apparaît quand même
      (`---- [CPADV] boot log replay ----`).
- [ ] Bannière : `==== MeshCore Cardputer ADV - ... ====`, version, `reset reason: power-on`,
      `running from partition 'app0'` (ou `app1` en mode téléphone).
- [ ] Ligne `I2C internal bus (SDA 8, SCL 9): N device(s): 0x18(ES8311 audio) 0x34(TCA8418 keyboard) 0x43(PI4IOE ...)`.
      **Noter la liste exacte.** Si 0x34 manque : le clavier ne peut pas marcher (problème matériel / bus).
- [ ] Une ligne `BEAT` toutes les 10 s.
- [ ] (autonome) Taper `help` + Entrée dans le moniteur : la liste des commandes s'affiche.

## Clavier (autonome) — si des touches ne marchent pas

- [ ] Log au démarrage : `TCA8418 keyboard ready (7x8 matrix ...)`, `INT high (idle)`.
- [ ] Commande `keys on`, puis taper chaque rangée du clavier : une ligne `raw 0x.. key .. down -> r. c. -> KEY ...`
      par appui. **Copier les lignes des touches qui ne font rien ou la mauvaise lettre.**
- [ ] La ligne `BEAT` : `keys ok N ev 0 err INT hi` (N augmente quand on tape ; `err` reste à 0).

## GPS (autonome) — dehors, ciel dégagé

- [ ] Log : `GPS listening at 115200 baud` puis `NMEA detected at 115200 baud` en moins de 3 s.
      Si `nothing received at ... baud` en boucle : le module ne parle pas (Cap mal enfoncé ?).
- [ ] `gps raw on` : des lignes `NMEA $GNRMC,...` / `$GNGGA,...` chaque seconde. `gps raw off` pour arrêter.
- [ ] Après 1 à 5 min : `GPS fix 43.xxxxx, 1.xxxxx, N sats` et `clock set from GPS`.

## Double démarrage (`cardputer_adv_dual`)

- [ ] Au démarrage : `dual-boot image: app0 standalone (...), app1 companion (...), running standalone`.
- [ ] Réglages → Mode téléphone affiche `Bluetooth >` ; Entrée, Entrée → le Cardputer redémarre,
      log `restarting into the companion (phone app) firmware`, puis bannière `companion BLE`, `partition 'app1'`.
- [ ] En mode téléphone : log `phone mode: press esc ... 3 times`. L'appli MeshCore trouve l'appareil.
- [ ] En mode téléphone, GPS : l'activer dans l'appli (réglages de l'appareil → GPS) ; log `GPS detected` au démarrage.
- [ ] Appuyer 3 fois sur `esc` : `esc 1/3`, `2/3`, `3/3`, « Mode clavier... » à l'écran, retour au firmware autonome.
- [ ] Le nom / la clé publique du nœud sont les mêmes dans les deux modes (même identité).

## Phase 1 — firmware companion (`cardputer_adv_companion_ble`)

- [ ] Au démarrage, le log contient `BOARD  board begin` puis `M5Unified board id=... (ADV=...)` avec les deux nombres identiques.
- [ ] Ce firmware n'utilise **pas** le clavier (seulement le bouton G0 sur le côté) : c'est normal.
- [ ] GPS : éteint tant qu'il n'est pas activé dans l'appli ; log `GPS detected` ou `No GPS detected` au démarrage.
- [ ] Le log indique `Cap LoRa-1262 detected` (ou `assuming Cap LoRa868` si c'est un Cap 868).
- [ ] Si Cap 1262 : la ligne `PI4IOE dir=... -> P0 HIGH (ok)`.
- [ ] La ligne `RADIO  SX1262 OK: 869.618 MHz ...`. Sinon, noter le code d'erreur affiché juste avant.
- [ ] L'écran affiche l'interface MeshCore (texte lisible, pas à l'envers, pas décalé).
- [ ] L'appli MeshCore sur le téléphone trouve l'appareil en Bluetooth ; le code PIN s'affiche sur l'écran.
- [ ] Un message envoyé depuis le téléphone (via le Cardputer) est reçu par un autre nœud, et inversement.
- [ ] Le niveau de batterie affiché dans l'appli est plausible (3,3 à 4,2 V).

## Phase 2 — firmware autonome (`cardputer_adv_standalone`)

- [ ] L'écran d'accueil s'affiche avec la barre d'état en haut et la ligne d'aide en bas.
- [ ] Le log indique `TCA8418 keyboard ready`.
- [ ] `S` ouvre les Réglages, `` ` `` revient à l'accueil. `M`, `C`, `G`, `K` ouvrent les écrans « Phase x ».
- [ ] Dans les Réglages : `;` et `.` montent et descendent, `,` et `/` changent la valeur.
- [ ] Renommer l'appareil : Entrée sur « Nom », effacer, taper un nom avec un accent (Alt+e donne é,
      appuyer encore pour è ê ë), Entrée. Le nouveau nom apparaît sur l'accueil.
- [ ] Fn + `` ` `` dans la saisie annule sans enregistrer.
- [ ] Chaque touche du clavier tapée dans la saisie du nom donne le bon caractère, Shift compris
      (tester toutes les lettres, chiffres et symboles ; noter celles qui sont fausses).
- [ ] Éteindre et rallumer : les réglages sont conservés (log `settings loaded`).
- [ ] Veille écran à 15 s : l'écran s'éteint, une touche le rallume (sans exécuter la touche).
- [ ] Réglages > Système : la mémoire libre et le temps « Allumé depuis » se mettent à jour.
- [ ] Laisser tourner 1 h : dans le log, la ligne `heap free ... min ...` toutes les minutes ne baisse pas régulièrement.

## Phase 3 — messagerie autonome (`cardputer_adv_standalone`)

Il faut un **second nœud MeshCore** (téléphone + appli avec un autre appareil, ou un autre Cardputer).

- [ ] Au démarrage, le log contient `mesh ready: ... contacts` puis, 3 s après, `advert sent (flood)`.
      Premier démarrage : `new identity created (saved)`.
- [ ] Le second nœud voit apparaître le Cardputer dans ses contacts (avec le nom choisi dans Réglages).
- [ ] Quand le second nœud s'annonce, un bandeau « Nouveau contact » apparaît et il est listé dans `C`.
- [ ] Message reçu du second nœud : bandeau + double bip, pastille orange sur l'accueil, compteur `*1` en haut.
- [ ] `M` → la conversation est en haut de la liste ; Entrée l'ouvre ; la pastille disparaît.
- [ ] Répondre (texte avec accents Alt+e…) : la bulle passe de « envoyé » à « reçu » (vert) en quelques secondes.
- [ ] Éteindre le second nœud et envoyer : « renvoyé » puis « échec » (rouge) au bout de ~30 s, bandeau + son grave.
- [ ] Canal Public : un message envoyé depuis l'appli sur Public arrive dans « Public » avec le nom de l'expéditeur.
- [ ] `J` puis `test` (devient `#test`) ; rejoindre le même canal `#test` dans l'appli : les messages passent
      dans les deux sens. **À vérifier : la clé des canaux #nom doit être la même que celle de l'appli.**
- [ ] Contacts : `F` met en favori (étoile, remonte en haut), `D` deux fois supprime.
- [ ] Fiche contact (Entrée) : chemin, « entendu il y a… ». Avec position du contact et GPS : distance et cap.
- [ ] 20 messages échangés dans les deux sens sans erreur ; statut « reçu » sur chacun.
- [ ] Éteindre / rallumer : conversations, messages, contacts et canaux toujours là (log `contacts loaded`).
- [ ] Écran en veille : un message reçu rallume l'écran avec le bandeau.
- [ ] Laisser tourner 1 h avec du trafic : la ligne `heap free ... min ...` du log reste stable.

## Phase 4 — GPS et boussole (`cardputer_adv_standalone`)

À faire **en extérieur**, ciel dégagé.

- [ ] Au démarrage, le log indique `GPS: NMEA detected at 115200 baud` (ou une autre vitesse).
      Si `GPS: listening at ...` tourne en boucle sans détection : noter les vitesses essayées et
      les éventuelles `checksum errors` (câblage / broches RX-TX inversées).
- [ ] `G` : l'état passe de « recherche... » (orange) à « fix 3D » (vert) en moins de 60 s à froid.
      Noter le temps exact et le nombre de satellites.
- [ ] Les coordonnées correspondent à ta position (comparer avec le téléphone), altitude plausible.
- [ ] En marchant : la vitesse et la direction s'affichent ; à l'arrêt « à l'arrêt ».
- [ ] L'heure de la barre d'état devient correcte toute seule (log `clock set from GPS`).
- [ ] `A` sur l'écran GPS : le second nœud reçoit l'annonce avec la position (visible sur sa carte).
- [ ] Réglages > « Partager position » = oui : les annonces suivantes contiennent la position.
- [ ] Un contact qui partage sa position : la liste des contacts affiche sa distance ; la distance
      est cohérente avec celle de l'appli MeshCore.
- [ ] Fiche contact → `B` : la boussole pointe vers lui. Immobile : « haut = nord » (tenir le Cardputer
      orienté nord pour vérifier). En marchant : la flèche est relative à la direction de marche.
- [ ] `,` `/` dans la boussole passent d'un contact à l'autre.
- [ ] `R` deux fois sur l'écran GPS : redémarrage à froid (log `cold restart sent`), le fix revient.

## Phase 5 — carte hors ligne (`cardputer_adv_standalone_map`, en option)

- [ ] Sans carte SD : le log indique `SD: no card (map disabled, everything else works)` ; `K` affiche la
      grille avec « Pas de carte sur la SD » ; messagerie et GPS fonctionnent normalement.
- [ ] Préparer la carte SD avec `tools/tiles` (voir `tools/tiles/README.md`) puis redémarrer :
      log `SD: map tiles zoom 12-15 around ...`.
- [ ] `K` : la carte s'affiche centrée sur ta position (ou sur la zone si pas encore de fix GPS).
- [ ] `=` / `-` zooment, `;` `.` `,` `/` déplacent, `0` recentre sur toi. Chaque affichage en moins de 300 ms
      (impression visuelle ; la durée est mesurée par le firmware).
- [ ] Ta position (point bleu) et les contacts qui partagent la leur (triangles orange) sont au bon endroit.
- [ ] **Pendant l'affichage de la carte, un message envoyé par le second nœud arrive quand même**
      (la radio et la carte SD partagent le même bus).
- [ ] Le firmware sans carte (`cardputer_adv_standalone`) : `K` affiche « Carte non incluse ».

## Phase 6 — finitions

### Démo sans module LoRa (`cardputer_adv_demo`)

- [ ] Au démarrage : bandeau « Mode démo », log `DEMO mode: no radio, simulated contacts and GPS`.
- [ ] Bob a laissé un message de bienvenue ; lui répondre : « reçu » puis sa réponse quelques secondes après.
- [ ] Toutes les 1 à 2 minutes : un message sur Public, un message privé ou un voisin entendu (bandeau + son).
- [ ] `G` : « recherche » puis fix au bout de 20 s ; la position tourne autour de Colomiers.
- [ ] Contacts : distances affichées ; boussole vers Bob (Toulouse, ~9 km à l'est).
- [ ] Le son des notifications sort du haut-parleur ; Réglages > Son = non le coupe.

### Pour tous les firmwares autonomes

- [ ] Réglages > Luminosité : faible / moyenne / forte change immédiatement l'écran.
- [ ] Sans carte SD : « Sauvegarder sur SD » est grisé (« pas de SD »).
- [ ] Avec carte SD : Sauvegarder → bandeau « Sauvegarde faite » ; le fichier `cardputer_backup.bin` est sur la SD.
- [ ] Changer le nom, puis Restaurer (Entrée deux fois) → redémarrage, l'ancien nom et les contacts reviennent.
- [ ] Après un flash complet + restauration : **la même identité** (les autres nœuds te reconnaissent).
- [ ] Écran éteint : log `screen off, CPU 80 MHz`, puis une touche le rallume normalement.
- [ ] Autonomie : charger à 100 %, laisser tourner une journée d'usage normal, noter le % de la ligne
      `bat` du log toutes les heures (ou Réglages > Système).
- [ ] Batterie à 10 % : bandeau « Batterie faible » et son grave, une seule fois.

### Installation par M5Burner et M5Launcher

- [ ] M5Burner : un `.bin` de la release ajouté en firmware personnalisé (adresse 0x0) démarre normalement.
- [ ] M5Launcher : copier `cardputer_adv_demo-v0.6.1.bin` sur la SD, l'installer depuis le lanceur.
      Le firmware démarre ; log `storage ... KB -> up to N conversations kept` avec plusieurs Mo.
- [ ] Sous M5Launcher : envoyer quelques messages, éteindre/rallumer : l'historique est conservé.
- [ ] Revenir au lanceur puis relancer le firmware : réglages et messages toujours là.
