# Interface autonome Cardputer ADV

Interface utilisateur du firmware MeshCore autonome pour le M5Stack Cardputer ADV + Cap LoRa.

- Installer : [FLASH.md](FLASH.md) (choix du fichier, flash dans le navigateur, touches)
- Tester sur le matériel : [TESTS.md](TESTS.md)
- Cartes hors ligne : [../../tools/tiles/README.md](../../tools/tiles/README.md)

```
core/            UI portable (identique sur le Cardputer et dans le simulateur)
  Services.h     seule frontière avec le reste du firmware (mesh, système, GPS...)
  Keys.h         événements clavier logiques
  App.*          pile d'écrans, barre d'état, ligne d'aide, routage clavier
  Theme.h        dimensions, couleurs, polices (Latin-1, accents français)
  screens/       un fichier par écran
  MessageStore.* historique des messages (fichiers anneau, partagé appareil / simulateur)
  Nmea.*         lecture des trames GPS
  DemoMesh.*     faux réseau (simulateur + firmware démo), DemoGps.* faux GPS
device/          code propre au Cardputer : radio MeshCore, GPS, clavier, SD, sauvegarde
sim/             simulateur PC sans écran, piloté par scénarios, captures PNG
```

## Lancer le simulateur

Sous Linux (ou WSL sous Windows), avec `g++`, `make` et `git` :

```bash
make -C app/sim -j8 test run
```

Les captures sont écrites dans `app/sim/build/shots/` (et `shots_hw/` pour le passage avec le faux clavier).
À chaque push, la CI GitHub les publie aussi dans l'artefact `simulator-screenshots`.

## Écrire un scénario

Un fichier texte dans `sim/scenarios/`, une commande par ligne :

```
name Raphaël-CPADV
time 1793541600
rx Bob On se retrouve à 18h ?
key m
expect-screen Messages
shot messages
```

La liste complète des commandes est en tête de `sim/main_sim.cpp`.
