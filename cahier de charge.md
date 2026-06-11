Voici le document d'architecture et le cahier des charges complet pour la V1 du **SHM-Scanner**. Ce document fusionne tes exigences, les contraintes de la Norme 42, l'approche offensive (Red Team) et les critères d'optimisation issus de tes lectures.

---

# Cahier des Charges & Spécifications Techniques : SHM-Scanner V1

> **Statut du Projet :** Spécifications Validées
> **Rôle :** Scanner de mémoire furtif pour environnement Linux (x86_64)
> **Contrainte de Style :** Norme 42 (C99, pas de variables globales, fonctions $\le$ 25 lignes)
> **Objectif OPSEC :** Rester sous le radar des Blue Teams et des outils de supervision standard.

---

## 1. Objectifs du Projet

Le **SHM-Scanner** est un outil de reconnaissance offensive en ligne de commande. Il doit analyser l'espace d'adressage virtuel d'un processus cible à la recherche de signatures ou de patterns spécifiques (chaînes de caractères, shellcodes, configurations sensibles) sans altérer le processus cible et sans saturer les ressources de la machine hôte.

---

## 2. Spécifications Fonctionnelles

### A. Phase d'Investigation (Parsing Chirurgical)

* **Targeting :** Extraction des informations du processus cible via son Identifiant de Processus (PID).
* **Parsing de `/proc` :** Lecture et analyse brute du fichier virtuel `/proc/[PID]/maps`.
* **Extraction des Permissions :** Identification stricte des segments de mémoire selon leurs privilèges d'accès :
* Lecture (`r`)
* Écriture (`w`)
* Exécution (`x`)
* Privé (`p`) / Partagé (`s`)


* **Filtrage Intelligent :** Capacité de filtrer ou d'ignorer les bibliothèques dynamiques standard (`.so` inchangées) pour concentrer le scan sur le Tas (*Heap*), la Pile (*Stack*) et les segments anonymes modifiables.

### B. Moteur de Lecture (Extraction Bas Niveau)

* **Aspiration Mémoire :** Utilisation exclusive de l'appel système `process_vm_readv` pour copier directement les données de l'espace d'adressage du processus cible vers l'espace du scanner.
* **Privilèges Minimaux :** Exécution conditionnée à la possession de la capacité Linux `CAP_SYS_PTRACE`, évitant ainsi d'exécuter l'outil avec l'identifiant utilisateur `root` complet (Amélioration OPSEC).

### C. Moteur de Concurrence (Parallélisation)

* **Thread Pool natif :** Implémentation complète en C d'un pool de threads POSIX (`pthread`) géré par une file d'attente de tâches (*Task Queue*).
* **Synchronisation Robuste :** Utilisation exclusive de mutex et de variables de condition Butenhof pour éviter toute situation de compétition (*race condition*) ou de blocage (*deadlock*).
* **Distribution :** Chaque thread prend en charge le scan d'une plage d'adresses virtuelles (VMA) spécifique extraite de `/proc`.

---

## 3. Spécifications Techniques & Contraintes de Code

### A. Rigueur de la Norme 42

* **Architecture :** Fonctions limitées à 25 lignes maximum. Pas de déclarations multiples sur une même ligne.
* **Variables :** Interdiction stricte des variables globales. Toutes les variables doivent être déclarées en début de bloc.
* **Robustesse :** Vérification systématique de la valeur de retour de *chaque* appel système et fonction d'allocation (`malloc`, `pthread_mutex_init`, `process_vm_readv`, `open`).

### B. Gestion Mémoire Évoluée (Anti-Page Faults)

* **Buffer Réutilisable :** Interdiction d'allouer ou de libérer des tampons mémoire de manière dynamique pendant la phase de scan.
* **Buffer Pool :** Allocation d'une zone mémoire fixe (un tampon par thread worker) lors de l'initialisation du pool pour y copier les pages lues.

---

## 4. Métriques de Performance & Critères d'Acceptation ( Brendan Gregg Mode )

Pour être validée, la V1 du SHM-Scanner doit valider les seuils quantitatifs suivants lors des tests de profilage :

| Métrique | Seuil Critique | Outil de Validation | Impact OPSEC / Justification |
| --- | --- | --- | --- |
| **Fuites Mémoire** | **0 octet** (Strict) | `valgrind --leak-check=full` | Évite le crash de l'outil et l'alerte sur anomalie mémoire. |
| **Charge CPU Globale** | **$<$ 5%** en mode Stealth | `htop` / `top` | Évite les alertes de pics CPU chez la Blue Team. |
| **Charge CPU / Cœur** | **$<$ 15%** sur un seul cœur | `perf stat` | Lissage de la charge sur le scheduler Linux. |
| **Minor Page Faults** | Stables après init | `/usr/bin/time -v` | Preuve de la réutilisation efficace des buffers système. |
| **Vitesse (Fast)** | 1 Go / 5 secondes | `time` | Mode Audit interne rapide. |
| **Vitesse (Stealth)** | Paramétrable (30-60s/Go) | Options CLI (`--delay-ms`) | Injection de micro-pauses (`nanosleep`) pour lisser l'activité. |

---

## 5. Matrice des Références Littéraires du Projet

Le développement s'appuie sur l'extraction des concepts spécifiques des ouvrages suivants :

```
[ TLPI - Kerrisk ] ──────────> Parsing /proc (Ch. 12), Mappings (Ch. 49/50), Timers (Ch. 23)
[ Pthreads - Butenhof ] ─────> Implémentation du Thread Pool et gestion des Mutex
[ Pointers - Reese ] ────────> Arithmétique des pointeurs brute sur buffers de process_vm_readv
[ Perf - Gregg ] ────────────> Optimisation CPU (Ch. 6), Gestion Page Faults/RSS (Ch. 7)
[ Forensics - Volatility ] ──> Identification des structures d'injection de code en mémoire

```

---

## 6. Prochaines Étapes de Développement

1. **Étape 1 :** Écriture du parser de `/proc/[PID]/maps` en lecture brute par bloc (sans `fscanf`).
2. **Étape 2 :** Conception du Thread Pool POSIX (Structure de données, file d'attente et Workers).
3. **Étape 3 :** Intégration de `process_vm_readv` et du buffer réutilisable par Thread.
4. **Étape 4 :** Implémentation du module de *Throttling* (temporisation) et profilage avec `perf`/`valgrind`.