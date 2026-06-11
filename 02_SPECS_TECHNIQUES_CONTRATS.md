# SHM-Scanner - Spécifications Techniques & Contrats

## 1. Architecture Module-par-Module

### 1.1 Module Parser (`parser.c`)

**Rôle** : Lire et analyser `/proc/[pid]/maps` pour extraire la topologie mémoire cible.

#### Entrée
```
/proc/[pid]/maps : Fichier texte système.

Format de ligne:
  56556000-56578000 r-xp 00000000 08:02 2100289  /bin/bash
  ^^^^^^^^ ^^^^^^^^ ^^^^ ^^^^^^^^ ^^^^^ ^^^^^^  ^^^^^
  start    end      perm offset  maj:  inode    name
```

#### Sortie Attendue
```c
Array de struct s_segment:
  {
    .start  = 0x56556000
    .end    = 0x56578000
    .perms  = "r-xp"
    .name   = "/bin/bash"
  }
  ...
```

#### Contrats de Fonctions

**Fonction : Parser les maps**
```
Signature: struct s_segment *parse_proc_maps(int pid, size_t *count)

Préconditions:
  - pid > 0
  - pid est un processus valide (accessible via /proc)
  - count est non-NULL

Postconditions:
  - Retourne un tableau de s_segment alloué dynamiquement
  - count est rempli avec le nombre de segments
  - Les segments sont triés par start address croissante
  - Valeur NULL si erreur (ouvre/lecture échouée)

Erreurs:
  - Fichier /proc/[pid]/maps inexistant : NULL
  - Allocation mémoire échouée : NULL + message stderr
  - Permissions insuffisantes : NULL + message stderr

Exemple appel:
  size_t seg_count = 0;
  struct s_segment *segs = parse_proc_maps(1234, &seg_count);
  if (!segs) {
    perror("parse_proc_maps");
    return EXIT_FAILURE;
  }
  printf("%zu segments trouvés\n", seg_count);
  free(segs);
```

**Fonction : Filtrer les segments**
```
Signature: struct s_segment *filter_segments(
    const struct s_segment *segments,
    size_t count,
    const char *filter_perms,  // Ex: "rw-p" ou NULL (tous)
    size_t *filtered_count
)

Préconditions:
  - segments est non-NULL
  - count > 0
  - filter_perms est "rw-p", "r-xp", ou NULL (aucun filtre)
  - filtered_count est non-NULL

Postconditions:
  - Retourne tableau des segments correspondant aux critères
  - filtered_count rempli avec le nouveau compte
  - Mémoire allouée (caller responsable du free)

Logique:
  - Si filter_perms == NULL : retourner une copie complète
  - Sinon : ne garder que les segments avec permissions exactes
  - Ignorer toujours les segments --- (non lisibles)

Exemple:
  size_t filtered = 0;
  struct s_segment *rw_only = filter_segments(
      segments, seg_count, "rw-p", &filtered
  );
  printf("Segments RW: %zu\n", filtered);
  free(rw_only);
```

**Fonction : Valider un segment**
```
Signature: int is_segment_readable(const struct s_segment *seg)

Retour:
  - 1 si lisible (perms contient 'r')
  - 0 sinon

Usage:
  Filtrer au parsing même les segments non-lisibles (--x, ---)
  pour optimiser la suite.
```

---

### 1.2 Module Scanner (`scanner.c`)

**Rôle** : Lire la mémoire physique/virtuelle et appliquer le moteur de recherche.

#### Contrats de Fonctions

