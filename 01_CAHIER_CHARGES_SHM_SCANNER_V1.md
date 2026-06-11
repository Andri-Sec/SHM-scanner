# SHM-Scanner V1 - Cahier des Charges Complet

## 1. Contexte Stratégique

SHM-Scanner est un **outil de post-exploitation** destiné à un opérateur Red Team. Son rôle : analyser la mémoire virtuelle d'un processus cible pour extraire des artefacts sensibles (payloads, clés cryptographiques, tokens, patterns de malware) sans déclencher les alertes des EDR/SIEM standards.

**Contrainte opérationnelle critique** : L'outil doit minimiser son empreinte forensique et comportementale sur la cible.

---

## 2. Périmètre de la Version 1 (Core Engine)

### 2.1 Fonctionnalités Obligatoires

| Fonctionnalité | Description | Priorité | Dépendance |
|---|---|---|---|
| Parser `/proc/[pid]/maps` | Extraire dynamiquement les segments mémoire d'une cible | **CRITIQUE** | — |
| Lecture de `/proc/[pid]/mem` | Accès direct à la mémoire virtuelle sans ptrace | **CRITIQUE** | Parser |
| Recherche par pattern (ASCII) | Chercher des chaînes texte exactes | **CRITIQUE** | Lecture |
| Recherche par pattern (Hex) | Chercher des suites d'octets bruts | **CRITIQUE** | Lecture |
| Multi-threading (Pool de Workers) | Paralléliser l'analyse sur N threads | **CRITIQUE** | Synchronisation |
| Synchronisation POSIX (Mutex/Cond) | Coordonner l'accès à la queue de tâches | **CRITIQUE** | Architecture |
| Rapportage | Journaliser les matchs avec adresse absolue | **IMPORTANT** | Recherche |
| Filtrage intelligent de segments | Ignorer les segments non pertinents (--x, ---) | **IMPORTANT** | Parser |

### 2.2 Fonctionnalités Additionnelles (Recommandées pour V1.5)

- **Obfuscation des patterns** : Chiffrement XOR/RC4 des patterns de recherche au niveau du binaire
- **Mode « Stealth Signature »** : Signature comportementale minimale (pas d'ouverture de /proc/self/*...)
- **Export structuré** : Sortie JSON des résultats pour intégration post-exploitation
- **Filtrage par permissions** : Option --perms rw-p | r-xp pour cibler certains segments
- **Limitation de throughput** : Throttling de la lecture pour éviter une surcharge CPU visiblement anormale

---

## 3. Spécifications Techniques Détaillées

### 3.1 Entrées/Sorties Attendues

#### Input
```
shm_scanner --target <PID> --pattern <STRING|HEX> [OPTIONS]

Exemples:
  shm_scanner --target 1234 --pattern "admin_token" --threads 4
  shm_scanner --target 5678 --pattern "0x4D5A90" --hex --threads 2
  shm_scanner --target 1234 --pattern "secret" --filter rw-p
```

#### Output (Stdout)
```
[MATCH] Adresse: 0x7ffee1234567 | PID: 1234 | Segment: [heap]
[MATCH] Adresse: 0x56557abcdef0 | PID: 1234 | Segment: [anon]
[INFO] Mémoire scannée: 342.5 MB | Durée: 0.234s | Threads: 4
[INFO] Matchs trouvés: 2
```

### 3.2 Architecture Système (Master/Worker)

```
┌─────────────────────────────────────────────────────────┐
│          SHM-Scanner Main (Master Thread)               │
├─────────────────────────────────────────────────────────┤
│ 1. Parse Arguments (PID, Pattern, Threads, Options)     │
│ 2. Ouvrir /proc/[PID]/maps (lecture)                   │
│ 3. Parser les segments mémoire                          │
│ 4. Filtrer les segments pertinents                      │
│ 5. Initialiser la Queue de Tâches (Mutex-protected)     │
│ 6. Créer N threads Workers                              │
│ 7. Attendre la fin de tous les Workers (join)           │
│ 8. Libérer proprement les ressources                    │
│ 9. Afficher le rapport final                            │
└─────────────────────────────────────────────────────────┘
         │
         ├─────────────────────────────────────┐
         │                                     │
    ┌────▼─────────┐              ┌──────────▼────────┐
    │  Worker 1    │              │  Worker 2..N      │
    ├──────────────┤              ├───────────────────┤
    │ Boucle:      │              │ Boucle:           │
    │ 1. Lock      │              │ 1. Lock           │
    │ 2. Pop Task  │              │ 2. Pop Task       │
    │ 3. Unlock    │              │ 3. Unlock         │
    │ 4. Lire Mem  │              │ 4. Lire Mem       │
    │ 5. Chercher  │              │ 5. Chercher       │
    │ 6. Report    │              │ 6. Report         │
    └──────────────┘              └───────────────────┘
```

### 3.3 Structures de Données Requises

#### Segment Mémoire
```
struct s_segment {
    unsigned long   start;       // Adresse début (hex)
    unsigned long   end;         // Adresse fin (hex)
    char            perms[5];    // rwxp (ex: "rw-p")
    char            name[256];   // Nom: [heap], [stack], [vdso], /path/...
};
```

#### Tâche d'Analyse
```
struct s_task {
    struct s_segment    segment;      // Segment à scanner
    unsigned char       *pattern;     // Pattern à rechercher
    size_t              pattern_len;  // Longueur du pattern
    int                 is_hex;       // 1 si hex, 0 si ASCII
};
```

#### Résultat de Match
```
struct s_match {
    unsigned long       address;      // Adresse absolue du match
    int                 pid;          // PID de la cible
    char                segment_name[256]; // Nom du segment
};
```

#### Queue de Tâches (Thread-Safe)
```
struct s_task_queue {
    struct s_task       *tasks;       // Tableau dynamique de tâches
    size_t              size;         // Nombre total de tâches
    size_t              index;        // Index courant (à traiter)
    pthread_mutex_t     mutex;        // Lock pour accès concurrent
    pthread_cond_t      cond;         // Condition variable
};
```

---

## 4. Contraintes Architecturales

### 4.1 Concurrence & Synchronisation

**Primitive obligatoire** : `pthread_mutex_t` pour protéger :
- L'accès à la queue de tâches (lecture de l'index courant)
- L'accès au buffer de résultats (journalisation des matchs)

