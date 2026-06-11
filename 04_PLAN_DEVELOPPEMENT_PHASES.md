# SHM-Scanner - Plan de Développement Progressif

## Philosophie

Ce projet suit une **stratégie d'apprentissage par palliers** (iterative mastery) :

1. **Maîtriser une composante à la fois**
2. **Valider complètement avant d'avancer**
3. **Intégrer progressivement**
4. **Profiler et optimiser en dernier**

Chaque phase doit atteindre 100% (ACCEPT), pas de "bon enough".

---

## Phase 1 : Fondations — Parser `/proc/[pid]/maps`

**Objectif** : Maîtriser la lecture du système de fichiers `/proc` et l'extraction de la topologie mémoire.

**Durée estimée** : 3-4 heures  
**Dépendance** : Aucune

### 1.1 Étapes

#### 1.1.1 Création de la structure de données

- [ ] Créer `includes/shm_scanner.h`
- [ ] Définir `struct s_segment` avec champs : start, end, perms, name
- [ ] Valider la taille et l'alignment (pas de padding surprise)
- [ ] Documenter la structure en commentaire

**Validation** :
```c
// Exemple de test simple dans main.c temporaire
struct s_segment seg = {0x56556000, 0x56578000, "r-xp", "/bin/bash"};
assert(seg.start < seg.end);
```

#### 1.1.2 Implémentation du parser basique

- [ ] Créer `srcs/parser.c`
- [ ] Implémenter : `struct s_segment *parse_proc_maps(int pid, size_t *count)`
- [ ] Ouvrir `/proc/[pid]/maps` en mode lecture
- [ ] Lire ligne par ligne
- [ ] Parser chaque ligne avec `sscanf()` ou équivalent
- [ ] Allouer dynamiquement le tableau de segments
- [ ] Gérer les erreurs (fichier absent, permissions)

**Test Manuel** :
```bash
./test_parser self
# Afficher les segments du processus courant
# Comparer avec: cat /proc/self/maps
```

#### 1.1.3 Filtrage et tri

- [ ] Implémenter : `struct s_segment *filter_segments(...)`
- [ ] Trier segments par start address croissante (qsort)
- [ ] Filtrer par permissions (rw-p, r-xp, etc.)
- [ ] Ignorer segments non-lisibles (---)

**Test** :
```bash
# Vérifier ordre et filtrage
./test_parser self | grep "rw-p" | head
```

#### 1.1.4 Validation de segments

- [ ] Implémenter : `int is_segment_readable(const struct s_segment *)`
- [ ] Vérifier permission 'r' présente dans `perms`
- [ ] Exclure segments --- (non-accessibles)

#### 1.1.5 Nettoyage

- [ ] Implémenter fonction de libération mémoire
- [ ] Tester avec valgrind : zéro fuite

**Validation Phase 1** :
```bash
make clean && make parser_test
valgrind --leak-check=full ./parser_test self

# Vérifier:
# 1. Output correctement parsé
# 2. Segments triés par start address
# 3. Valgrind: "ERROR SUMMARY: 0 errors"
# 4. Norminette clean
```

---

## Phase 2 : Accès Mémoire — Lecture de `/proc/[pid]/mem`

**Objectif** : Maîtriser la lecture sécurisée de la mémoire virtuelle d'un processus.

**Durée estimée** : 3-4 heures  
**Dépendance** : Phase 1

### 2.1 Étapes

#### 2.1.1 Ouverture de `/proc/[pid]/mem`

- [ ] Créer `srcs/scanner.c`
- [ ] Implémenter : `int open_target_memory(int pid)`
- [ ] Ouvrir fichier `/proc/[pid]/mem` en mode O_RDONLY
- [ ] Vérifier code retour (errno = EPERM si permissions insuffisantes)
- [ ] Gérer le cas où `/proc/[pid]/mem` n'existe pas

**Test** :
```bash
./test_scanner open_mem $$
# Afficher fd obtenu
```

#### 2.1.2 Lecture avec lseek + read

- [ ] Implémenter : `ssize_t read_memory_segment(...)`
- [ ] Utiliser `lseek(fd, address, SEEK_SET)`
- [ ] Utiliser `read(fd, buffer, size)`
- [ ] Gérer cas : EOF (retour 0), erreur (EFAULT = -1)
- [ ] Ne JAMAIS faire busyloop sur read() court

