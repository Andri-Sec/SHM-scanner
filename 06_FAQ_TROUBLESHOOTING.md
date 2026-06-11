# SHM-Scanner - FAQ & Troubleshooting

## 1. Architecture & Design

### Q: Pourquoi pas utiliser ptrace() directement ?

**R:** ptrace() est **lourd et détectable** :
- EDR/IDS monitorent ptrace immédiatement
- PTRACE_ATTACH sur process = RED FLAG
- Génère signal SIGSTOP sur cible
- Demande CAP_SYS_PTRACE (plus restrictif)

**SHM-Scanner utilise `/proc/[pid]/mem` à la place** :
- Pas d'appel ptrace (furtif)
- Requiert juste permissions de fichier
- Même UID que cible = accès direct
- Moins de bruit EDR

### Q: Pourquoi 4 variables max par fonction (Norme 42) ?

**R:** Cela force une **architecture simple et compréhensible** :
- Fonctions courtes = faciles à auditer
- Pas de état local complexe
- Lisibilité augmente (moins d'erreurs)
- En contexte threaded : moins de variables = moins de race conditions

**Stratégie** : Passer les structures en paramètre au lieu d'utiliser globales.

### Q: Pourquoi pas de variables globales ?

**R:** Les globales sont **difficiles à synchroniser en multi-threaded** :
- Race conditions insidieuses
- Harder à débugger (état partout)
- Préférer passer en paramètre ou struct de contexte

**Exception** : Contexte read-only (options CLI) alloué une fois au startup et partagé.

---

## 2. Multi-threading & Synchronisation

### Q: Quel risque si je n'utilise pas pthread_mutex_t ?

**R:** **Race condition = undefined behavior** :

```c
// MAUVAIS (race condition):
static int task_index = 0;
int current = task_index++;  // UNSAFE: 2+ threads peuvent lire même index

// BON (thread-safe):
pthread_mutex_lock(&lock);
int current = task_index++;
pthread_mutex_unlock(&lock);
// Seul 1 thread à la fois modifie task_index
```

Impact: Segments dupliqués, skipped, ou scanner crash.

### Q: Pourquoi pthread_cond_wait() et pas usleep() ?

**R:** `pthread_cond_wait()` est **efficace énergétiquement** :

```c
// MAUVAIS (busy-wait, drain CPU):
while (!pop_task(...)) {
  usleep(1000);  // Sleep 1 ms, check, repeat = CPU 100%
}

// BON (sleep propre):
while (!pop_task(...)) {
  pthread_cond_wait(&queue->cond, &queue->mutex);
  // Thread dormi COMPLÈTEMENT jusqu'à signal
  // Zéro CPU, zéro polling
}
```

### Q: Comment éviter le deadlock ?

**R:** Règles simples :

1. **Toujours acquérir locks dans le MÊME ordre**
   ```c
   // Consistant:
   lock(m1); lock(m2); ... unlock(m2); unlock(m1);
   // Jamais: lock(m2); lock(m1); ...
   ```

2. **Minimiser la durée du lock**
   ```c
   // Mauvais:
   mutex_lock();
   process_task();  // Peut être lent, beaucoup de calcul
   mutex_unlock();
   
   // Bon:
   mutex_lock();
   task = pop_task();
   mutex_unlock();
   process_task();  // Sans lock, parallèle
   ```

3. **Pas de appel bloquant inside lock**
   ```c
   // Mauvais:
   mutex_lock();
   read(fd, buf, 4096);  // Peut bloquer !
   mutex_unlock();
   
   // Bon:
   mutex_lock();
   index = pop_task();
   mutex_unlock();
   read(fd, buf, 4096);  // Après unlock
   ```

### Q: Peut-on utiliser rwlock au lieu de mutex ?

**R:** Oui, optionnel pour optimisation :

```c
// Avec rwlock (Read-Write lock):
pthread_rwlock_rdlock(&lock);  // Multiple readers simultanés
int has_task = pop_task(...);
pthread_rwlock_unlock(&lock);

// Avantage: Pop task est "read-only" sur queue
// Désavantage: Plus complexe, overkill si queue petite
```

Pour SHM-Scanner V1 : **mutex simple est plus facile et suffisant**.

---

## 3. Gestion Mémoire & Valgrind

### Q: J'ai "still reachable" dans Valgrind, c'est grave ?

**R:** Dépend du contexte :

```
"still reachable" = allocation jamais freed, mais pointer reste en scope

Exemple acceptable:
  int *p = malloc(10);
  exit(0);  // p pas freed, mais programme tue anyway
  // Valgrind: "still reachable" = OK (processus meurt)

Exemple INACCEPTABLE:
  while (condition) {
    int *p = malloc(10);
    // Jamais freed, et loop continue
  }
  // Valgrind: "definitely lost" = LEAK

Règle: Pour SHM-Scanner, viser "ERROR SUMMARY: 0 errors"
```

### Q: Comment tester mémoire pendant multi-threading ?

**R:** Utiliser Valgrind avec `--tool=helgrind` pour race conditions :

```bash
valgrind --tool=helgrind --threads=yes ./shm_scanner ...
# Détecte race conditions ET leaks mémoire
```

### Q: malloc() a échoué, que faire ?

**R:** Toujours vérifier et exit proprement :

```c
// Mauvais:
int *arr = malloc(huge_size);
arr[0] = value;  // CRASH si malloc échoué (NULL deref)

// Bon:
int *arr = malloc(huge_size);
if (!arr) {
  fprintf(stderr, "Erreur allocation mémoire\n");
  cleanup_resources();  // Libérer ce qui a déjà alloc
  exit(EXIT_FAILURE);
}
```

---

## 4. Pattern Matching & Recherche

### Q: Comment gérer patterns qui chevauchent ?

**R:** Chercher à chaque octet, pas juste au début de match :

```c
// Exemple: pattern "aa" dans "aaaa"
// Attendu: 3 matches (offsets 0, 1, 2)

// Algorithme (simple brute-force):
for (size_t i = 0; i <= buffer_size - pattern_len; i++) {
  if (memcmp(&buffer[i], pattern, pattern_len) == 0) {
    // MATCH à offset i
    i++;  // Continuer depuis i+1 pour chevauchement
  }
}

// Note: i++ à CHAQUE itération, pas i += pattern_len
```

### Q: Quel est le plus grand pattern accepté ?

**R:** Théoriquement illimité, pratiquement :

```c
// Limitation suggérée (éviter DoS):
#define MAX_PATTERN_LEN (1 << 20)  // 1 MB max

if (pattern_len > MAX_PATTERN_LEN) {
  fprintf(stderr, "Pattern trop long (max %d)\n", MAX_PATTERN_LEN);
  return -1;
}
```

### Q: Pattern hex "4D5A" c'est le header PE ?

**R:** Oui, "MZ" en ASCII = {0x4D, 0x5A} en hex = signature PE.

```c
// Chercher tous les exécables PE en mémoire:
./shm_scanner --target [pid] --pattern "4D5A" --hex --threads 4
```

---

## 5. Debugging & Troubleshooting

### Q: Mon scanner hang (freeze) après quelques secondes

**R:** Symptômes et causes probables :

| Symptôme | Cause Probable | Solution |
|---|---|---|
| Complètement frozen | Deadlock mutex | Ajouter timeout pthread_cond_wait() |
| CPU 100%, pas de progrès | Infinite loop dans worker | Vérifier logique pop_task |
| Hang après 1-2 segments | Segment inaccessible (EFAULT) | Gérer retour read() == 0 ou -1 |
| Workers créés mais ne terminent pas | cond_wait() jamais signalé | Ajouter broadcast avant exit |

### Q: Erreur "Too many open files"

**R:** Atteint limite ulimit :

```bash
# Vérifier limite actuelle
ulimit -n
# Default: souvent 1024

# Augmenter (temporary):
ulimit -n 4096

# Dans code, fermer fd après utilisation:
close(mem_fd);  // Après wait_workers()
```

### Q: GDB dit "Cannot access memory at address 0x..."

**R:** Normal, adresse de la cible pas accessible depuis scanner.

```bash
# Pour inspecter mémoire cible:
gdb attach [scanner_pid]  # Scanner doit être en pause

# MIEUX: inspecter à travers /proc/self/maps/mem du scanner
```

### Q: Compiler échoue "undefined reference to pthread functions"

**R:** Manque `-pthread` flag :

```bash
# MAUVAIS:
gcc -o shm_scanner main.c thread_pool.c
# Erreur: undefined reference to `pthread_create'

# BON:
gcc -pthread -o shm_scanner main.c thread_pool.c
# Ou:
gcc -o shm_scanner main.c thread_pool.c -lpthread
```

---

## 6. Performance & Optimisation

### Q: Comment améliorer de 100 MB/s à 200 MB/s ?

**R:** Optimisations possibles (ordre de priorité) :

1. **Augmenter buffer size** : 4 KB → 8 KB/16 KB
2. **Augmenter threads** : Tant que < nombre de CPU cores
3. **Réduire allocations** : Allouer une fois, réutiliser
4. **Compiler avec -O2** : GCC optimise automatiquement

```bash
gcc -O2 -Wall -Wextra -Werror -std=c99 -pthread ...
```

### Q: Plus de threads = plus rapide ?

**R:** Non, il y a un optimal :

```
Graphe throughput vs. threads:

Throughput
    |     
100 |     ___
    |    /   \
 50 |   /     \___
    |  /           
  0 +----+----+----+----+
    0    2    4    6    8 threads
    
Optimal = nombre de CPU cores (ex: 4 core = 4 threads best)
Au delà: contention > parallélisme
```

### Q: Comme profiler le scanner ?

**R:** Utiliser perf ou valgrind :

```bash
# Valgrind profiling:
valgrind --tool=callgrind ./shm_scanner ...
kcachegrind callgrind.out.[pid]  # Visualiser graphiquement

# Linux perf:
perf record ./shm_scanner ...
perf report
```

---

## 7. Sécurité & Furtivité

### Q: Mon binaire SHM-Scanner est détecté par antivirus, quoi faire ?

**R:** Plusieurs options :

1. **Renommer binaire** : `mv shm_scanner analyzer`
2. **Obfusquer strings** : XOR patterns de recherche (Phase 6)
3. **Compiler statiquement** : `-static` (standalone, pas de dépendances)
4. **UPX compression** : Réduire taille (mais ralentit légèrement)

```bash
gcc -static -O2 -Wall -Wextra -std=c99 -pthread -o scanner ...
upx --best scanner  # Optional, optional antivirus evasion
```

### Q: Peut-on cacher le scanner de process list (ps, top) ?

**R:** Non avec SHM-Scanner V1 (process normal).

Solutions V2+:
- Kernel rootkit (masquer process)
- LD_PRELOAD hooks (masquer du ps)
- Hors-scope pour V1

### Q: Comment nettoyer les traces après utilisation ?

**R:** Checklist :

```bash
# 1. Terminer scanner (déjà fini normalement)
pkill shm_scanner

# 2. Nettoyer history shell:
set +o history
history -c
rm ~/.bash_history

# 3. Supprimer binaire:
rm /tmp/shm_scanner

# 4. Nettoyer audit logs (nécessite root):
sudo auditctl -w /proc -p r  # Supprimer règle de monit
sudo truncate -s 0 /var/log/audit/audit.log

# 5. Vérifier pas de core dump:
ls /var/crash/*
```

---

## 8. Compilation & Portabilité

### Q: Comment compiler sur macOS ?

**R:** `/proc/[pid]/mem` n'existe pas sur macOS.

Options :
1. **Ignorer et compiler** : Code compile mais sera read-only (pas fonctionnel)
2. **Ajouter support macOS** : Utiliser `vm_read()` API au lieu de `/proc`
3. **Conteneur Linux** : Docker avec image Linux

Pour V1 : Accepter Linux-only.

### Q: Windows/MinGW : compatible ?

**R:** Partiellement.

Windows equivalent de `/proc/[pid]/mem` :
- API `ReadProcessMemory()` (natif Windows)
- Pas de `/proc` filesystem
- Nécessite réécriture du parsing

Pour V1 : Linux-first, Windows optionnel V2.

### Q: Compiler avec Clang au lieu de GCC ?

**R:** Oui, compatible :

```bash
clang -Wall -Wextra -Werror -std=c99 -pthread -o shm_scanner ...
```

Même comportement que GCC.

---

## 9. Tests & Validation

### Q: Comment tester pattern matching ?

**R:** Créer buffer de test avec patterns connus :

```c
unsigned char test_buf[] = "hello world hello test";
struct s_match *matches = NULL;
size_t count = 0;

search_pattern_ascii(test_buf, sizeof(test_buf), "hello", 5, 0x1000, &matches, &count);

// Vérifier:
assert(count == 2);
assert(matches[0].address == 0x1000);
assert(matches[1].address == 0x1000 + 12);

free(matches);
```

### Q: Comment tester race conditions ?

**R:** ThreadSanitizer (TSAN) détecte automatiquement :

```bash
gcc -fsanitize=thread -pthread -std=c99 -o test_threads ...
./test_threads

# Affichera détails de race conditions (si présentes)
```

### Q: Comment vérifier que TOUS les segments sont scannés ?

**R:** Compter segments et vérifier chaque est traité :

```bash
# Segments attendus:
cat /proc/[pid]/maps | wc -l
# Exemple: 50 segments

# Scanner doit traiter 50 segments:
./shm_scanner --target [pid] --pattern "xxxxxxxx" --threads 4 2>&1 | grep "segments"
```

---

## 10. Concepts Avancés

### Q: Peut-on scanner plusieurs processus en parallèle ?

**R:** Non avec V1 (un PID à la fois).

Optimisation V2 :
```
Multi-target mode:
  ./shm_scanner --targets "1234,5678,9012" --pattern "secret" --threads 8
  // Répartir threads entre 3 processus
```

### Q: Quel pattern pour chercher adresses mémoire absolues ?

**R:** Difficile car dépend de ASLR.

Alternative :
- Chercher strings / magics constants au lieu d'adresses absolues
- Utiliser patterns de données (ex: tokens base64)

### Q: Comment éviter faux positifs ?

**R:** Améliorer spécificité du pattern :

```bash
# Mauvais (beaucoup faux positifs):
./shm_scanner --target [pid] --pattern "test"  # Trop générique

# Meilleur (plus ciblé):
./shm_scanner --target [pid] --pattern "api_token_abc123"  # Plus long = moins FP
```

---

**Fin de la FAQ**