**Condition Variable** : `pthread_cond_t` pour éviter un busy-wait (spinning) des workers en attente de tâches.

**Pattern de synchronisation attendu** :
```
Worker:
  while (!queue_vide) {
    mutex_lock()
    task = pop_task()
    if (tâche) {
      mutex_unlock()
      process_task(task)    // SANS LOCK - critique performance
      log_match(result)     // Avec LOCK si partagé
    } else {
      cond_wait()          // Attendre signal ou timeout
      mutex_unlock()
    }
  }
```

### 4.2 Gestion de la Mémoire

- **Allocation dynamique** : malloc/calloc pour structures variables (segments, queue)
- **Déallocation stricte** : Chaque malloc doit avoir un free correspondant dans la même portée logique
- **Buffer circulaire** : Pour la lecture de mémoire, utiliser un buffer fixe (ex: 4 KB) par worker pour éviter une copie massive
- **Validation Valgrind** : Le rapport final doit mentionner « No leaks are possible »

### 4.3 Gestion des Erreurs

**Chaque appel système doit être validé** :

| Appel | Erreur Critique | Récupération |
|---|---|---|
| `open(/proc/[pid]/maps)` | -1 | Afficher erreur + exit(1) |
| `read()` | 0 (EOF) ou -1 | Fermer descripteur + libérer ressources |
| `pthread_create()` | != 0 | Annuler tous les threads créés + libérer queue |
| `malloc()` | NULL | Afficher message + exit(1) |
| `pthread_join()` | != 0 | Considérer comme échoué, mais continuer nettoyage |

---

## 5. Normes & Contraintes de Code

### 5.1 Norme 42 Stricte

1. **Langage** : C pur (C99 ou C11)
2. **Fonctions** : Max 25 lignes (logic seulement, pas les braces seules)
3. **Variables par fonction** : Max 4 (hors boucles imbriquées)
4. **Déclarations** : En début de bloc (avant tout code)
5. **Pas de variables globales** : Sauf contexte threadé justifié (voir 5.2)
6. **Indentation** : TAB (largeur 4), pas d'espaces
7. **Lignes** : Max 80 caractères
8. **Fichiers** : Max 400 lignes par .c (archivage conseillé)

### 5.2 Exceptions Légitimes (Documentées)

Dans le contexte du multi-threading, une **struct de contexte globale** peut être acceptable **si justifiée** :
- Contexte des options CLI (read-only après init)
- Variables de synchronisation globales (mutex principal)

**Règle** : Documenter chaque exception en commentaire de la déclaration.

