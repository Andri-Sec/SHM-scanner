# SHM-Scanner - Norme 42 & Checklist Évaluation Stricte

## 1. Norme 42 : Regles Obligatoires

### 1.1 Langage & Compilation

- [ ] **Langage C pur** : C99 ou C11 uniquement
  - Pas de C++ ou extensions GCC spécifiques
  - Compiler avec `-std=c99` ou `-std=c11`

- [ ] **Compilation sans warning** 
  ```bash
  gcc -Wall -Wextra -Werror -std=c99 -pedantic \
      -Wwrite-strings -Wshadow -pthread
  ```
  - Zéro warning à la sortie du compilateur
  - Si une ligne genere un warning : elle est incorrecte

- [ ] **Format du code**
  - Indentation : TAB (représentation 4 espacements)
  - Pas d'espaces pour indenter
  - Longueur ligne : MAX 80 caractères (strict)
  - Pas de trailing whitespace

### 1.2 Fonctions & Complexité

- [ ] **Taille de fonction** : MAX 25 lignes
  - Compter les lignes logiques (pas les braces seules)
  - Brace ouvrante et fermante sur 2 lignes = 2 lignes
  - Lignes vides = comptées

- [ ] **Variables par fonction** : MAX 4
  - Inclut paramètres de fonction
  - Variables déclarées dans la fonction
  - Les boucles imbriquées ne comptent pas (voir exception)
  - Les structures typedef ne comptent pas (1 var = 1 struct)

**Exception** : Une boucle imbriquée (for dans for) peut avoir une variable de boucle supplémentaire.

- [ ] **Déclarations** : Obligatoires en début de bloc
  - Toutes les variables déclarées AVANT tout code
  - Respecter l'ordre ANSI-C : déclarations → code

### 1.3 Variables Globales

- [ ] **Pas de variables globales** (sauf exceptions justifiées)
  
  **Cas accepté (avec documentation)** :
  ```c
  /* Exception Justifiée : Contexte opérationnel en lecture seule */
  static struct s_context g_ctx;  // Accessible partout, sync via mutex
  ```
  
  - Documenter CHAQUE global en commentaire
  - Préférer les passer en paramètre
  - En multi-threaded : utiliser uniquement si thread-safe (mutex)

### 1.4 Norminette Automatisée

**Utiliser la norminette 42 officielle** pour valider :
```bash
norminette -R CheckForbiddenSourceHeader srcs/ includes/
norminette -R CheckForbiddenSourceHeader Makefile
```

Erreurs graves (reject automatique) :
- `ONLY_FILENAMES` : nom de variable/fonction interdit
- `GLOBAL_VARIABLE` : global non justifié
- `FUNCTION_TOO_LONG` : > 25 lignes
- `TOO_MANY_LOCALS` : > 4 variables
- `BAD_INDENTATION` : mauvaise indentation
- `TRAILING_SPACE` : espace/tab en fin de ligne
- `LINE_TOO_LONG` : > 80 caractères

---

## 2. Contraintes de Code Additionnelles

### 2.1 Gestion Mémoire

- [ ] **Zéro fuite mémoire**
  - Valgrind report : `ERROR SUMMARY: 0 errors`
  ```bash
  valgrind --leak-check=full --track-origins=yes \
           --show-leak-kinds=all ./shm_scanner ...
  ```

- [ ] **Chaque malloc a son free**
  - Tracer chaque allocation en commentaire
  - Documenter le propriétaire du pointeur (qui free ?)

- [ ] **Pas de buffer overflow**
  - Vérifier tailles de buffer avant strcpy/memcpy
  - Utiliser strncpy si nécessaire

### 2.2 Gestion des Erreurs

- [ ] **Vérifier code retour de TOUS les appels système**

| Appel | Erreur | Action |
|---|---|---|
| `open()` | -1 | Afficher errno + exit/return |
| `read()` | -1 ou 0 | Vérifier contexte, fermer descripteur |
| `write()` | -1 ou < attendu | Gérer EINTR, EAGAIN |
| `malloc()` | NULL | Message stderr + exit(1) |
| `free()` | — | Vérifier ptr != NULL avant |
| `pthread_create()` | != 0 | Annuler threads créés + free |
| `pthread_join()` | != 0 | Considérer échoué, log + continue |
| `pthread_mutex_lock()` | != 0 | Deadlock potentiel, exit |
| `close()` | -1 | Log warning, mais continuer |

### 2.3 Sécurité & Robustesse

- [ ] **Validation des entrées**
  - PID : vérifier > 0 et <= INT_MAX
  - Pattern : vérifier len > 0 et < MAX_PATTERN_LEN
  - Threads : vérifier 1 <= threads <= 64

- [ ] **Pas d'accès hors limites**
  - Tableaux : vérifier index < taille avant accès
  - Structures : padding/alignment correct
  - Pointeurs : ne jamais déréférencer NULL

- [ ] **Pas de comportement indéfini**
  - Débordement integer : utiliser unsigned quand approprié
  - Shift operations : vérifier shift < bit width
  - Division : vérifier diviseur != 0