**Fonction : Ouvrir mémoire cible**
```
Signature: int open_target_memory(int pid)

Préconditions:
  - pid > 0
  - Processus accessible (permissions suffisantes)

Postconditions:
  - Retourne un descripteur de fichier valide (fd >= 3)
  - Valeur -1 si erreur

Erreurs:
  - /proc/[pid]/mem inexistant ou non accessible → -1 + stderr
  - Permissions insuffisantes → -1 + stderr

Détail:
  - Ouvre /proc/[pid]/mem en mode lecture seule (O_RDONLY)
  - Ce descripteur sera utilisé par les workers en lecture
  - À fermer avec close(fd) après scan

Exemple:
  int mem_fd = open_target_memory(1234);
  if (mem_fd == -1) {
    fprintf(stderr, "Impossible d'accéder à la mémoire de PID 1234\n");
    return;
  }
  // ... utiliser mem_fd pour lire
  close(mem_fd);
```

**Fonction : Lire segment mémoire**
```
Signature: ssize_t read_memory_segment(
    int mem_fd,
    unsigned long address,
    size_t size,
    unsigned char *buffer
)

Préconditions:
  - mem_fd >= 0 (descripteur valide)
  - address > 0
  - size > 0 et <= taille du buffer
  - buffer est alloué et accessible

Postconditions:
  - Retourne nombre d'octets lus (0 à size)
  - Retourne -1 en cas d'erreur
  - buffer rempli avec les octets de [address, address+size)

Détail:
  - Utilise lseek(mem_fd, address, SEEK_SET) puis read()
  - Si le read retourne < size : c'est normal (fin de segment)
  - Si le read retourne -1 (EFAULT) : segment non accessible

Exemple:
  unsigned char buf[4096];
  ssize_t bytes_read = read_memory_segment(mem_fd, 0x56556000, 4096, buf);
  if (bytes_read == -1) {
    perror("read_memory_segment");
    return;
  }
  printf("Lus %zd octets\n", bytes_read);
```

**Fonction : Chercher pattern (ASCII)**
```
Signature: int search_pattern_ascii(
    const unsigned char *buffer,
    size_t buffer_size,
    const char *pattern,
    size_t pattern_len,
    unsigned long base_address,
    struct s_match **matches,
    size_t *match_count
)

Préconditions:
  - buffer est non-NULL et de taille > 0
  - pattern est non-NULL et > 0
  - base_address = l'adresse absolue du début du buffer
  - matches et match_count non-NULL

Postconditions:
  - Retourne nombre de matchs trouvés (0 ou plus)
  - matches = tableau dynamique de s_match (alloué)
  - match_count rempli
  - Chaque match contient adresse absolue correcte

Détail:
  - Utiliser memmem() ou implémenter recherche brute
  - Calculer adresse absolue = base_address + offset dans buffer
  - Gérer les matchs qui chevauchent

Exemple:
  struct s_match *matches = NULL;
  size_t found = 0;
  search_pattern_ascii(
      buffer, 4096, "admin_token", 11, 0x56556000,
      &matches, &found
  );
  printf("Trouvé %zu occurrences\n", found);
  for (size_t i = 0; i < found; i++) {
    printf("  Match #%zu à 0x%lx\n", i, matches[i].address);
  }
  free(matches);
```

**Fonction : Chercher pattern (Hex)**
```
Signature: int search_pattern_hex(
    const unsigned char *buffer,
    size_t buffer_size,
    const unsigned char *hex_bytes,
    size_t hex_len,
    unsigned long base_address,
    struct s_match **matches,
    size_t *match_count
)

Préconditions:
  - buffer non-NULL
  - hex_bytes = octets bruts convertis (ex: "4D5A90" → {0x4D, 0x5A, 0x90})
  - hex_len = nombre d'octets à chercher
  - base_address = adresse absolue du buffer
  - matches et match_count non-NULL

Postconditions:
  - Même contrat que search_pattern_ascii
  - Cherche une suite d'octets exacte

Détail:
  - La conversion "4D5A90" → {0x4D, 0x5A, 0x90} se fait dans main.c
  - scanner.c reçoit directement les octets

Exemple:
  unsigned char mz_header[] = {0x4D, 0x5A, 0x90};  // "MZ\x90"
  struct s_match *matches = NULL;
  size_t found = 0;
  search_pattern_hex(
      buffer, 4096, mz_header, 3, 0x56556000,
      &matches, &found
  );
  printf("Signatures PE trouvées: %zu\n", found);
  free(matches);
```