**Détail important** :
```c
// read() peut retourner moins de size octets !
// Pas d'erreur, juste fin du segment
ssize_t bytes = read(fd, buf, 4096);
if (bytes == -1) {
  perror("read");  // VRAI erreur
} else if (bytes == 0) {
  printf("EOF atteint\n");
} else {
  printf("Lus %zd octets\n", bytes);
}
```

#### 2.1.3 Buffer circulaire pour efficacité

- [ ] Implémenter un buffer fixe (4 KB recommandé)
- [ ] Lire par chunks plutôt que octet par octet
- [ ] Calculer offset correctement dans chaque chunk

**Architecture** :
```
Segment [0x1000 - 0x5000]
  Chunk 1: lire [0x1000 - 0x1FFF]  (4 KB)
  Chunk 2: lire [0x2000 - 0x2FFF]  (4 KB)
  Chunk 3: lire [0x3000 - 0x3FFF]  (4 KB)
  Chunk 4: lire [0x4000 - 0x4FFF]  (4 KB)
```

#### 2.1.4 Test de lecture réelle

- [ ] Scanner sa propre mémoire (self)
- [ ] Lire une adresse connue (ex: adresse de variable stack)
- [ ] Vérifier que les octets lu correspondent à la variable

**Exemple** :
```c
int my_var = 0x12345678;
unsigned char buf[4];
unsigned long addr = (unsigned long)&my_var;
ssize_t n = read_memory_segment(mem_fd, addr, 4, buf);
// buf doit contenir "78 56 34 12" (little-endian)
```

#### 2.1.5 Gestion erreurs robuste

- [ ] Tester avec PID invalide (99999)
- [ ] Tester sans permissions (root process)
- [ ] Vérifier messages d'erreur clairs
- [ ] Valgrind clean

**Validation Phase 2** :
```bash
make clean && make scanner_test
valgrind --leak-check=full ./scanner_test self

# Vérifier:
# 1. Lecture correcte de /proc/self/mem
# 2. Gestion erreur sur PID invalide
# 3. Lecture par chunks sans perte
# 4. Valgrind clean
# 5. Norminette clean
```

---

## Phase 3 : Moteur de Recherche — Pattern Matching

**Objectif** : Implémenter recherche efficace (ASCII et Hex).

**Durée estimée** : 4-5 heures  
**Dépendance** : Phase 2

### 3.1 Étapes

#### 3.1.1 Recherche ASCII basique

- [ ] Implémenter : `int search_pattern_ascii(...)`
- [ ] Utiliser `memmem()` ou boucle manuelle
- [ ] Trouver TOUS les matchs dans le buffer (overlaps inclus)
- [ ] Calculer adresse absolue = base_address + offset

**Test** :
```c
unsigned char buf[] = "hello world hello";
struct s_match *matches = NULL;
size_t count = 0;
search_pattern_ascii(buf, strlen(buf), "hello", 5, 0x1000, &matches, &count);
// Expected: 2 matches à 0x1000 et 0x1000 + 12
```

#### 3.1.2 Conversion Hex string → octets

- [ ] Implémenter : `int parse_hex_string(...)`
- [ ] Parser "4D5A90" → {0x4D, 0x5A, 0x90}
- [ ] Valider longueur paire
- [ ] Valider caractères [0-9A-Fa-f]
- [ ] Allouer dynamiquement

**Exemples** :
```c
unsigned char *bytes = NULL;
size_t len = 0;

// Valide
parse_hex_string("4D5A", &bytes, &len);  // {0x4D, 0x5A}

// Invalide
parse_hex_string("4D5", &bytes, &len);   // Erreur: longueur impaire
parse_hex_string("4GZ", &bytes, &len);   // Erreur: caractères invalides
```

#### 3.1.3 Recherche Hex

- [ ] Implémenter : `int search_pattern_hex(...)`
- [ ] Utiliser même logique que ASCII mais sur octets bruts
- [ ] Supporter patterns binaires (ex: signature PE "MZ\x90")