### 2.4 Synchronisation (Multi-threading)

- [ ] **Pas de race condition**
  - Mutex protège accès partagés
  - Condition variable correctement signalée
  - Pas d'accès à queue sans lock

- [ ] **Pas de deadlock**
  - Ordre d'acquisition des locks cohérent
  - Pas d'acquisition imbriquée du même mutex
  - Timeout sur pthread_cond_wait() recommandé

- [ ] **Cleanup correct**
  - pthread_mutex_destroy() après utilisation
  - pthread_cond_destroy() après utilisation
  - Pas d'accès aux locks après destroy

---

## 3. Checklist Fonctionnelle (Unit Tests)

### 3.1 Parser `/proc/[pid]/maps`

- [ ] **Cas nominal** : Parser le fichier maps d'un processus valide
  ```
  Input:  /proc/[self]/maps
  Output: Array de segments correctement parsé
  Vérifier: start, end, perms, name corrects
  ```

- [ ] **Erreur fichier absent** : PID invalide
  ```
  Input:  PID = 99999 (inexistant)
  Expected: Retour NULL + message stderr
  ```

- [ ] **Erreur permissions** : PID sans permissions de lecture
  ```
  Input:  /proc/[root]/maps (en tant que user normal)
  Expected: NULL + EPERM message
  ```

- [ ] **Tri des segments** : Segments triés par start address
  ```
  Vérifier: segments[i].start < segments[i+1].start
  ```

### 3.2 Filtrage de Segments

- [ ] **Filter NULL** (tous les segments)
  ```
  Input:  segments[], filter = NULL
  Output: Copie complète
  ```

- [ ] **Filter "rw-p"** (tas, heap, stack)
  ```
  Input:  segments[], filter = "rw-p"
  Output: Uniquement segments rw-p
  ```

- [ ] **Filter "r-xp"** (code exécutable)
  ```
  Input:  segments[], filter = "r-xp"
  Output: Uniquement segments r-xp
  ```

- [ ] **Filter impossible** (aucun match)
  ```
  Input:  segments[], filter = "---"
  Output: Tableau vide ou count = 0
  ```

### 3.3 Lecture Mémoire

- [ ] **Ouvrir /proc/self/mem** avec succès
  ```
  Expected: fd >= 3 (valide)
  ```

- [ ] **Lire segment valide** (ex: stack)
  ```
  Input:  mem_fd, address valide, size=4096
  Output: bytes_read > 0
  ```

- [ ] **Gérer EFAULT** (adresse invalide)
  ```
  Input:  mem_fd, address invalide
  Output: -1 ou bytes_read = 0
  ```

### 3.4 Recherche Pattern

- [ ] **Pattern ASCII simple**
  ```
  Buffer: "admin_token_secret"
  Pattern: "token"
  Expected: 1 match à offset 6
  Address = base + 6
  ```

- [ ] **Pattern ASCII multiple**
  ```
  Buffer: "foo foo foo"
  Pattern: "foo"
  Expected: 3 matches à offsets 0, 4, 8
  ```

- [ ] **Pattern Hex**
  ```
  Buffer: {0x4D, 0x5A, 0x90, 0x00}  // MZ\x90\x00
  Pattern: "4D5A90"
  Expected: 1 match à offset 0
  ```

- [ ] **Pattern absent**
  ```
  Buffer: "foobar"
  Pattern: "xyz"
  Expected: 0 matches, count = 0
  ```

- [ ] **Pattern qui chevauche**
  ```
  Buffer: "aaaa"
  Pattern: "aa"
  Expected: 3 matches (offsets 0, 1, 2)
  ```

### 3.5 Multi-threading

- [ ] **1 thread** : Pas de crash, segment traité
  ```
  Input:  segments (1), threads=1
  Output: Exécution séquentielle OK
  ```

- [ ] **N threads** : Tous les segments traités
  ```
  Input:  segments (100), threads=4
  Expected: Tous les 100 segments scannés (pas de double, pas de skip)
  ```

- [ ] **Synchronisation** : Pas de race condition
  ```
  Vérifier avec ThreadSanitizer (clang -fsanitize=thread)
  ```

- [ ] **Pas de deadlock** : Timeout test
  ```
  Lancer scan, vérifier qu'il termine dans timeout raisonnable (5s)
  ```

### 3.6 Argument Parsing

- [ ] **Mode d'emploi absent**
  ```
  Command: ./shm_scanner
  Expected: Affiche usage + exit(1)
  ```

- [ ] **Arguments obligatoires**
  ```
  Missing --target: exit(1) + message
  Missing --pattern: exit(1) + message
  ```

- [ ] **PID invalide**
  ```
  --target abc: exit(1) + message
  --target -5: exit(1) + message
  --target 0: exit(1) + message
  ```

- [ ] **Threads invalide**
  ```
  --threads 0: exit(1) + message
  --threads 65: exit(1) + message (max 64)
  --threads abc: exit(1) + message
  ```

