/*
 * ESP32 - PILOTAGE DES 4 MOTEURS PAR MQTT (dashboard Node-RED)
 * -------------------------------------------------------------
 * Matériel : 2 modules L298N, 4 moteurs (X1+X2 ensemble, Y, Z), pile 9V.
 *
 * Bibliothèque à installer : "PubSubClient" (Nick O'Leary)
 *   Arduino IDE -> Outils -> Gérer les bibliothèques -> chercher PubSubClient
 *
 * TOPICS MQTT :
 *   iot/moteurs/commande  (reçu)   X_AVANT, X_ARRIERE, Y_GAUCHE, Y_DROITE,
 *                                  Z_HAUT, Z_BAS, STOP
 *   iot/moteurs/duree     (reçu)   durée du mouvement en millisecondes (50 à 3000)
 *   iot/moteurs/status    (envoyé) JSON pour l'affichage du dashboard
 *
 * La puissance est FIXE (VITESSE_POURCENT). La variable, c'est la DURÉE : chaque
 * commande fait tourner le moteur pendant dureeMs puis l'arrête toute seule
 * (sécurité : même si le Wi-Fi est coupé, rien ne reste allumé).
 */

#include <WiFi.h>
#include <PubSubClient.h>

// =====================================================================
// 1. A MODIFIER : TES RÉGLAGES RÉSEAU
// =====================================================================

// Les identifiants Wi-Fi et l'IP du broker sont dans config.h (jamais versionné).
// Copie config.example.h en config.h puis remplis tes valeurs.
#include "config.h"

// =====================================================================
// 2. TOPICS MQTT
// =====================================================================

const char* TOPIC_CMD    = "iot/moteurs/commande";
const char* TOPIC_DUREE  = "iot/moteurs/duree";
const char* TOPIC_STATUS = "iot/moteurs/status";

// =====================================================================
// 3. BROCHES (identiques au câblage des 2 modules L298N)
// =====================================================================

// Module 1 : axe X (deux moteurs, un par canal)
const int X1_IN1 = 27;  // D27 -> module 1, IN1
const int X1_IN2 = 26;  // D26 -> module 1, IN2
const int X2_IN1 = 25;  // D25 -> module 1, IN3
const int X2_IN2 = 33;  // D33 -> module 1, IN4

// Module 2 : axes Y et Z
const int Y_IN1 = 14;   // D14 -> module 2, IN1
const int Y_IN2 = 13;   // D13 -> module 2, IN2
const int Z_IN1 = 18;   // D18 -> module 2, IN3
const int Z_IN2 = 19;   // D19 -> module 2, IN4

const int NB_BROCHES = 8;
const int BROCHES[NB_BROCHES] = {X1_IN1, X1_IN2, X2_IN1, X2_IN2,
                                 Y_IN1,  Y_IN2,  Z_IN1,  Z_IN2};

// Positions dans le tableau BROCHES (un moteur = 2 positions)
const int X1 = 0;
const int X2 = 2;
const int Y  = 4;
const int Z  = 6;

// =====================================================================
// 4. RÉGLAGES MOTEURS
// =====================================================================

const int PWM_FREQ_HZ     = 5000;
const int PWM_RESOLUTION  = 8;       // valeurs de 0 à 255

const int VITESSE_POURCENT = 60;     // puissance FIXE

int dureeMs               = 500;     // durée d'un mouvement (réglable depuis le dashboard)
const int DUREE_MIN_MS    = 50;      // bornes de sécurité
const int DUREE_MAX_MS    = 3000;

// =====================================================================
// 5. VARIABLES
// =====================================================================

WiFiClient espClient;
PubSubClient client(espClient);

bool enMouvement = false;            // un mouvement est-il en cours ?
unsigned long finMouvement = 0;      // à quel moment l'arrêter
unsigned long dernierEssaiMqtt = 0;  // pour reconnecter sans bloquer

// =====================================================================
// 6. PWM (compatible paquet ESP32 version 2.x et 3.x)
// =====================================================================

void pwmInit() {
  for (int i = 0; i < NB_BROCHES; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(BROCHES[i], PWM_FREQ_HZ, PWM_RESOLUTION);
#else
    ledcSetup(i, PWM_FREQ_HZ, PWM_RESOLUTION);
    ledcAttachPin(BROCHES[i], i);
#endif
  }
}

void pwmEcrire(int position, int valeur) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BROCHES[position], valeur);
#else
  ledcWrite(position, valeur);
#endif
}

int dutyActuel() {
  return (255 * VITESSE_POURCENT) / 100;
}

// =====================================================================
// 7. FONCTIONS MOTEUR (un moteur = positions "a" et "a+1")
// =====================================================================

void moteurSens(int a, bool sensA) {
  if (sensA) {
    pwmEcrire(a + 1, 0);
    pwmEcrire(a, dutyActuel());
  } else {
    pwmEcrire(a, 0);
    pwmEcrire(a + 1, dutyActuel());
  }
}