### 5.3 Compilation Stricte

```bash
gcc -Wall -Wextra -Werror -std=c99 -pthread -O2 \
    -pedantic -Wwrite-strings -Wshadow ...
```

**Zéro warning autorisé** à la compilation finale.

---

## 6. Phases de Développement

### Phase 1 : Fondations
- [ ] Parsing de `/proc/[pid]/maps` (isolé)
- [ ] Validation des structures `s_segment`
- [ ] Tests unitaires (comparaison vs. fichiers réels)

### Phase 2 : Accès Mémoire
- [ ] Ouverture et lecture de `/proc/[pid]/mem`
- [ ] Gestion des erreurs de permission
- [ ] Buffering par chunks (4-8 KB)

### Phase 3 : Moteur de Recherche
- [ ] Recherche ASCII brute (memchr-like)
- [ ] Recherche Hex (parsing hex string → octets)
- [ ] Test sur segments réels

### Phase 4 : Concurrence
- [ ] Pool de threads (créer/join)
- [ ] Queue de tâches (struct + mutex)
- [ ] Synchronisation (lock/unlock/signal)

### Phase 5 : Intégration & Optimisation
- [ ] Argument parsing (getopt)
- [ ] Rapportage structuré
- [ ] Profiling & optimisation performance
- [ ] Valgrind clean

### Phase 6 : Hardening & Furtivité
- [ ] Obfuscation patterns (optionnel V1.5)
- [ ] Minimisation des alertes comportementales
- [ ] Tests sur cibles réelles (harmless)

---

## 7. Critères de Succès (DoD - Definition of Done)

### Fonctionnel
- [ ] Scanner démarre sans argument → affiche usage
- [ ] Scanner accepte --target et --pattern
- [ ] Scanner exécute 1 worker sans race condition
- [ ] Scanner exécute N workers en parallèle
- [ ] Tous les matchs sont reportés avec adresse correcte
- [ ] Les patterns hex et ASCII sont supportés

### Technique
- [ ] Zéro fuite mémoire (valgrind report)
- [ ] Zéro warning compilation
- [ ] Norminette 42 clean
- [ ] Code compilable sur Linux/GCC, macOS/Clang, Windows/MinGW
- [ ] Tous les appels système vérifiés
- [ ] Performance : > 100 MB/s sur machine standard

### Documentation
- [ ] README.md avec mode d'emploi
- [ ] Commentaires stratégiques (pourquoi, pas quoi)
- [ ] Makefile complet

---

## 8. Métriques Opérationnelles (Red Team)

Avant de déployer le scanner réel sur cible :

| Métrique | Seuil Acceptable | Mesure |
|---|---|---|
| **Empreinte CPU** | < 40% monocore pendant scan | Via `top -p [pid]` |
| **Empreinte Mémoire** | < 50 MB | Via `ps aux \| grep shm_scanner` |
| **Détection par EDR** | Aucune alerte | Via simulation bac à sable |
| **Temps de scan** | < 5s pour 500 MB | Benchmark |
| **Taux de faux positifs** | 0 | Validation manuelle |

---

## 9. Livrables (Output Final)

```
shm_scanner/
├── Makefile                    # Build complet + clean
├── Includes/
│   ├── shm_scanner.h           # Prototypes + structures
│   └── thread_pool.h           # Pool API
├── Srcs/
│   ├── main.c                  # Entry point (< 25 lignes)
│   ├── parser.c                # /proc/[pid]/maps parsing
│   ├── scanner.c               # Moteur recherche
│   ├── thread_pool.c           # Implémentation workers
│   └── utils.c                 # Helpers (free, errors)
├── README.md                   # Documentation utilisateur
└── REPORTS/
    ├── norminette_report.txt   # Résultat 42 norminette
    ├── valgrind_report.txt     # Rapport mémoire
    └── compilation_log.txt     # Build log -Wall -Wextra -Werror
```

---

## 10. Points de Vigilance Red Team

1. **Éviter ptrace** : Les EDR monitorent PTRACE_ATTACH immédiatement
2. **Permissions** : `/proc/[pid]/mem` requiert UID==target_uid ou CAP_SYS_PTRACE
3. **Outils de détection** : Des scripts bash peuvent détecter ouvertures fréquentes de /proc/*
4. **Signature comportementale** : Eviter des patterns comme « boucle open/read » trop rapide
5. **Timing** : Sur cible surveillée, faire des micro-pauses entre tâches

---

**Fin du Cahier des Charges V1**
