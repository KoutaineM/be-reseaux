# MIC-TCP — Bureau d'Étude Réseaux

Auteurs : Melwan KOUTAINE, Aouab ADMOU

## 1. Commandes de Compilation

Placez-vous à la racine du dépôt, puis lancez :

```bash
make
````

Les exécutables seront générés dans le dossier `build/`.

---

## 2. Lancement des Applications de Test

### Mode Texte

* **Puits :**

```bash
./tsock_texte -p <port>
```

* **Source :**

```bash
./tsock_texte -s <adresse_dest> <port>
```

### Mode Vidéo

* **Puits :**

```bash
./tsock_video -p -t mictcp
```

* **Source :**

```bash
./tsock_video -s -t mictcp
```

---

## 3. Fonctionnalités Implémentées

### ✅ Fonctionnalités Fonctionnelles

* **Connexion (3-way handshake)** : Établissement fiable de la connexion client/serveur.
* **Transmission de données** : Envoi et réception de messages texte et vidéo.
* **Fermeture de connexion** : Gestion correcte des étapes FIN, FIN+ACK, ACK final.
* **Fiabilité partielle** :

  * Mesure du taux de perte à la connexion.
  * Fenêtre glissante paramétrable.
  * Acceptation conditionnelle de pertes selon la politique définie.
* **Synchronisation application/transport (réception)** :

  * Mutex et variables de condition assurent la cohérence entre réception réseau et application.

### ⚠️ Limites et Points Non Fonctionnels

* La gestion de plusieurs connexions simultanées n’a pas été testée.
* L’allocation mémoire pour certaines structures (ex. adresses IP) peut être optimisée.
* Les tests n’ont pas couvert tous les cas de pertes extrêmes (> 20%).

---

## 4. Choix d’Implémentation Remarquables

### Fiabilité Partielle

* **Mesure initiale** :

  * À l’établissement de la connexion, le client envoie 100 paquets pour estimer le taux de perte.
* **Fenêtre glissante** :

  * Taille : 10.
  * Seuils de pertes acceptées :

    * `< 2%` : 0 perte
    * `2–5%` : 1 perte
    * `5–12%` : 2 pertes
    * `12–20%` : 3 pertes
    * `> 20%` : connexion refusée
* **Décision de retransmission** :

  * Si un ACK n’est pas reçu :

    * Si le nombre de pertes dans la fenêtre reste acceptable → pas de retransmission.
    * Sinon → retransmission jusqu’à obtention d’un ACK ou dépassement du seuil.

### Synchronisation Application/Transport (Réception)

* **Buffer applicatif** :

  * File protégée par mutex.
  * Si vide, le thread application attend via une variable de condition.
* **Traitement asynchrone** :

  * Un thread dédié traite les PDUs reçus et les place dans le buffer.
  * Évite de bloquer la réception réseau si l’application est lente.

---

## 5. Historique des Versions

* **v1** : Connexion et transmission de base (sans fiabilité partielle).
* **v2** : Ajout de la mesure de fiabilité et de la fenêtre glissante.
* **v3** : Fermeture complète de la connexion + synchronisation application/transport.
