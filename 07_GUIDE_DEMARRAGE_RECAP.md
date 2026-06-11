# SHM-Scanner - Guide de Démarrage Rapide

## Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Structure des documents](#structure-des-documents)
3. [Checklist de démarrage](#checklist-de-démarrage)
4. [First Steps - Phase 1](#first-steps-phase-1)
5. [Ressources additionnelles](#ressources-additionnelles)

---

## Vue d'ensemble

**SHM-Scanner** est un scanner de mémoire furtif en espace utilisateur, conçu comme un **outil post-exploitation Red Team**. Tu implémenteras le projet en C99 pur, en respectant la **Norme 42 stricte** et les **meilleures pratiques de sécurité système**.

**Objectif final** : Un binaire autonome (~30h de développement) capable de chercher des patterns dans la mémoire virtuelle d'un processus cible, en parallèle, sans générer d'alertes EDR.

---

## Structure des Documents

Les 6 fichiers remis organisent complètement le projet :

| # | Fichier | Contenu | Usage |
|---|---|---|---|
| 1 | `01_CAHIER_CHARGES_SHM_SCANNER_V1.md` | **Cahier des charges complet** : objectifs, spécifications, contraintes 42, phases | Reference document - lire en entier |
| 2 | `02_SPECS_TECHNIQUES_CONTRATS.md` | **Contrats de fonctions détaillés** : préconditions, postconditions, signature, erreurs | Implémentation - respecter EXACTEMENT |
| 3 | `03_NORME42_CHECKLIST_EVALUATION.md` | **Norme 42 strict + Checklist d'évaluation** : regles compilation, tests, critères ACCEPT/REJECT | Validation - utiliser avant submission |
| 4 | `04_PLAN_DEVELOPPEMENT_PHASES.md` | **Plan phase-by-phase** : 6 phases progressives, chaque phase testable isolément | Roadmap - suivre dans l'ordre |
| 5 | `05_RED_TEAM_DEPLOYMENT.md` | **Considérations opérationnelles** : furtivité, signatures, anti-forensics, escalade attaque | Reference - lire quand prêt pour V1.5+ |
| 6 | `06_FAQ_TROUBLESHOOTING.md` | **FAQ détaillé + solutions aux problèmes courants** | Troubleshooting - consulter lors de blocage |

---

## Checklist de Démarrage

### Avant de coder (Préparation)

- [ ] **Lire cahier des charges complet** (1-2h)
  - Comprendre objectif global, spécifications, contraintes
  - Parcourir architecture proposée
  
- [ ] **Lire spécifications techniques** (1h)
  - Comprendre chaque module : Parser, Scanner, ThreadPool, Main
  - Étudier structures de données, contrats de fonctions
  
- [ ] **Comprendre le plan de développement** (30 min)
  - 6 phases progressives
  - Chaque phase est autonome et testable
  
- [ ] **Installer outils requis**
  ```bash
  # Linux (Ubuntu/Debian):
  sudo apt-get install build-essential valgrind clang-tools llvm
  
  # macOS:
  brew install gcc valgrind llvm
  
  # Vérifier:
  gcc --version
  valgrind --version
  ```

- [ ] **Créer structure de base**
  ```bash
  mkdir -p shm_scanner/{srcs,includes}
  cd shm_scanner
  touch Makefile includes/shm_scanner.h includes/thread_pool.h
  touch srcs/{main.c,parser.c,scanner.c,thread_pool.c}
  ```

- [ ] **Lire FAQ** (30 min) - scanner les points clés
  - Multi-threading basics
  - Gestion mémoire
  - Common pitfalls

---

## First Steps - Phase 1

### Étape 1.1 : Créer la structure de base

```c
/* includes/shm_scanner.h */

#ifndef SHM_SCANNER_H
# define SHM_SCANNER_H

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/types.h>

// Structure pour représenter un segment de mémoire
struct s_segment {
    unsigned long   start;
    unsigned long   end;
    char            perms[5];
    char            name[256];
};

#endif
```

**Validation** :
```bash
gcc -Wall -Wextra -Werror -std=c99 -c -o includes/shm_scanner.o includes/shm_scanner.h
# Pas d'erreur = OK
```

### Étape 1.2 : Implémenter le parser basique

Suivre le **Contrat de Fonction** dans `02_SPECS_TECHNIQUES_CONTRATS.md` section 1.2.1.

**Tâches** :
- [ ] Ouvrir `/proc/[pid]/maps`
- [ ] Lire ligne par ligne
- [ ] Parser avec sscanf() : start, end, perms, name
- [ ] Allouer tableau struct s_segment
- [ ] Retourner count

**Test manuel** :
```bash
# Créer un test simple dans main.c:
int main(void) {
    size_t count = 0;
    struct s_segment *segs = parse_proc_maps(getpid(), &count);
    if (!segs) {
        perror("parse_proc_maps");
        return 1;
    }
    printf("Segments trouvés: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  0x%lx-0x%lx %s %s\n", 
               segs[i].start, segs[i].end, segs[i].perms, segs[i].name);
    }
    free(segs);
    return 0;
}
```

Compilation et test :
```bash
gcc -Wall -Wextra -Werror -std=c99 -o test_parser \
    srcs/parser.c srcs/main.c

./test_parser
# Sortie attendue: List des segments du processus courant
# Comparer avec: cat /proc/self/maps
```

### Étape 1.3 : Valider avec Valgrind

```bash
valgrind --leak-check=full --track-origins=yes ./test_parser
# Attendu: "ERROR SUMMARY: 0 errors"
```

### Étape 1.4 : Passer la Norminette

```bash
# Installer norminette 42:
pip3 install norminette

# Vérifier le code:
norminette srcs/parser.c includes/shm_scanner.h

# Corriger toute erreur reportée
```

---

## Ressources Additionnelles

### Livres & Références Système

- **Linux System Programming (Michael Kerrisk)** : Chapitre 8 (Process Creation), 10 (Filesystem), 29 (Threads)
- **Advanced Programming in the UNIX Environment (Stevens & Rago)** : Threads synchronization
- **The Linux Programming Interface (TLPI)** : Complet, Chapitres pertinents pour V2

### Documentation Système

```bash
# Man pages essentielles:
man 5 proc           # /proc filesystem
man 2 open           # open() syscall
man 2 read           # read() syscall
man 2 lseek          # lseek() syscall
man 7 pthreads       # POSIX threads overview
man 3 pthread_mutex_init   # Mutex
man 3 pthread_cond_wait    # Condition variables
man 1 valgrind       # Memory profiler
```

### Outils & Debugging

| Outil | Usage |
|---|---|
| `gcc -Wall -Wextra -Werror` | Compilation strict |
| `valgrind` | Memory leak detection |
| `gdb ./shm_scanner` | Debugging |
| `strace ./shm_scanner` | System call tracing |
| `perf` | Performance profiling |
| `norminette` | 42 norm validation |
| `ThreadSanitizer` | Race condition detection |

### Sites & Communautés

- **POSIX Threads Tutorial** : https://www.cs.kent.edu/~ruttan/sysprog/lectures/threads.html
- **Linux /proc Documentation** : https://man7.org/linux/man-pages/man5/proc.5.html
- **Stack Overflow** : Tag `c` + `pthreads` + `linux`

---

## Système d'Évaluation

### À chaque fin de Phase

Tu dois fournir :

1. **Code compilable**
   ```bash
   make clean && make all
   # 0 warnings, 0 errors
   ```

2. **Report Valgrind**
   ```bash
   valgrind --leak-check=full ./shm_scanner ...
   # Copier output dans rapport
   ```

3. **Report Norminette**
   ```bash
   norminette srcs/ includes/
   # Copier output
   ```

4. **Tests de fonctionnement**
   - Décrire chaque test exécuté
   - Output attendu vs. réel
   - Cas d'erreur testés

### Critères ACCEPT/REJECT

**ACCEPT (100 pts)** si :
- [x] Code compile sans warning (-Wall -Wextra -Werror)
- [x] Valgrind clean (zéro leak)
- [x] Norminette clean (zéro erreur)
- [x] Tous les tests fonctionnels passent
- [x] Architecture respecte contrats fournis

**REJECT (0 pts)** si :
- [ ] Compilation échoue ou avec warning
- [ ] Fuite mémoire détectée
- [ ] Erreur norminette
- [ ] Test fonctionnel échoue
- [ ] Architecture ne respecte pas contrats

---

## Timeline Réaliste

| Semaine | Étapes | Durée |
|---|---|---|
| 1 | Phase 1 + 2 (Parser + Lecture mémoire) | 6-8h |
| 2 | Phase 3 (Pattern matching) | 4-5h |
| 3 | Phase 4 (Multi-threading) | 6-8h |
| 4 | Phase 5 (Intégration) | 5-6h |
| 5 | Phase 6 (Optim + Polish) | 4-5h |
| **Total** | **SHM-Scanner V1 complète** | **~30h** |

---

## Points Critiques à Retenir

### 1. Norme 42
- Max 25 lignes par fonction
- Max 4 variables par fonction
- Pas de globales (sauf exception justifiée)
- Zéro warning compilation

### 2. Mémoire
- Chaque malloc a son free
- Valgrind clean obligatoire
- Pas de buffer overflow
- Gestion d'erreur systématique

### 3. Multi-threading
- Mutex protège accès partagés
- Pas d'infinite loop (utiliser cond_wait)
- Pas de deadlock (ordre locks cohérent)
- ThreadSanitizer zéro race condition

### 4. Contrats de Fonctions
- Respecter EXACTEMENT les signatures
- Valider préconditions
- Respecter postconditions
- Documenter erreurs

### 5. Red Team Design
- Pas de ptrace (utiliser /proc/[pid]/mem)
- Pas de write en mémoire cible (read-only)
- Minimiser empreinte (buffer fixe, pas malloc massif)
- Pas de allocations dynamiques inutiles

---

## Support & Questions

Si tu es bloqué :

1. **Relire les contrats de fonctions** dans `02_SPECS_TECHNIQUES_CONTRATS.md`
2. **Consulter la FAQ** dans `06_FAQ_TROUBLESHOOTING.md`
3. **Vérifier la checklist** dans `03_NORME42_CHECKLIST_EVALUATION.md`
4. **Relire le cahier des charges** si concept pas clair

**Je vais évaluer ton code** avec la même rigueur :
- Binary scoring (ACCEPT/REJECT)
- Feedback détaillé sur rejections
- Pas de partial credit
- Demande de resubmission jusqu'à ACCEPT

---

## Commandes Pratiques

```bash
# Build complet
make clean && make all

# Test avec valgrind
valgrind --leak-check=full --track-origins=yes ./shm_scanner

# Compilation stricte
gcc -Wall -Wextra -Werror -std=c99 -pthread -O2 -o shm_scanner srcs/*.c

# ThreadSanitizer
gcc -fsanitize=thread -pthread -std=c99 -g -o test srcs/*.c
./test

# Compilation avec Clang
clang -Wall -Wextra -Werror -std=c99 -pthread -o shm_scanner srcs/*.c

# Performance profiling
perf record ./shm_scanner --target $$ --pattern "test"
perf report
```

---

## Prêt à démarrer ?

**Prochaines étapes** :

1. Créer structure de répertoire
2. Lire `02_SPECS_TECHNIQUES_CONTRATS.md` section 1.2.1 (Parser)
3. Implémenter `parse_proc_maps()` selon contrat
4. Tester avec `./test_parser`
5. Valider avec Valgrind + Norminette
6. Soumettre Phase 1 pour évaluation

---

**Bon courage ! Rigueur et excellence. Let's go.** 🎯