void moteurStop(int a) {
  pwmEcrire(a, 0);
  pwmEcrire(a + 1, 0);
}

void toutStop() {
  moteurStop(X1);
  moteurStop(X2);
  moteurStop(Y);
  moteurStop(Z);
}

// =====================================================================
// 8. STATUT ENVOYÉ AU DASHBOARD
// =====================================================================

void publierStatut(char axe, const char* sens, bool mouvement) {
  char buf[120];
  if (mouvement) {
    snprintf(buf, sizeof(buf),
             "{\"axe\":\"%c\",\"sens\":\"%s\",\"duree\":%d,\"etat\":\"MOUVEMENT\"}",
             axe, sens, dureeMs);
  } else {
    snprintf(buf, sizeof(buf), "{\"etat\":\"ARRET\"}");
  }
  client.publish(TOPIC_STATUS, buf);
}

// =====================================================================
// 9. MOUVEMENTS
// =====================================================================

// Lance un mouvement sur un axe (X, Y ou Z) dans un sens
void lancerMouvement(char axe, bool sensA, const char* nomSens) {
  toutStop();                          // on coupe tout mouvement précédent

  if (axe == 'X') {                    // axe X : X1 et X2 ENSEMBLE
    moteurSens(X1, sensA);
    moteurSens(X2, sensA);
  } else if (axe == 'Y') {
    moteurSens(Y, sensA);
  } else if (axe == 'Z') {
    moteurSens(Z, sensA);
  }

  enMouvement = true;
  finMouvement = millis() + dureeMs;
  publierStatut(axe, nomSens, true);

  Serial.print(">> Axe ");
  Serial.print(axe);
  Serial.print(" : ");
  Serial.print(nomSens);
  Serial.print(" pendant ");
  Serial.print(dureeMs);
  Serial.println(" ms");
}

void arretTotal() {
  toutStop();
  enMouvement = false;
  publierStatut(' ', "", false);
  Serial.println(">> STOP");
}

// =====================================================================
// 10. RÉCEPTION DES MESSAGES MQTT
// =====================================================================

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  // --- Changement de durée du mouvement ---
  if (String(topic) == TOPIC_DUREE) {
    int v = message.toInt();
    dureeMs = constrain(v, DUREE_MIN_MS, DUREE_MAX_MS);
    Serial.print("Duree reglee a ");
    Serial.print(dureeMs);
    Serial.println(" ms");
    return;
  }

  // --- Commandes de mouvement ---
  if (String(topic) == TOPIC_CMD) {
    Serial.print("Commande recue : ");
    Serial.println(message);

    if      (message == "X_AVANT")   lancerMouvement('X', true,  "AVANT");
    else if (message == "X_ARRIERE") lancerMouvement('X', false, "ARRIERE");
    else if (message == "Y_GAUCHE")  lancerMouvement('Y', true,  "GAUCHE");
    else if (message == "Y_DROITE")  lancerMouvement('Y', false, "DROITE");
    else if (message == "Z_HAUT")    lancerMouvement('Z', true,  "HAUT");
    else if (message == "Z_BAS")     lancerMouvement('Z', false, "BAS");
    else if (message == "STOP")      arretTotal();
    else Serial.println("Commande inconnue, ignoree");
  }
}

// =====================================================================
// 11. CONNEXIONS
// =====================================================================

void connecterWifi() {
  Serial.print("Connexion au Wi-Fi ");
  Serial.print(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi OK, IP de l'ESP32 : ");
  Serial.println(WiFi.localIP());
}

// Tente une reconnexion MQTT toutes les 5 secondes, sans bloquer le programme
void reconnecterMqtt() {
  if (client.connected()) return;
  if (millis() - dernierEssaiMqtt < 5000) return;
  dernierEssaiMqtt = millis();

  Serial.print("Connexion MQTT... ");
  if (client.connect("esp32-moteurs")) {
    Serial.println("OK");
    client.subscribe(TOPIC_CMD);
    client.subscribe(TOPIC_DUREE);
  } else {
    Serial.print("echec (code ");
    Serial.print(client.state());
    Serial.println("), nouvel essai dans 5 s");
  }
}

// =====================================================================
// 12. SETUP
// =====================================================================

void setup() {
  Serial.begin(115200);
  pwmInit();
  toutStop();                          // rien ne bouge au démarrage

  connecterWifi();

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);
}

// =====================================================================
// 13. LOOP
// =====================================================================

void loop() {
  // Reconnexions éventuelles
  if (WiFi.status() != WL_CONNECTED) {
    toutStop();            // sécurité : on coupe tout AVANT d'attendre le Wi-Fi
    enMouvement = false;
    connecterWifi();
  }
  reconnecterMqtt();
  client.loop();

  // Fin automatique du mouvement
  if (enMouvement && millis() >= finMouvement) {
    arretTotal();
  }
}
