# Installer le firmware sur le Cardputer ADV

> **Langue de l'interface** : le firmware démarre en anglais. Pour passer en français : touche `s`
> (Settings), première ligne « Language / Langue », touche `/`. Les noms d'écrans cités ici sont ceux de
> l'interface en français (*Réglages* = *Settings*, *Mode téléphone* = *Phone mode*).

## 1. Choisir le bon fichier

| Fichier | Pour qui | Module LoRa |
| --- | --- | --- |
| `cardputer_adv_demo` | **Essayer l'interface** : contacts et GPS simulés | pas besoin |
| `cardputer_adv_standalone` | Messagerie autonome, sans téléphone | Cap LoRa-1262 |
| `cardputer_adv_standalone_map` | Pareil + carte hors ligne sur la carte SD | Cap LoRa-1262 |
| `cardputer_adv_companion_ble` | Avec l'appli MeshCore du téléphone (Bluetooth) | Cap LoRa-1262 |
| `cardputer_adv_dual` | **Les deux** : autonome au clavier + mode téléphone | Cap LoRa-1262 |

`cardputer_adv_dual` contient les deux firmwares. Il démarre en mode clavier (avec la carte).
*Réglages → Mode téléphone* (Entrée deux fois) redémarre en mode téléphone : l'appli MeshCore
trouve alors le Cardputer en Bluetooth. Pour revenir au clavier : **appuyer 3 fois sur la touche
`esc`** (en haut à gauche, celle du `` ` ``). Les deux modes gardent la même identité radio, mais
leurs contacts et messages sont séparés. Ce fichier ne s'installe pas avec M5Launcher.

**Avec le Cap LoRa : visser l'antenne avant d'allumer.** Émettre sans antenne abîme la radio.

## 2. Télécharger

- **Versions publiées** : page *Releases* du dépôt GitHub (`arthubhub/meshcore-cardputer`). Un seul
  fichier `.bin` par firmware, terminé par le numéro de version (`cardputer_adv_dual-v0.8.0.bin`), à flasher
  tel quel. Une fois flashé, la version installée s'affiche dans *Settings › System › Version*
  (*Réglages › Système* en français) et dans le journal série.
- **Dernière compilation** (il faut être connecté à GitHub) : onglet *Actions* → dernière exécution
  « Cardputer ADV » verte → *Artifacts* → `firmware-<nom>` → prendre le fichier `...-merged.bin`.

## 3. Flasher (le plus simple : dans le navigateur)

1. Ouvrir https://thelastoutpostworkshop.github.io/ESPConnect/ avec **Chrome ou Edge** sur un ordinateur.
2. Brancher le Cardputer en USB-C. S'il n'est pas reconnu : l'éteindre, maintenir le bouton **G0**
   (sur le côté) en branchant le câble, puis relâcher.
3. *Connect* → choisir le port série du Cardputer.
4. Choisir le fichier `.bin`, adresse **0x0**, lancer l'écriture.
5. Débrancher / rebrancher (ou appuyer sur reset).

En ligne de commande, c'est équivalent :

```bash
esptool.py --chip esp32s3 write_flash 0x0 cardputer_adv_demo-v0.6.1.bin
```

## 3 bis. Autres façons d'installer

### Avec M5Burner (l'outil officiel M5Stack)

Les fichiers `.bin` de la release sont des images complètes, comme celles que M5Burner flashe.

1. Ouvrir M5Burner, choisir l'appareil **Cardputer**.
2. Section des firmwares personnalisés (*Custom* / *User Custom*, compte M5Stack nécessaire) :
   ajouter le fichier `.bin`, adresse **0x0**.
3. Le garder privé ou le partager avec un code de partage (pratique pour donner la démo à quelqu'un).

### Avec M5Launcher (lanceur d'applis sur carte SD)

Pour ceux qui ont déjà M5Launcher sur leur Cardputer : on garde le lanceur et ses autres applis.

1. Copier le fichier `.bin` de la release (le même que pour ESPConnect) sur la carte SD.
2. Dans M5Launcher : *SD* → choisir le fichier → installer.
3. Le lanceur crée tout seul la zone de stockage du firmware (messages, contacts, réglages) avec
   **toute la place libre** de la mémoire. Le journal série affiche au démarrage
   `storage ... KB -> up to N conversations kept` : avec très peu de place, le Cardputer garde
   moins de conversations, mais tout fonctionne.
4. Avant de réinstaller ou de changer d'appli : *Réglages → Sauvegarder sur SD*, car M5Launcher peut
   effacer la zone de stockage.

Vérifier un fichier avant de le copier (facultatif, sur PC) :
`python3 tools/launcher_check.py cardputer_adv_demo-v0.6.1.bin` doit afficher « Launcher compatibility: OK ».

> Le firmware companion (`cardputer_adv_companion_ble`) garde le découpage officiel de MeshCore :
> sous M5Launcher il n'aurait que 448 Ko de stockage (assez pour lui, il garde peu de données).

## 4. Voir les messages de diagnostic (pour les tests)

Moniteur série à **115200 bauds** (ESPConnect a un onglet *Serial*, ou l'outil de l'IDE Arduino,
ou `pio device monitor -b 115200`).
Les lignes qui commencent par `[CPADV]` décrivent ce que fait le firmware : les copier en entier
pour signaler un problème. Format : `[CPADV] 12.345 I GPS    message` (secondes depuis
l'allumage, niveau I/W/E = info/attention/erreur, partie concernée).

- Pas besoin d'ouvrir le moniteur avant d'allumer : le début du log est gardé en mémoire et
  réaffiché dès que le moniteur se connecte (`---- boot log replay ----`).
- Toutes les 10 s, une ligne `BEAT` résume l'état (mémoire, batterie, clavier, radio, GPS).
- Firmware autonome : taper une commande dans le moniteur puis Entrée. `help` liste tout.
  `status`, `i2c` (puces vues sur le bus interne), `keys on` (chaque touche brute),
  `gps raw on` (trames GPS brutes), `gps cold`, `boot companion`, `reboot`.

## 5. Garder sa configuration

Avant de changer de firmware : *Réglages → Sauvegarder sur SD*. Après le flash :
*Réglages → Restaurer depuis SD* (deux fois Entrée). Le fichier `cardputer_backup.bin` contient ta
**clé privée MeshCore** : ne prête pas la carte SD.

## Touches utiles

| Touche | Action |
| --- | --- |
| `M` `C` `G` `K` `S` | Messages, Contacts, GPS, Carte, Réglages (depuis l'accueil) |
| `;` `.` `,` `/` | haut, bas, gauche, droite |
| `` ` `` | retour |
| Entrée | ouvrir / envoyer |
| Fn + `;` `.` | défiler dans une discussion |
| Fn + `` ` `` | retour depuis un écran de saisie |
| Alt + `e` (`a`, `i`, `o`, `u`, `c`) | lettres accentuées : é, puis è ê ë en rappuyant |