**Test** :
```c
unsigned char buf[] = {0x4D, 0x5A, 0x90, 0x00, 0xFF};
unsigned char pattern[] = {0x4D, 0x5A, 0x90};
struct s_match *matches = NULL;
size_t count = 0;
search_pattern_hex(buf, 5, pattern, 3, 0x1000, &matches, &count);
// Expected: 1 match à 0x1000
```

#### 3.1.4 Gestion des chevauchements

- [ ] Pattern "aa" dans "aaaa" doit trouver 3 matchs (offsets 0, 1, 2)
- [ ] Pas de saut entre matches
- [ ] Vérifier avec patterns de 1 byte

#### 3.1.5 Allocation/libération des resultats

- [ ] Allouer dynamiquement les matchs trouvés
- [ ] Respecter 4 variables max par fonction
- [ ] Valgrind clean

**Validation Phase 3** :
```bash
make clean && make search_test
valgrind ./search_test

# Tests:
# 1. Pattern ASCII simple (1 match)
# 2. Pattern ASCII multiple (3+ matches)
# 3. Pattern chevauchant
# 4. Pattern Hex
# 5. Pattern absent
# 6. Buffer vide
# 7. Pattern > buffer
# 8. Valgrind clean
# 9. Norminette clean
```

---

## Phase 4 : Concurrence — Pool de Threads POSIX

**Objectif** : Implémenter multi-threading thread-safe avec mutex/cond.

**Durée estimée** : 6-8 heures (la plus complexe)  
**Dépendance** : Phase 3

### 4.1 Étapes

#### 4.1.1 Structure de queue de tâches

- [ ] Créer `includes/thread_pool.h`
- [ ] Définir `struct s_task_queue` : tasks[], size, index, mutex, cond
- [ ] Documenter la synchronisation

#### 4.1.2 Initialisation de la queue

- [ ] Implémenter : `struct s_task_queue *init_task_queue(...)`
- [ ] Allouer tableau de s_task
- [ ] `pthread_mutex_init(&queue->mutex, NULL)`
- [ ] `pthread_cond_init(&queue->cond, NULL)`
- [ ] Index courant = 0

**Important** :
```c
// Mutex et Condition Variable sont des "objets" à initialiser
// Pas de malloc, simple initialisation en place
pthread_mutex_t m;
pthread_mutex_init(&m, NULL);  // Pas pthread_mutex_t *m = malloc(...)
```

#### 4.1.3 Pop tâche (thread-safe)

- [ ] Implémenter : `int pop_task(struct s_task_queue *, struct s_task *)`
- [ ] **DOIT être appelé avec queue->mutex déjà verrouillé**
- [ ] Vérifier queue->index < queue->size
- [ ] Incrémenter index
- [ ] Copier s_task vers out_task
- [ ] Retourner 1 (succès) ou 0 (épuisé)

**Pattern** :
```c
// Dans le worker:
pthread_mutex_lock(&queue->mutex);
int has_task = pop_task(queue, &task);
pthread_mutex_unlock(&queue->mutex);
```

#### 4.1.4 Créer et démarrer les workers

- [ ] Implémenter : `int launch_workers(...)`
- [ ] Allouer tableau pthread_t
- [ ] Boucle : pthread_create() pour chaque worker
- [ ] Retourner le tableau de thread IDs

**Attention à la gestion d'erreur** :
```c
for (int i = 0; i < num_threads; i++) {
  int err = pthread_create(&threads[i], NULL, worker_thread, queue);
  if (err != 0) {
    // ANNULER les threads déjà créés !
    // Ne pas créer de nouveaux sinon deadlock
    for (int j = 0; j < i; j++) {
      pthread_cancel(threads[j]);  // ou pthread_join pour arrêt propre
    }
    free(threads);
    return -1;
  }
}
```

#### 4.1.5 Boucle worker

- [ ] Implémenter : `void *worker_thread(void *arg)`
- [ ] Boucle jusqu'à épuisement des tâches
- [ ] Comportement :
  ```
  while (true) {
    lock()
    has_task = pop_task()
    unlock()
    
    if (has_task) {
      process_task(task)
    } else {
      wait_condition()  // Dormir jusqu'à signal
    }
  }
  ```

