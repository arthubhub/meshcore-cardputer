# Cartes hors ligne pour le Cardputer

La carte est **optionnelle** : elle n'existe que dans le firmware `cardputer_adv_standalone_map`.
Sans carte SD ou sans tuiles, ce firmware fonctionne normalement et l'écran Carte affiche un message.

## Préparer la carte SD (sur le PC)

1. Installer Python 3 et Pillow : `pip install pillow`
2. Choisir une source de tuiles **qui autorise l'usage hors ligne**. Les serveurs publics
   `tile.openstreetmap.org` l'interdisent pour du téléchargement en masse
   (https://operations.osmfoundation.org/policies/tiles/). Deux possibilités :
   - un fichier `.mbtiles` raster (exporté depuis QGIS, MOBAC…) → option `--mbtiles` ;
   - un fournisseur de tuiles dont les conditions le permettent → option `--url`.
3. Lancer, par exemple pour 10 km autour de Colomiers, zooms 12 à 15 :

```bash
python3 make_tiles.py --center 43.6116 1.3346 --radius-km 10 --zooms 12-15 --mbtiles ma_zone.mbtiles --out carte
```

4. Copier le dossier `carte/tiles` **à la racine** de la carte microSD (formatée en FAT32).

Ordres de grandeur : chaque tuile fait 128 Ko (pixels bruts, lisibles sans décodage par le Cardputer).
10 km de rayon en zooms 12 à 15 ≈ 1 000 tuiles ≈ 130 Mo.

Le mode `--synthetic` génère une fausse carte de test, sans téléchargement.
