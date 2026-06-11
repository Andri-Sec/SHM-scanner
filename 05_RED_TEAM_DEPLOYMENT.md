# SHM-Scanner - Considérations Red Team & Déploiement

## 1. Furtivité Opérationnelle

### 1.1 Signature Comportementale

**Risques de détection** :

| Action | EDR/SIEM Visibility | Mitigation |
|---|---|---|
| Ouvrir `/proc/[pid]/maps` | Moniteur processus system calls | Acceptable, bruit normal |
| Ouvrir `/proc/[pid]/mem` | **ÉLEVÉ** si ptrace utilisé | Éviter ptrace, utiliser `/proc/mem` direct |
| Lire mémoire rapidement | Anomaly detection (throughput) | Throttle reads légèrement |
| Lancer N threads | Comportement CPU anormal | Garder threads <= CPU cores |
| Pattern hardcoded en clair | Static malware scan du binaire | Obfusquer strings (Phase 6) |

**Recommandation** :
```
Phase 1-5: Acceptable pour lab.
Phase 6 (V1.5): Obfuscation patterns obligatoire pour déploiement réel.
```

### 1.2 Empreinte Mémoire (Process)

```
Cible de déploiement: < 50 MB resident set size

Composition:
  - Thread stack (4 MB per thread, donc N threads = peu impactant)
  - Task queue (variable, taille segments × sizeof(s_task))
  - Buffer circulaire (4 KB max par worker, × N = peu)
  - Results array (croît avec matchs trouvés)

Gestion:
  - Allouer task queue une seule fois
  - Réutiliser buffers circulaires
  - Streamer résultats (ne pas tout garder en RAM)
```

### 1.3 Signature Temporelle

**Patterns anormaux** :
- Scanner ouvre 1000x `/proc/mem` en < 1 seconde
- CPU 100% pendant 30 secondes non stop
- Accès répétés à mêmes adresses mémoire

**Mitigation** :
```c
// Ajouter micro-pauses aléatoires (Phase 6)
if (worker_id % 2 == 0) {
  usleep(rand() % 1000);  // 0-1 ms random sleep
}
```

### 1.4 Permissions Requises

**Cas 1 : Même UID que la cible**
```
open(/proc/[target_pid]/mem) = SUCCÈS
Pas besoin CAP_SYS_PTRACE
Peu de bruit
```

**Cas 2 : UID différent (ex: scanner en tant que nobody, cible en tant que user)**
```
open(/proc/[target_pid]/mem) = EACCES
DOIT avoir CAP_SYS_PTRACE
Alternative: exécuter scanner en tant que cible UID
```

**Cas 3 : En tant que root**
```
Scanner peut lire n'importe quel process
MAIS: Exécuter root binary = TRÈS visible
Préférable: escalader juste assez pour le UID cible
```

---

## 2. Limitations Intentionnelles (Security by Design)

### 2.1 Pas d'Allocation de Buffer Massif

**Mauvais design** (risque crash sur cible) :
```c
// INCORRECT:
unsigned char *full_memory = malloc(process_vsize);
read(mem_fd, full_memory, process_vsize);
// Si process utilise 8 GB, allocate 8 GB!
// Système crash ou scanner crash
```

**Design SHM-Scanner (correct)** :
```c
// CORRECT:
unsigned char buffer[4096];  // Buffer fixe
for (off in segments) {
  read_memory_segment(mem_fd, off, 4096, buffer);
  search_pattern(buffer, 4096, ...);
}
// Mémoire constante, pas de ballonnement
```

### 2.2 Pas de Write en Mémoire Cible

**SHM-Scanner READ-ONLY** :
- Ouvre `/proc/[pid]/mem` en mode O_RDONLY
- Aucun write possible
- Pas d'injection de shellcode, dropper, beacon
- Analyse passive uniquement

**Impact opérationnel** :
```
Scanner utilisé pour: RECONNAISSANCE + EXTRACTION D'ARTEFACTS
Non utilisé pour: INJECTION, MODIFICATION, PERSISTENCE
```

### 2.3 Pas de Synchronisation Réseau

**SHM-Scanner local-only** :
- Aucun appel réseau (pas de DNS, HTTP, C2)
- Résultats stockés localement ou exfiltrés manuellement
- Pas de signature réseau

---

## 3. Gestion des Erreurs & Recovery

### 3.1 Processus Cible Terminé Pendant Scan