**Fonction : Convertir hex string → octets**
```
Signature: int parse_hex_string(
    const char *hex_str,
    unsigned char **out_bytes,
    size_t *out_len
)

Préconditions:
  - hex_str est non-NULL et contient uniquement [0-9A-Fa-f]
  - Longueur de hex_str doit être paire (ex: "4D5A", pas "4D5")
  - out_bytes et out_len sont non-NULL

Postconditions:
  - out_bytes = tableau alloué dynamiquement
  - out_len = nombre d'octets (hex_str.len / 2)
  - Retourne 0 si succès, -1 si format invalide

Erreurs:
  - Caractère non-hex → -1 + message stderr
  - Longueur impaire → -1 + message stderr
  - Allocation échouée → -1 + message stderr

Exemple:
  unsigned char *bytes = NULL;
  size_t len = 0;
  if (parse_hex_string("4D5A90", &bytes, &len) == -1) {
    fprintf(stderr, "Format hex invalide\n");
    return;
  }
  printf("Convertis en %zu octets\n", len);
  // bytes = {0x4D, 0x5A, 0x90}
  free(bytes);
```

---

### 1.3 Module Thread Pool (`thread_pool.c`)

**Rôle** : Gérer création, synchronisation et coordination des workers.

#### Contrats de Fonctions

**Fonction : Initialiser la queue**
```
Signature: struct s_task_queue *init_task_queue(
    const struct s_segment *segments,
    size_t segment_count,
    const unsigned char *pattern,
    size_t pattern_len,
    int is_hex
)

Préconditions:
  - segments est non-NULL et count > 0
  - pattern est non-NULL
  - pattern_len > 0

Postconditions:
  - Retourne queue allouée et initialisée
  - Mutex et Condition Variable créés
  - Chaque segment = 1 tâche
  - Valeur NULL si erreur allocation

Détail:
  - La queue contient segment_count tâches
  - Chaque tâche = (segment, pattern, pattern_len, is_hex)
  - Mutex et Cond initialisés avec pthread_*_init()
  - Index courant = 0

Exemple:
  struct s_task_queue *queue = init_task_queue(
      segments, seg_count, (unsigned char*)"token", 5, 0
  );
  if (!queue) {
    fprintf(stderr, "Erreur allocation queue\n");
    return EXIT_FAILURE;
  }
```

**Fonction : Nettoyer la queue**
```
Signature: void cleanup_task_queue(struct s_task_queue *queue)

Préconditions:
  - queue est non-NULL ou NULL (safe)

Postconditions:
  - Détruit mutex et condition variable
  - Libère tableau de tâches
  - Libère la structure queue

Détail:
  - Appeler APRÈS tous les pthread_join()
  - Ne pas appeler depuis un worker
  - Safe d'appeler même si init a échoué (vérifier NULL)
```

**Fonction : Worker principal**
```
Signature: void *worker_thread(void *arg)

Préconditions:
  - arg = (struct s_task_queue *)
  - Queue initialisée et mutex valide

Postconditions:
  - Thread s'exécute jusqu'à épuisement des tâches
  - Threads s'endorment sur condition variable si queue vide
  - Retourne NULL (ou statut d'erreur)

Logique:
  while (true) {
    mutex_lock()
    if (tous les segments traités) {
      break
    }
    task = pop_next_task()
    mutex_unlock()
    
    if (task valide) {
      process_task(task)  // Sans lock, parallel-safe
    } else {
      cond_wait()         // Attendre signal ou timeout
    }
  }
  return NULL

Détail:
  - Chaque worker traite = Ouvrir mem_fd, lire buffer, chercher pattern
  - Les résultats de recherche doivent être thread-safe (lock si output partagé)
  - En fin de boucle, signaler autre threads via cond_broadcast()
```