**Détail important : éviter busy-wait** :
```c
// MAUVAIS (CPU 100%, drain battery):
while (!pop_task(queue, &task)) {
  usleep(1000);  // Busy loop
}

// BON (sleep propre):
while (!pop_task(queue, &task)) {
  pthread_cond_wait(&queue->cond, &queue->mutex);  // Attendre signal
}
```

#### 4.1.6 Attendre fin des workers

- [ ] Implémenter : `int wait_workers(...)`
- [ ] Boucle : `pthread_join()` pour chaque thread
- [ ] Vérifier code retour (0 = succès)

```c
int wait_workers(pthread_t *threads, int num) {
  for (int i = 0; i < num; i++) {
    int err = pthread_join(threads[i], NULL);
    if (err != 0) {
      fprintf(stderr, "Thread %d join failed\n", i);
      return -1;
    }
  }
  return 0;
}
```

#### 4.1.7 Nettoyage queue

- [ ] Implémenter : `void cleanup_task_queue(...)`
- [ ] `pthread_mutex_destroy(&queue->mutex)`
- [ ] `pthread_cond_destroy(&queue->cond)`
- [ ] `free(queue->tasks)`
- [ ] `free(queue)`

**Ordre important** :
```c
// Pas d'accès aux locks APRÈS destroy
// destroy doit être APRÈS le dernier pthread_join()
```

#### 4.1.8 Test séquentiel (1 thread)

- [ ] Créer 1 queue avec 3 segments
- [ ] Lancer 1 worker
- [ ] Vérifier que tous les 3 segments sont traités
- [ ] Pas de crash, pas de infini loop

#### 4.1.9 Test parallèle (4 threads)

- [ ] 100 segments, 4 workers
- [ ] Vérifier que CHAQUE segment est traité exactement 1 fois
- [ ] Pas de double traitement
- [ ] Pas de skip
- [ ] Pas de timeout (< 5 secondes)

#### 4.1.10 Détection race conditions

- [ ] Compiler avec ThreadSanitizer (TSAN) :
  ```bash
  gcc -std=c99 -fsanitize=thread -pthread ...
  ```
- [ ] Lancer tests : zéro race condition reportée

**Validation Phase 4** :
```bash
make clean && make thread_test
# Compiler avec TSAN
gcc -std=c99 -fsanitize=thread -Wall -Wextra -Werror -pthread \
    -o thread_test srcs/thread_pool.c srcs/test_threads.c

# Tests:
# 1. init/cleanup queue
# 2. pop_task correctness
# 3. 1 worker, N segments → tout traité
# 4. 4 workers, 100 segments → distribution uniforme
# 5. TSAN report: zero race conditions
# 6. Valgrind clean
# 7. Norminette clean
```

---

## Phase 5 : Intégration Complète

**Objectif** : Assembler tous les modules + parsing CLI.

**Durée estimée** : 5-6 heures  
**Dépendance** : Phases 1-4

### 5.1 Étapes

#### 5.1.1 Argument parsing

- [ ] Implémenter : `int parse_arguments(...)`
- [ ] Gérer : --target, --pattern, --threads, --hex, --filter, --help
- [ ] Valider PID, pattern_len, threads (1-64)
- [ ] Afficher usage si invalide

#### 5.1.2 Main orchestration

- [ ] Créer structure de contexte
- [ ] Séquence :
  1. parse_arguments()
  2. parse_proc_maps()
  3. filter_segments()
  4. open_target_memory()
  5. init_task_queue()
  6. launch_workers()
  7. wait_workers()
  8. collect_results()
  9. print_report()
  10. cleanup()

#### 5.1.3 Worker logic complet

- [ ] Chaque worker doit :
  1. pop_task()
  2. read_memory_segment() par chunks
  3. search_pattern_ascii() ou search_pattern_hex()
  4. log_match() (thread-safe avec mutex sur results)

#### 5.1.4 Rapportage

- [ ] Afficher : address, PID, segment_name pour chaque match
- [ ] Afficher résumé : total mémoire scannée, nb matchs, durée

**Format** :
```
[MATCH] 0x7ffee1234567 | PID: 1234 | Segment: [heap]
[MATCH] 0x56557abcdef0 | PID: 1234 | Segment: [anon]
[INFO] Mémoire scannée: 342.5 MB | Durée: 0.234s | Threads: 4
[INFO] Matchs trouvés: 2
```