```
Scénario: Process cible crash ou killed pendant scan

Comportement attendu:
  1. open(/proc/[target]/mem) échoue ou le fd become invalid
  2. read() retourne 0 (EOF) ou -1 (EACCES/EBADF)
  3. Worker doit gérer gracefully : log warning, continue

Implémentation:
  if (read(...) == -1 && errno == EBADF) {
    fprintf(stderr, "Processus cible probablement terminé\n");
    // Continuer avec autres segments (cache)
    // Ou arrêter complètement
  }
```

### 3.2 Seg Fault / Écriture mémoire cible

```
Scénario: Target memory devient inaccessible PENDANT scan

Signaux possibles:
  - SIGBUS (bus error, memory alignment)
  - SIGSEGV (via memcpy lecture indirecte)

Mitigation:
  - NE PAS utiliser strncpy/memcpy sur buffer lu
  - Utiliser memmem(buffer, size, pattern, plen) safe
  - Les syscalls read() gèrent EFAULT correctement
```

### 3.3 Deadlock Potentiel

```
Scénario: Mutex verrouillé, worker attend indéfiniment

Prévention:
  1. Pas d'acquisition imbriquée du même mutex
  2. Éviter lock pendant process_task (potential blocking)
  3. Timeout sur pthread_cond_wait (optional mais sûr)

Exemple safe:
  pthread_cond_timedwait(&queue->cond, &queue->mutex, timeout);
  // Workers ne "hanged" jamais > timeout
```

---

## 4. Escalade d'Attaque Typique

### Scénario: Exfiltration de Tokens depuis Chrome

```
Contexte: Chrome process stocke session tokens en mémoire

Étapes:
  1. Identifier PID du processus Chrome: pgrep chrome
  2. Lancer scanner:
     ./shm_scanner --target [chrome_pid] --pattern "Bearer_" --threads 4
  3. Attendre quelques secondes
  4. Parser les adresses retournées
  5. Utiliser un outil hex dump pour extraire les tokens
  6. Exfiltrer tokens hors-bande (USB, mail, etc.)

Signature opérationnelle:
  - Zéro trace disque (scanner + output volatil)
  - Peu de bruit réseau
  - Succès : tokens in-memory recovered pour utilisation ultérieure

Timing: ~1-2 minutes pour scan complet
```

### Scénario: Chercher les Secrets SSH

```
Contexte: sshd process stocke clés privées en mémoire

Pattern à chercher: "-----BEGIN OPENSSH PRIVATE KEY-----"

./shm_scanner --target [sshd_pid] \
              --pattern "-----BEGIN OPENSSH" --threads 2

Résultats: Adresses où les clés SSH résident
Extraction: Dumper ces zones avec objdump ou gdb (post-exploitation)
```

---

## 5. Anti-Forensics & Log Cleanup

### 5.1 Traces Réseau

```
SHM-Scanner ne génère AUCUNE trace réseau → zéro nettoyage réseau
```

### 5.2 Traces Processus (auditd/auditctl)

```
Auditd peut logger:
  - Appels système (open, read, etc.)
  - Changes to /proc
  
Cleanup:
  - Avant: auditctl -l (voir les règles)
  - Pendant: SHM-Scanner ne peut pas "étouffer" auditd
  - Après: Éditer /var/log/audit/audit.log (nécessite root)

Mitigation: Scanner exécuté AVANT que auditd soit activé
```

### 5.3 Traces Shell (Bash History)

```
Bash stocke commands dans ~/.bash_history

Cleanup:
  - set +o history  # Avant lancer scanner
  - history -c      # Après (clear)
  - unset HISTFILE  # Optionnel

Ou: rm ~/.bash_history
```

### 5.4 Traces Mémoire

```
SHM-Scanner résultats stockés en mémoire du scanner

Cleanup:
  - Processus scanner TERMINÉ = mémoire libérée par OS
  - Aucun dump core (ulimit -c 0)
  - Aucun log disque (--output /dev/null ou résultat volatile)
```

---

## 6. Détection & Contremesures Défensives

### 6.1 Signatures de Détection

**EDR va chercher** :
```
1. Process ouvre /proc/[pid]/maps beaucoup
2. Process ouvre /proc/[pid]/mem
3. Process avec threads = comportement anormal
4. Lecture mémoire d'autres process sans ptrace
```

**Contremesures pour V1.5** :
```c
- Obfuscation patterns (XOR)
- Minimal logs (stderr muet avec redirections)
- Sleep aléatoires
- Exécution rapide (< 30 secondes)
```

