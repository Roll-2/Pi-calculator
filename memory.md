# memory.md — diario di sessione

Log cronologico del lavoro sul progetto π (calcolatore unificato + visualizzatore).
Aggiornato a ogni STEP, non a fine sessione.

## Environment

- Data sessione: 2026-09-19
- `uname -a`: Darwin Mac.Home 25.6.0 Darwin Kernel Version 25.6.0: Fri Jul 31 19:17:26 PDT 2026; root:xnu-12377.161.14~5/RELEASE_ARM64_T6041 arm64
- `uname -m`: arm64
- `sw_vers`: macOS 26.6.2 (Build 25G83)
- `sysctl -n hw.memsize`: 25769803776 byte = 24,0 GB
- `sysctl -n hw.ncpu`: 12
- `clang++ --version`: Apple clang 21.0.0 (clang-2100.3.34.2), target arm64-apple-darwin25.6.0
- `make --version`: GNU Make 3.81
- `brew list --versions`: boost 1.90.0_1, gmp 6.3.0, mpfr 4.2.2, sdl2 2.32.10, sdl2_ttf 2.24.0 — **tutte presenti, nessuna installazione necessaria**
- `pkg-config --modversion sdl2`: 2.32.10
- File dati: `pi100m.txt` = 100.000.002 byte ("3." + 100M cifre, nessun newline finale); `pi300k.txt` = 300.016 byte; entrambi iniziano con `3.14159265358979323846264338327950288419716939937510582097494459230781640628620899862803482534211706`
- git: branch `main`, 4 commit preesistenti (messaggi in italiano), working tree pulito (solo `.freebuff/` non tracciato, non toccare)
- `.gitignore` esiste già e ignora già: `*.o`, `.DS_Store`, `build/`, `*.dSYM/`, binari `pi300k`/`pi100m`, `pi100m.txt`. **Attenzione: ignora anche `Makefile`** — servirà un'eccezione `!Makefile` per committare il Makefile del visualizzatore.

## Decisioni

