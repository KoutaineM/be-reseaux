# MIC-TCP — Bureau d'Étude Réseaux

Auteurs : Melwan KOUTAINE, Aouab ADMOU

## 1. Commandes de compilation

Placez-vous à la racine du dépôt, puis lancez :

```bash
make
```

Les exécutables seront générés dans le dossier `build/`.

---

## 2. Lancement des applications de test

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

## 3. Fonctionnalités implémentées

### Aspects fonctionnels

* **Connexion (3-way handshake)** : Établissement fiable de la connexion client/serveur via un échange SYN, SYN+ACK, ACK.
* **Transmission de données** : Envoi et réception fiables de messages texte et vidéo, avec gestion des numéros de séquence et acquittements (ACKs).
* **Fermeture de connexion** : Gestion robuste de la fermeture avec envoi répété de FIN jusqu'à réception de FIN+ACK, suivi d'un ACK final.
* **Fiabilité partielle** :
  * Mesure du taux de perte lors de l'établissement de la connexion.
  * Fenêtre glissante configurable pour tolérer des pertes selon une politique définie.
  * Acceptation conditionnelle des pertes basée sur le taux mesuré.
* **Synchronisation application/transport (réception)** :
  * Utilisation de mutex et variables de condition pour assurer la cohérence entre la réception réseau et le traitement applicatif.
  * Buffer applicatif thread-safe pour stocker les données reçues.
* **Asynchronisme client** :
  * Thread réseau dédié côté client pour traiter les ACKs et FINs, réduisant la latence et améliorant la réactivité.

---

## 4. Choix d’implémentation remarquables

### Fonctionnement du protocole

Le protocole MIC-TCP est une implémentation simplifiée d’un protocole de transport fiable, inspiré de TCP. Il fonctionne comme suit :

1. **Établissement de la connexion** :
   - Utilise un *3-way handshake* : le client envoie un SYN, le serveur répond par un SYN+ACK, et le client confirme avec un ACK.
   - Après le handshake, le client mesure la fiabilité du canal en envoyant 100 paquets de test et en calculant le taux de perte.

2. **Transmission de données** :
   - Les données sont envoyées avec des numéros de séquence pour garantir l’ordre.
   - Chaque paquet envoyé attend un ACK correspondant. Si l’ACK n’est pas reçu dans le délai configuré (`TIMEOUT`), le protocole décide de retransmettre ou d’accepter la perte selon la fenêtre glissante.
   - Côté client, un thread réseau asynchrone traite les ACKs, synchronisé avec l’envoi via des variables de condition.

3. **Fermeture de la connexion** :
   - Initie une fermeture avec un FIN, qui est envoyé jusqu’à réception d’un FIN+ACK.
   - Envoie un ACK final pour confirmer la fermeture.
   - Les FINs reçus côté client sont traités par le thread réseau, qui signale la progression via une variable de condition.

### Changements dans mictcp_core.c

Le fichier `mictcp_core.c`, initialement fourni par le professeur, a été modifié pour retourner un descripteur de socket système (`sys_socket`), pour ne pas s’appuyer sur une variable globale `global_socket`. Nous avons implémenté un système de table de correspondance (lookup table) dans `mictcp_sock_lookup.c` et `mictcp_sock_lookup.h`, permettant de :

- Associer chaque socket MIC-TCP (`mic_tcp_sock`) à un descripteur utilisateur (`fd`) et un descripteur système (`sys_socket`).
- Récupérer un socket via `get_socket_by_fd` ou `get_socket_by_sys_fd`, éliminant la dépendance à une variable globale.
- Gérer plusieurs sockets potentiellement simultanés.

Cette approche améliore la modularité et la robustesse du protocole.

### Asynchronisme côté client

Pour optimiser la gestion des ACKs et FINs, nous avons ajouté un thread réseau asynchrone côté client, lancé dans `mic_tcp_connect` après l’établissement de la connexion. Ce thread :

- Exécute `listening_client`, qui utilise `IP_recv` pour recevoir les PDUs et les passe à `process_client_PDU`.
- Traite les ACKs pour les données envoyées par `mic_tcp_send`, signalant une variable de condition (`ack_cond`) pour synchroniser avec le thread principal.
- Gère les FIN+ACKs lors de la fermeture pour coordonner avec `mic_tcp_close`.
- Utilise des mutex pour garantir la cohérence des états et des numéros de séquence.

Cette asynchronie réduit la latence et permet au thread principal de se concentrer sur l’envoi ou la fermeture, tandis que le réseau est géré en parallèle.

### Logging amélioré

Le logging a été enrichi pour afficher clairement les événements du protocole, avec des préfixes indiquant le contexte du thread :