#### 5.1.5 Test d'intégration

- [ ] Scanner son propre PID avec un pattern ASCII
- [ ] Scanner avec pattern Hex
- [ ] Scanner avec filtrage
- [ ] Vérifier que les matchs sont corrects

**Exemple** :
```bash
# Créer un buffer de test
echo "admin_password_secret" > /tmp/test_mem

# Chercher le pattern
./shm_scanner --target [PID_of_echo] --pattern "password"
# Devrait trouver 1 match

# Avec hex
./shm_scanner --target $$ --pattern "7B5D" --hex
# Chercher '{}' en hex (0x7B 0x5D)
```

**Validation Phase 5** :
```bash
make clean && make all

# Tests:
# 1. ./shm_scanner (sans args) → usage
# 2. ./shm_scanner --target $$ --pattern "test"
# 3. ./shm_scanner --target invalid_pid → erreur appropriée
# 4. ./shm_scanner --target $$ --pattern "pwd" --threads 4
# 5. Valgrind clean
# 6. Norminette clean
# 7. Zéro warning compilation
```

---

## Phase 6 : Optimisation & Hardening

**Objectif** : Performance, sécurité, furtivité.

**Durée estimée** : 4-5 heures  
**Dépendance** : Phase 5

### 6.1 Étapes

#### 6.1.1 Profiling

- [ ] Mesurer temps par segment
- [ ] Identifier goulots d'étranglement (parsing vs. scanning vs. sync)
- [ ] Benchmark : MB/s scannés

#### 6.1.2 Optimisations sans-perte

- [ ] Buffer size tuning (4 KB vs. 8 KB vs. 16 KB)
- [ ] Thread count tuning
- [ ] Réduire locks (minimiser critique section)

#### 6.1.3 Obfuscation patterns (optionnel V1.5)

- [ ] XOR patterns en mode compilation
- [ ] Déchiffrer à runtime
- [ ] Pas de string de recherche en clair dans binaire

#### 6.1.4 Minimisation empreinte

- [ ] Éviter allocations répétées
- [ ] Réutiliser buffers
- [ ] Limiter log pendant scan

#### 6.1.5 Sécurité finale

- [ ] Address Space Layout Randomization (ASLR) compatible
- [ ] Stack canaries activés
- [ ] Format string safe

```bash
gcc -Wall -Wextra -Werror -std=c99 -pthread \
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    -O2 -pie -fPIE
```

#### 6.1.6 Documentation finale

- [ ] README.md complet
- [ ] Examples d'usage
- [ ] Limitations connues (ex: /proc spécifique Linux)
- [ ] Architecture overview

**Validation Phase 6** :
```bash
# Performance
time ./shm_scanner --target $$ --pattern "test" --threads 4
# Expected: < 5s pour 500 MB

# Sécurité
checksec --file=shm_scanner
# Vérifier: Canary enabled, NX enabled, PIE enabled

# Valgrind
valgrind --leak-check=full ./shm_scanner --target $$ --pattern "x"

# Final check
make clean && make all
norminette srcs/ includes/
gcc -Wall -Wextra -Werror -std=c99 -pthread -O2
```

---

## Timeline Résumé

| Phase | Composant | Durée | Statut |
|---|---|---|---|
| 1 | Parser /proc/maps | 3-4h | À faire |
| 2 | Lecture /proc/mem | 3-4h | À faire |
| 3 | Pattern Matching | 4-5h | À faire |
| 4 | Multi-threading | 6-8h | À faire |
| 5 | Intégration | 5-6h | À faire |
| 6 | Optimisation | 4-5h | À faire |
| **Total** | **SHM-Scanner V1** | **~30h** | **À démarrer** |

---

## Stratégie de Validation

Pour chaque phase :

1. **Implémenter les fonctions contrat-to-contract**
2. **Tester isolément** (unit tests)
3. **Valider avec Valgrind** (zéro leak)
4. **Compiler avec -Wall -Wextra -Werror** (zéro warning)
5. **Passer la norminette** (42 compliant)
6. **ACCEPT ou REJECT** (binaire)

---

**Prêt à démarrer Phase 1 ?**