- (STEP 1) CLI con parsing manuale (no getopt: messaggi d'errore più chiari e controllo opzioni sconosciute).
- (STEP 1) Controllo RAM: `system_ram_gb()` con sysctl `hw.memsize`; se fallisce si esce con errore (il controllo è obbligatorio, non si prosegue alla cieca). Costanti: 53 B/cifra, cap 20 GB, margine SO 4 GB. Nessun flag di override.
- (STEP 1) Verifica prime 1000 cifre: riusato il prefisso noto di pi100m.cpp; se fallisce exit 1 e NESSUN file scritto (scrittura solo dopo verifica OK).
- (STEP 1) `.gitignore`: aggiunte regole `pi_calc`, `Pi-visual`, `pi_*.txt`, `visual/*.o`, `pictures/` ed eccezione `!Makefile` (il .gitignore esistente ignorava `Makefile` in modo generico — senza l'eccezione il Makefile del visualizzatore non sarebbe committabile). pi300k.txt/pi100m.txt non matchano `pi_*.txt` e restano come sono.
- (STEP 1) Test 345M cifre: il gate RAM lo ammette (18,3 GB stimati < 20) ma il calcolo reale (~20-25 min stimati) va oltre i timeout dell'agent; interrotto senza danni (swap invariato, nessun processo residuo). Il percorso di rifiuto è comunque verificato con 1B e 500M. Funzionalità a 10M verificata end-to-end.

## STEP completati

- [x] STEP 0 — Ispezione ambiente: eseguiti uname/sw_vers/sysctl/clang++/make/brew/pkg-config/ls/wc/head/tail. Nessun pacchetto mancante. Dati registrati qui sopra.
- [x] STEP 1 — pi_calc.cpp unificato e committato (8b6dbe5). Compilazione clang++ OK (warning solo da gmpxx.h di sistema). Test: -N 1000 --no-verify OK (1002 byte); -h OK; -N 1e9 e -N 5e8 rifiutati con exit 1 e messaggio completo; default 314159 cifre OK in 0,28 s (pi_314159.txt = 314161 byte, inizia con 3.14159..., nessun newline finale); -N 345000000 ammesso dal gate ma interrotto per timeout agent (non un difetto del programma); -N 10000000 OK in 24,7 s con verifica OK (10000002 byte).
- [x] STEP 2 — Scaffolding committato (6245b41): Pi-visual.cpp scheletro (SDL2 + GL 3.3 core, 1600×900, due viewport neri, Esc), visual/pi_reader.h placeholder, Makefile (all/run/clean). Compilazione make OK. Nota tecnica: su Homebrew SDL2 gli header stanno in include/SDL2/, serve -I/opt/homebrew/include/SDL2; SDL_opengl.h superfluo, basta OpenGL/gl3.h. L'eccezione !Makefile nel .gitignore funziona (Makefile committato).
- [x] STEP 3 — Modulo lettura π committato (1558787): pi_reader.h header-only con mmap PROT_READ/MAP_PRIVATE, digit(i)=data[2+i]-'0', verifica prime 100 cifre all'open. Self-test temporaneo OK su pi100m.txt (100.000.000 cifre), pi300k.txt, pi_10000000.txt; test rimosso.
- [x] STEP 4 — Camera committata (26be569): visual/camera.h (mat4 column-major, inverse, perspective, lookAt, keyframe smoothstep con zoom geometrico). Unit test -DCAMERA_TEST 4/4 OK. Verifica visiva: triangolo rotante con camera a keyframe nel viewport sinistro; smoke test: app viva, no errori GL.
- [x] STEP 5 — Chord statico committato (7c7c1e0): visual/chord.h (transizioni 10×10, settori con gap, corde-quad larghezza ~sqrt conteggio, blending additivo), CLI --pi <file>, mat4_ortho. Self-test su 1M cifre: ratio freq max/min 1.0081 (nessun settore dominante), 90/100 transizioni presenti. Smoke test OK.
- [x] STEP 6 — Chord streaming committato (50e80d0): ChordStream (rolling window 5000 corde, fade quadratico 8 s, upload orphaning), shader flat con u_alpha, rate(t) esponenziale provisionale (A=184, k=0.09, max 9e6) — integrale su [0,120] ≈ 100M, da verificare a fine STEP 9. Un incidente di str_replace ha corrotto chord.h: risolto riscrivendo il file per intero. Smoke test 6 s OK.

## Problemi aperti

- Il brief descrive `fractals.cpp` e `Hydrogen3D.cpp` come file di riferimento presenti, ma **non esistono nel repository** (verificato con ls e git ls-files). Impatto: i "riusa da Hydrogen3D/fractals" (mat4/lookAt/perspective, cache testo SDL_ttf, screenshot zlib) vanno implementati da zero con tecniche standard. Nessun blocco funzionale: le specifiche sono complete. Procedo; se l'utente li vuole ripristinare, possono essere aggiunti a valle senza toccare il codice nuovo.

- [x] STEP 9 — Clock e rate tarati (c02a5b4): probe numerica temporanea con integrazione discreta dt=1/60 e clamp/cap identici al loop: file da 100M cifre esaurito a t=120,00 s — bersaglio centrato, nessun parametro modificato.

- [x] STEP 10 — HUD committato: visual/hud.h (SDL_ttf, cache texture con potatura, font di sistema Helvetica.ttc, due corpi). Contatori (cifre/tempo/rate/fps) a 10 Hz in alto a destra, titolo a sinistra, legenda 10 cifre in basso. Compilazione pulita, smoke test OK.

- [x] STEP 11 — Palette pastello committate: 4 schemi ×10 colori (provvisoria + Botanica/Alba/Notturna), CHORD_CURRENT_PALETTE a runtime con ricostruzione mesh, tasti 0-3, default Botanica. Legenda sincronizzata. Compilazione pulita, smoke test OK.

- [x] STEP 12 — QoL committato: visual/screenshot.h (glReadPixels→flip→Paeth→zlib→PNG con CRC32), tasti S (screenshot in pictures/, fallback $HOME), R (reset clock+stream), F (fullscreen). Writer PNG verificato con test GL offscreen (file: PNG image data 100x80 RGB). Smoke test app OK.

## Prossimi passi

- STEP 13: profilazione FPS/CPU, target 60 fps stabili, ottimizzare il chord streaming se necessario.

## Decisioni (aggiunta STEP 8)

- (STEP 8) Coreografia in coordinate θ convertite a tempo con rate_inverse(digits): i dolly-in restano agganciati a 14π e 226π qualunque sia la lunghezza del file. t(14π)≈89 s, t(226π)≈120 s con i parametri attuali.
- (STEP 8) Pendolo e chord derivano dallo stesso progresso: θ_target = THETA_MAX·(cifre consumate/cifre totali), con cifre consumate = stream.base_index. K_θ implicito: THETA_MAX/digits_totali ≈ 7.1e-6 rad/cifra.

## Note operative per l'agente

- Lo smoke test dell'app grafica va fatto DA SOLO: `(./Pi-visual >/tmp/pivis.log 2>&1 & echo $! > /tmp/pivis.pid); sleep 4; kill $(cat /tmp/pivis.pid)` — combinandolo con make nello stesso comando l'harness va in hang (già successo allo STEP 5).
- I file di riferimento fractals.cpp e Hydrogen3D.cpp NON esistono (vedi Problemi aperti): i "riusa da..." del brief si intendono come "implementa con tecniche standard".