**Fonction : Démarrer les workers**
```
Signature: int launch_workers(
    struct s_task_queue *queue,
    int num_threads,
    int mem_fd,
    pthread_t **thread_ids
)

Préconditions:
  - queue est initialisée
  - num_threads > 0 et <= nombre de segments
  - mem_fd >= 0
  - thread_ids est non-NULL

Postconditions:
  - Crée num_threads threads workers
  - thread_ids = tableau alloué avec les pthread_t IDs
  - Retourne 0 si succès, -1 si erreur

Détail:
  - Utiliser pthread_create() pour chaque worker
  - Passer queue et mem_fd en contexte
  - Capturer les ID pour pthread_join() plus tard
  - En cas d'erreur création : annuler les threads déjà créés (cancel)

Exemple:
  pthread_t *threads = NULL;
  if (launch_workers(queue, 4, mem_fd, &threads) == -1) {
    fprintf(stderr, "Erreur création workers\n");
    cleanup_task_queue(queue);
    return EXIT_FAILURE;
  }
  // ... attendre workers
  free(threads);
```

**Fonction : Attendre la fin des workers**
```
Signature: int wait_workers(
    pthread_t *thread_ids,
    int num_threads
)

Préconditions:
  - thread_ids est le tableau créé par launch_workers()
  - num_threads > 0

Postconditions:
  - Tous les threads sont terminés (pthread_join)
  - Retourne 0 si succès, -1 si un join a échoué

Détail:
  - Appeler pour chaque thread_ids[i]
  - Ne pas interrompre les workers sauf erreur critique
  - Si un worker échoue, continuer les autres puis rapporter

Exemple:
  if (wait_workers(threads, num_workers) == -1) {
    fprintf(stderr, "Un ou plusieurs workers ont échoué\n");
  }
```

**Fonction : Pop tâche de la queue (Thread-Safe)**
```
Signature: int pop_task(
    struct s_task_queue *queue,
    struct s_task *out_task
)

Préconditions:
  - queue est initialisée et mutex valide
  - out_task est non-NULL

Postconditions:
  - Si tâche disponible : copie dans out_task, retourne 1
  - Si aucune tâche : retourne 0

Détail:
  - DOIT être appelé avec queue->mutex déjà verrouillé
  - Incrémenter queue->index
  - Vérifier que queue->index < queue->size

Exemple (dans worker):
  pthread_mutex_lock(&queue->mutex);
  int has_task = pop_task(queue, &current_task);
  pthread_mutex_unlock(&queue->mutex);
  
  if (has_task) {
    process_task(current_task);
  }
```

---

### 1.4 Module Main & Argument Parsing (`main.c`)

**Rôle** : Entry point, parsing CLI, orchestration générale.

#### Contrats de Fonctions

**Fonction : Afficher usage**
```
Signature: void print_usage(const char *prog_name)

Postconditions:
  - Affiche sur stdout le mode d'emploi

Format attendu:
  Usage: shm_scanner --target <PID> --pattern <STRING|HEX> [OPTIONS]
  
  Options:
    --target <PID>    : PID du processus cible (obligatoire)
    --pattern <STR>   : Pattern à chercher (obligatoire)
    --threads <N>     : Nombre de threads (défaut: 1, max: 64)
    --hex             : Traiter pattern comme hexadécimal
    --filter <PERMS>  : Filtrer par permissions (ex: rw-p, r-xp)
    --help            : Afficher ce message
```

