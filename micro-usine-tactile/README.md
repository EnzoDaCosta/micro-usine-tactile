# Micro-Usine Tactile : pilotage d'un doigt mécanique (X, Y, Z)

Prototype qui pilote à distance un « doigt mécanique » sur 3 axes depuis un tableau de bord Node-RED.
Un ESP32 reçoit les ordres par MQTT et commande 4 moteurs DC via 2 modules L298N.

Projet industriel École Hexagone, année 2025-2026 : « Micro-Usine Tactile : Smartphone sous contrôle ».
Périmètre de l'examen : hardware (moteurs, doigt mécanique), communication hardware/software, tableau de bord interactif.

## Architecture

```
Dashboard Node-RED  ->  Broker Mosquitto  ->  ESP32 (Wi-Fi)  ->  2 x L298N  ->  Moteurs X, Y, Z
   (supervision, IT)                          (terrain, OT)
```

## Matériel

- ESP32 (ESP-WROOM-32)
- 2 modules L298N
- 4 moteurs DC type 130 (1,5 à 3 V)
- Pile 9 V, breadboards, fils

## Câblage

| Axe | Moteurs | Module | Broches ESP32 -> entrées du module |
|---|---|---|---|
| X | X1 | n°1, canal A (`OUT1`/`OUT2`) | D27 -> `IN1`, D26 -> `IN2` |
| X | X2 | n°1, canal B (`OUT3`/`OUT4`) | D25 -> `IN3`, D33 -> `IN4` |
| Y | Y | n°2, canal A (`OUT1`/`OUT2`) | D14 -> `IN1`, D13 -> `IN2` |
| Z | Z (doigt) | n°2, canal B (`OUT3`/`OUT4`) | D18 -> `IN3`, D19 -> `IN4` |

Alimentation :

- Pile 9 V : fil + sur la borne `+12V` des deux modules, fil - sur `GND`.
- Le `GND` de l'ESP32 est relié au même `GND` (masse commune).
- Laisser en place les cavaliers `5V-EN`, `ENA` et `ENB` des modules.
- Ne pas relier la borne `5V` des modules à l'ESP32.

## Fonctionnement

- Puissance **fixe à 60 %** (PWM 5 kHz). La **durée du mouvement** est la variable : curseur de 100 à 2000 ms
  (l'ESP32 la borne entre 50 et 3000 ms, 500 ms par défaut).
- Chaque mouvement s'arrête seul à la fin de sa durée.
- Si le Wi-Fi tombe, l'ESP32 coupe tous les moteurs avant d'attendre la reconnexion.
- L'axe X fait tourner X1 et X2 avec le même ordre.

### Topics MQTT

| Topic | Sens | Contenu |
|---|---|---|
| `iot/moteurs/commande` | Dashboard -> ESP32 | `X_AVANT`, `X_ARRIERE`, `Y_GAUCHE`, `Y_DROITE`, `Z_HAUT`, `Z_BAS`, `STOP` |
| `iot/moteurs/duree` | Dashboard -> ESP32 | durée du mouvement en millisecondes |
| `iot/moteurs/status` | ESP32 -> Dashboard | JSON : axe, sens, durée, état (`MOUVEMENT` ou `ARRET`) |

## Installation

### 1. Mosquitto (broker MQTT)

Ajouter à la fin de `mosquitto.conf` puis redémarrer le service :

```
listener 1883
allow_anonymous true
```

Autoriser le port TCP 1883 dans le pare-feu. Attention : cette configuration est celle du prototype, elle n'est pas sécurisée (voir plus bas).

### 2. Node-RED

1. Installer Node.js (version 18 ou plus), puis `npm install -g --unsafe-perm node-red`.
2. Lancer `node-red`, ouvrir `http://localhost:1880`.
3. Menu -> Manage palette -> installer `node-red-dashboard`.
4. Menu -> Import -> choisir `nodered/flow_moteurs.json`, puis **Deploy**.
5. Le dashboard est sur `http://localhost:1880/ui`.

### 3. ESP32

1. Arduino IDE : installer la bibliothèque **PubSubClient** (Nick O'Leary) et le paquet de cartes ESP32.
2. Dans `esp32_moteurs_mqtt/`, copier `config.example.h` en `config.h` et renseigner le Wi-Fi (2,4 GHz) et l'IPv4 de l'ordinateur.
3. Carte : **ESP32 Dev Module**. Téléverser, ouvrir le Moniteur Série à 115200 bauds.
4. On doit voir `Wi-Fi OK` puis `Connexion MQTT... OK`.

## Utilisation

Sur le dashboard : 6 boutons de direction (X, Y, Z), bouton STOP, curseur de durée, flèches de direction
en temps réel, dernier mouvement et état.

## Limites actuelles et évolutions

Le prototype n'est **pas sécurisé** : broker anonyme, MQTT en clair, dashboard sans authentification, pas de journalisation,
un seul réseau (pas de séparation IT/OT). Évolutions prévues :

- Appui sur l'écran (bonus), support smartphone imprimé en 3D, scroll répétable
- Séparation IT/OT, comptes MQTT, TLS (port 8883), authentification du dashboard
- API REST authentifiée, logs horodatés persistants
- Arrêt d'urgence physique, analyse de risques, plan de résilience

## Structure du dépôt

```
esp32_moteurs_mqtt/   code de l'ESP32 (sketch Arduino) + config.example.h
nodered/              flux Node-RED à importer (dashboard)
```