### 6.2 Détection par Pattern

```
EDR regex pour scanner = "SHM-Scanner" ou "shm_scanner" string?

Mitigation: Renamer binaire
  mv shm_scanner memory_analyzer  # Nom générique

Pattern strings:
  "struct s_task" → Renommer vers "s_job"
  Pas d'utilité, juste renommer publiquement
```

---

## 7. Limitations de la V1

### Accepté pour V1:
- [x] Analyse passive sans injection
- [x] Pas de obfuscation patterns (lab only)
- [x] Pas de throttling (lab only)
- [x] Pas de C2 feedback (résultats locaux)

### Non-implémenté pour V1:

| Fonctionnalité | Raison | Impact |
|---|---|---|
| Obfuscation XOR | Complexity, Phase 6 | Détectable statiquement |
| Throttling CPU | Nice-to-have | Peut générer anomaly alert |
| Stealth beacons | Hors-scope | N/A (outil local) |
| Encrypted comms | N/A | Outil autonome |
| Sandbox evasion | TLPI V2 future | Pas besoin V1 |

---

## 8. Déploiement en Production (Red Team)

### Pré-déploiement Checklist

- [ ] **Compilation statique** (optionnel) : `-static` si possible
  ```bash
  gcc -static -Wall -Wextra -Werror -std=c99 -pthread ...
  # Resultat: binaire standalone, pas de dépendances lib
  ```

- [ ] **Binary renaming** : Renommer en quelque chose de benign
  ```bash
  cp shm_scanner /tmp/analyze_proc
  ```

- [ ] **Pattern obfuscation** : XOR patterns avant déploiement (Phase 6)

- [ ] **Isolation réseau** : Vérifier scan ne contacte pas C2

- [ ] **Timing** : Lancer scan pendant fenêtre "busy" (camoufle CPU spike)

- [ ] **Clean exit** : Vérifier scanner se termine proprement
  ```bash
  timeout 60 ./shm_scanner --target [pid] --pattern "x"
  echo $?  # Should be 0 (succès) ou 124 (timeout)
  ```

### Post-déploiement

```
Après exécution scanner:

1. Récupérer résultats (stdout capture ou file)
2. Nettoyer history shell (set +o history)
3. Terminer scanner (déjà fini)
4. Audit audit.log si possible (vérifier traces)
5. Cover tracks avec dead drop ou USB
```

---

## 9. Amélioration Future (V2 Roadmap)

### V1.5 (Court terme)
- [ ] Obfuscation patterns XOR
- [ ] Throttling CPU configurable
- [ ] Output JSON structuré
- [ ] Support multiples patterns en une passe

### V2 (Long terme - TLPI Sandboxing)
- [ ] ptrace sandboxing (TLPI chapitre spécifique)
- [ ] Seccomp filtering
- [ ] Namespaces isolation
- [ ] Capability-based access control

### V3+ (Vision)
- [ ] Kernel module version (kmem scanning)
- [ ] VM introspection (libvmi)
- [ ] Snapshot mémoire chiffré hors-chaîne

---

## 10. Ressources & Références

### Documentation Système
- `/proc` filesystem : `man 5 proc`
- ptrace alternatives : Éviter, utiliser `/proc/[pid]/mem`
- POSIX threads : `man 7 pthreads`

### Outils de Validation
- **Valgrind** : Leak detection, profiling
- **ThreadSanitizer** : Race condition detection
- **GDB** : Debugging, memory inspection
- **strace** : System call tracing (pour forensics)

### Références Red Team
- "Adversary Emulation" : MITRE ATT&CK (T1055 Process Injection, T1120 Peripheral Device Discovery)
- "Memory Dumping" : SANS Pen Testing, Memory Forensics
- OWASP : Memory dump analysis

---

## 11. Considérations Légales & Éthiques

**Avertissement Critique** :

```
SHM-Scanner est un outil HAUTEMENT INVASIF.
Utilisation STRICTEMENT limitée à:
  1. Environnement de test contrôlé (lab)
  2. Engagement de pen-testing AUTORISÉ
  3. Post-exploitation en contexte opérationnel légitime

Utilisation INTERDITE sur:
  - Systèmes sans autorisation écrite
  - Données personnelles (PII, tokens, credentials réels)
  - Systèmes de production sans contrat RED TEAM
  
Responsabilité: Utilisateur SEUL responsable des implications légales.
```

---

**Fin de la Documentation Red Team**