**Fonction : Parser arguments**
```
Signature: int parse_arguments(
    int argc,
    char **argv,
    int *out_pid,
    char **out_pattern,
    int *out_is_hex,
    int *out_num_threads,
    char **out_filter_perms
)

Préconditions:
  - argc > 0
  - argv est valide
  - Tous les out_* sont non-NULL

Postconditions:
  - Retourne 0 si succès, -1 si arguments invalides
  - out_pid rempli avec PID cible
  - out_pattern rempli (heap alloc)
  - out_is_hex = 1 si --hex présent
  - out_num_threads = nombre de threads (défaut 1)
  - out_filter_perms = filter ou NULL

Erreurs:
  - PID invalide (non numérique, <= 0) → -1 + message
  - Pattern absent → -1 + message
  - --threads non numérique → -1 + message
  - Threads hors limites (0 ou > 64) → -1 + message

Exemple:
  int pid = 0;
  char *pattern = NULL;
  int is_hex = 0;
  int num_threads = 1;
  char *filter = NULL;
  
  if (parse_arguments(argc, argv, &pid, &pattern, &is_hex, 
                      &num_threads, &filter) == -1) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
```

**Fonction : Main orchestration**
```
Logique générale:
  1. parse_arguments() → valider CLI
  2. parse_proc_maps(pid) → lister segments
  3. filter_segments() → appliquer --filter
  4. open_target_memory(pid) → obtenir mem_fd
  5. init_task_queue() → préparer tâches
  6. launch_workers(queue, num_threads) → démarrer scan
  7. wait_workers() → attendre fin
  8. print_results() → afficher matchs
  9. cleanup() → libérer toutes ressources

Gestion d'erreur:
  - À chaque étape, vérifier code retour
  - En cas d'erreur : libérer ressources acquises jusqu'à présent
  - exit(1) ou EXIT_FAILURE

Nettoyage final:
  - free(pattern)
  - free(filter_perms) si alloc
  - close(mem_fd)
  - free(segments)
  - free(filtered_segments)
  - cleanup_task_queue()
  - Afficher rapport final
```

---

## 2. Structures de Données Globales

```c
/* shm_scanner.h */

struct s_segment {
    unsigned long   start;
    unsigned long   end;
    char            perms[5];       // "rwxp" format
    char            name[256];      // Segment name
};

struct s_task {
    struct s_segment        segment;
    unsigned char           *pattern;
    size_t                  pattern_len;
    int                     is_hex;
};

struct s_match {
    unsigned long       address;
    int                 pid;
    char                segment_name[256];
};

struct s_task_queue {
    struct s_task           *tasks;
    size_t                  size;
    size_t                  index;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
};

struct s_context {
    int                 pid;
    char                *pattern;
    int                 is_hex;
    int                 num_threads;
    char                *filter_perms;
    struct s_match      *results;
    size_t              result_count;
    pthread_mutex_t     results_mutex;  // Pour accès thread-safe aux résultats
};
```

---

## 3. Dépendances & Headers Requis

```c
/* Core POSIX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Threads */
#include <pthread.h>

/* Système */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>      // Optional: pour dénomination threads
#include <signal.h>         // Optionnel

/* Standards C */
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
```

---

## 4. Contraintes Portabilité

Le code doit compiler **sans warning** sur :

| Plateforme | Compilateur | Flags |
|---|---|---|
| Linux x86_64 | GCC 9+ | `-Wall -Wextra -Werror -std=c99 -pthread` |
| macOS ARM64 | Clang 14+ | `-Wall -Wextra -Werror -std=c99 -pthread` |
| Windows 10 | MinGW + MSVC | `-Wall -Wextra -std=c99` (pthread via libpthread) |

**Note** : `/proc/[pid]/mem` est spécifique Linux. Sur d'autres OS, utiliser alternatives (ex: mach API macOS).

---

## 5. Métriques de Code Attendues

| Métrique | Cible |
|---|---|
| Cyclomatic Complexity | < 5 par fonction |
| Lignes par fonction | < 25 |
| Couverture de tâches | 100% (chaque segment étudié) |
| Pas de race condition | Validé via tools (ThreadSanitizer, Helgrind) |
| Pas de fuite mémoire | Valgrind clean |

---

**Fin des Spécifications Techniques**