- **MAIN** : Événements survenant dans le thread principal (par exemple, appels à `mic_tcp_send`, `mic_tcp_close`).
- **NET** : Événements survenant dans le thread réseau (par exemple, réception d’ACKs ou FINs dans `process_client_PDU`).

Les messages incluent des codes de couleur ANSI pour faciliter la lecture et des détails comme les numéros de séquence, les états du socket, et les résultats des tentatives de transmission.

### Organisation des fichiers

Le projet est structuré en plusieurs parties pour une meilleure maintenabilité :

- **include/mictcp/** : En-têtes du protocole (`mictcp.h`, `mictcp_config.h`, `mictcp_pdu.h`, `mictcp_sliding_window.h`, `mictcp_sock_lookup.h`).
- **src/** : Implémentations du protocole (`mictcp_socket.c`, `mictcp_core.c`, `mictcp_sliding_window.c`, `mictcp_sock_lookup.c`).
- **api/** : Interface bas-niveau (`mictcp_core.h`).
- **build/** : Dossier pour les exécutables générés.

Les paramètres configurables, comme le taux de perte (`LOSS_RATE`) et le délai d’attente (`TIMEOUT`), sont définis dans `include/mictcp/mictcp_config.h`.

### Système de négociation

La négociation de la connexion est une étape clé pour assurer la fiabilité partielle :

1. **Mesure du taux de perte** :
   - Après le *3-way handshake*, le client envoie `MESURING_RELIABILITY_PACKET_NUMBER` (100) paquets de test au serveur.
   - Le serveur répond par des ACKs pour chaque paquet reçu.
   - Le client calcule le taux de perte comme : `(100 - (nombre_ACKs_reçus / 100)) * 100`.

2. **Configuration de la fenêtre glissante** :
   - La taille de la fenêtre est fixée à 10 (`sliding_window_size`).
   - Selon le taux de perte mesuré, le nombre de pertes consécutives acceptées (`sliding_window_consecutive_loss`) est défini :

     | Taux de Perte Mesuré | Pertes Acceptées dans Fenêtre | Commentaire                |
     |----------------------|-------------------------------|----------------------------|
     | x < 2%               | 0 sur 10                      | Fiabilité maximale         |
     | 2% ≤ x < 5%          | 1 sur 10                      | Pertes faibles             |
     | 5% ≤ x < 12%         | 2 sur 10                      | Pertes modérées            |
     | 12% ≤ x ≤ 20%        | 3 sur 10                      | Pertes élevées             |
     | x > 20%              | refus                         | Canal trop peu fiable      |

   - Si le taux de perte dépasse 20 %, la connexion est fermée immédiatement.

3. **Décision dynamique** :
   - Pendant la transmission, chaque perte de paquet est enregistrée dans la fenêtre glissante (`sliding_window`).
   - La fonction `verify_acceptable_loss` vérifie si le nombre de pertes reste dans les limites définies.
   - Si les pertes sont acceptables, aucune retransmission n’est effectuée, optimisant la performance sur des canaux bruités.

### Fiabilité partielle

* **Mesure initiale** :
  * 100 paquets envoyés pour estimer le taux de perte.
* **Fenêtre glissante** :
  * Taille : 10.
  * Seuils de pertes définis ci-dessus.
* **Décision de retransmission** :
  * Si un ACK est manquant, la fonction `verify_acceptable_loss` décide si la perte est tolérable.
  * Si tolérable, la fenêtre est mise à jour (`update_sliding_window(sock, 0)`), et la transmission continue.
  * Sinon, le paquet est retransmis jusqu’à réception de l’ACK ou dépassement du seuil.

### Synchronisation application/transport (Réception)

* **Buffer applicatif** :
  * File protégée par un mutex pour stocker les PDUs reçus.
  * Si vide, l’application attend via une variable de condition (`connection_cond`).
* **Traitement asynchrone** :
  * Le thread réseau (`listening_client`) place les données dans le buffer via `app_buffer_put`.
  * L’application récupère les données via `mic_tcp_recv`, synchronisé par `connection_cond`.

---

## 5. Historique des Versions

* **v1** : Communication normale.
* **v2** : Communication avec acquittement et établissement de connexion.
* **v3** : Communication avec acquittement, établissement de connexion et fiabilité partielle statique.
* **v4.1** : Communication avec acquittement, établissement de connexion et fiabilité négociée.
* **v4.2** : Ajout de l’asynchronisme, fermeture de connexion robuste et table de sockets.

---

Ce projet implémente un protocole MIC-TCP robuste et configurable, avec une gestion efficace des connexions, des transmissions, et des fermetures, tout en introduisant des mécanismes asynchrones pour optimiser les performances réseau.