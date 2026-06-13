# TeslaCAN

Système embarqué pour Tesla Model 3 : désactivation automatique du bip de limite de vitesse, compteur circulaire AMOLED, et boutons de raccourci.

## Matériel

| Rôle | Carte | Communication |
|---|---|---|
| Serveur CAN | ESP32 WROOM32 | CAN bus ↔ Tesla + ESP-NOW |
| Transceiver CAN | Waveshare SN65HVD230 | CAN physique |
| Affichage | ESP32-S3 Touch AMOLED 1.75" (Waveshare) | ESP-NOW |
| Boutons | ESP32-C3 | ESP-NOW |

## Architecture

```
Tesla CAN bus (500 kbps, OBD2)
       │
  SN65HVD230
       │
  ESP32 WROOM32 (server)
   ├─ ESP-NOW ──► ESP32-S3 AMOLED (display)
   └─ ESP-NOW ──► ESP32-C3 (buttons)
```

## Fonctionnalités

- **Auto-mute bip** : dès que la vitesse dépasse 2 km/h, le bip de limite de vitesse est coupé automatiquement
- **Compteur circulaire** : gauge animée 0-240 km/h, zones couleur (vert/orange/rouge), affichage de la limite
- **Boutons raccourcis** (appui court / appui long) :

| Bouton | Appui court | Appui long |
|---|---|---|
| 0 | Mute bip | Feux de détresse |
| 1 | Mode Sentinelle | Lumières Sentry |
| 2 | Ouvrir coffre | Fermer coffre |
| 3 | Frunk | Verrouiller |
| 4 | Climatisation ON | Climatisation OFF |
| 5 | Volume + | Essuie-glace + |

## Mise en route

### 1. Configurer les adresses MAC

Flasher chaque board et lire la ligne `My MAC:` dans le monitor série.
Ensuite remplir dans les fichiers `config.h` :

- `server/include/config.h` → `DISPLAY_MAC` et `BUTTONS_MAC`
- `display/include/config.h` → `SERVER_MAC`
- `buttons/include/config.h` → `SERVER_MAC`

### 2. Trouver les trames CAN de bip

Les IDs CAN sont des **placeholders** issus de la recherche communautaire (opendbc).
Pour trouver la trame exacte qui contrôle le bip sur ton firmware Tesla :

```bash
# Avec un adaptateur socketcan ou savvycan
candump can0 | grep "0x2B9\|0x3D8\|0x45"
```

Déclenche le bip de limite → identifie la trame → mets à jour `CAN_BEEP_MUTE_FRAME` dans `server/include/can_ids.h`.

### 3. Flasher

```bash
cd server  && pio run -t upload
cd display && pio run -t upload
cd buttons && pio run -t upload
```

### 4. Connexion CAN

| SN65HVD230 | ESP32 |
|---|---|
| TX | GPIO 21 |
| RX | GPIO 22 |
| VCC | 3.3V |
| GND | GND |

Brancher CANH/CANL sur les broches 6 et 14 de la prise OBD2 du véhicule.

## IDs CAN Tesla Model 3 (communauté)

| Signal | ID | Notes |
|---|---|---|
| Vitesse | `0x257` | bits[0:12], ×0.036 = km/h |
| Rapport (P/R/N/D) | `0x118` | bits[3:6] |
| Limite de vitesse | `0x3D8` | byte[3], à confirmer |
| Batterie SOC | `0x132` | bits[0:9] ×0.1% |
| Température ext. | `0x241` | byte[0] − 40 = °C |

Source : [opendbc](https://github.com/commaai/opendbc) — vérifier avec ton firmware.