- [ ] **Mode Hex**
  ```
  --hex --pattern "4D5A": Reconnaître comme hex
  --pattern "4D5A" (sans --hex): Traiter comme ASCII
  ```

- [ ] **Filter perms**
  ```
  --filter rw-p: Valider perms
  --filter invalid: Rejeter ou ignorer
  ```

---

## 4. Checklist de Performance & Robustesse

### 4.1 Performance

- [ ] **Throughput** : >= 100 MB/s sur machine standard
  ```bash
  time ./shm_scanner --target [PID] --pattern "pattern" --threads 4
  # Mesurer durée totale pour taille mémoire scannée
  ```

- [ ] **Mémoire scanner** : <= 50 MB
  ```bash
  ps aux | grep shm_scanner
  # Vérifier RSS (resident set size)
  ```

- [ ] **CPU** : <= 60% monocore pendant scan
  ```bash
  top -p [shm_scanner_pid]
  ```

### 4.2 Robustesse

- [ ] **Plusieurs cibles** : Scanner différents PIDs sans crash
  ```
  ./shm_scanner --target $$
  ./shm_scanner --target 1
  ./shm_scanner --target [autre_process]
  ```

- [ ] **Patterns longs** : Supporter patterns > 256 bytes
  ```
  --pattern "a"*500
  Expected: Fonctionner ou message d'erreur explicite
  ```

- [ ] **Segments petits** : Gérer segments < 4 KB
  ```
  Buffer circulaire doit gérer cas limite
  ```

- [ ] **Pas de crash sur process malveillant**
  ```
  Lancer scanner sur fork bomb / processus bloqueant
  Expected: Timeout ou recovery graceful
  ```

---

## 5. Checklist Compilation & Build

### 5.1 Makefile

- [ ] **Cible all** : Compile sans erreur
  ```bash
  make clean && make all
  # Result: shm_scanner executable
  ```

- [ ] **Cible clean** : Supprime objets et executable
  ```bash
  make clean
  # Vérifier: Pas de .o, pas de shm_scanner
  ```

- [ ] **Cible re** : Recompile complètement
  ```bash
  make re
  # Équivalent à make clean && make all
  ```

- [ ] **Flags de compilation**
  ```bash
  -Wall -Wextra -Werror -std=c99 -pthread
  Optional: -O2, -g (debug)
  ```

- [ ] **Dépendances** : Recompile si .h changé
  ```bash
  touch includes/shm_scanner.h
  make
  # Doit recompiler tous les .c
  ```

### 5.2 Portabilité

- [ ] **Linux x86_64** : Compile + exécute OK
  ```bash
  gcc -Wall -Wextra -Werror -std=c99 -pthread ...
  ```

- [ ] **macOS ARM64** : Compile (optionnel, accepter échec `/proc`)
  ```bash
  clang -Wall -Wextra -Werror -std=c99 -pthread ...
  # Note: /proc/[pid]/mem n'existe pas, implémenter fallback
  ```

- [ ] **Windows MinGW** : Compile (optionnel)
  ```bash
  i686-w64-mingw32-gcc -Wall -Wextra -Werror -std=c99 -pthread ...
  ```

---

## 6. Checklist Documentation

- [ ] **README.md**
  - Usage clair avec exemples
  - Compilation instructions
  - Limitations connues
  - Architecture overview

- [ ] **Commentaires de code**
  - Pourquoi (design intent), pas quoi (évident)
  - Sections complexes documentées
  - Pas de commentaire sur `int x = 5;`

- [ ] **Makefile commenté**
  - Variables expliquées
  - Targets documentées

---

## 7. Critères d'Évaluation (ACCEPT vs REJECT)

### ACCEPT (100 pts) si:
- [x] Norminette clean
- [x] Compilation sans warning -Wall -Wextra -Werror
- [x] Zéro fuite mémoire (valgrind)
- [x] Tous les tests fonctionnels passent
- [x] Performance >= 100 MB/s
- [x] Code lisible, architecure claire
- [x] README + documentation adéquate

### REJECT (0 pts) si:
- [ ] Norminette error
- [ ] Compilation avec warning
- [ ] Fuite mémoire detectée
- [ ] Crash ou hang sur entrée valide
- [ ] Race condition (ThreadSanitizer)
- [ ] Deadlock détecté
- [ ] Buffer overflow
- [ ] Pas de Makefile ou Makefile cassé
- [ ] Code incompréhensible ou mal architecturé

---

## 8. Feedback Rejection (Template)

Si le code est rejeté, le feedback suit ce format :

```
REJECT - [Category] : [One-line hint]

Category: COMPILATION | NORME42 | LEAK | RACE_CONDITION | SEGFAULT | LOGIC

Exemple:
  REJECT - NORME42 : Fonction main() dépasse 25 lignes. 
  Référence: Chapitre 3.2 du cahier des charges.
```

**Pas de full code review sur rejection**. Juste direction générale.

---

**Fin de la Checklist d'Évaluation**
