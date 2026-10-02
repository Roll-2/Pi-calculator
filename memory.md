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

- [x] STEP 13 — Performance committato (7f0f229): sample 3 s + ps: processo vsync-bound (SDL_CondWait su swap), CPU 7–14% di un core su 12, molto sotto il target di 2 core. Nessun collo di bottiglia: nessuna modifica al codice.
- [x] STEP 14 — Documentazione: README aggiornato in EN+IT con sezioni "pi_calc — the unified calculator" (CLI, barriera RAM) e "Pi-visual — the π visualizer" (pannelli, tasti, --pi, limiti), tabelle e struttura progetto aggiornate. .gitignore già completo dai tempi di STEP 1 (pi_calc, Pi-visual, pi_*.txt, visual/*.o, pictures/, !Makefile). memory.md mantenuto passo per passo.

- [x] STEP 15 — Verifica finale: pi_calc default OK (314161 byte, 3.14159..., verifica OK, 0,28 s); -N 1e9 rifiutato con messaggio RAM; make clean/all OK; Pi-visual vivo e stabile ~270 s continui attraversando entrambi i dolly-in (t≈89 s, t≈120 s) e la stabilizzazione, 12–19% CPU, 0,8% MEM, log vuoto, swap invariato; output di test intermedi rimossi; git log ordinato per STEP. Nota: tasti S/R/F/Esc verificati per codice e la pipeline S end-to-end col test GL offscreen; la verifica interattiva dei tasti richiede l'utente alla finestra.

- [x] FIX GRAFICA (richiesta utente con 3 foto di riferimento, stile MathVisualProofs) — il chord vecchio era sbagliato: corde RETTE bianche spessissime converge a ventaglio sui vertici, anello tagliato dai bordi, niente cifre; pendolo invisibile. Riscritto visual/chord.h: corde CURVE (Bézier quadratica con controllo verso il centro, 32 segmenti), ancoraggi DISTRIBUITI lungo l'arco di ogni settore (48/cifra, niente ventagli), anello centrato (CHORD_WORLD=1.25, cifre a r=1.13 incluse nel frustum), cifre grandi SDL_ttf attorno all'anello con trasformazione mondo→pixel. Bug reale trovato col nuovo check framebuffer (PI_VISUAL_CHECK=1): i DUE glClear per-viewport si cancellavano a vicenda (glClear ignora il viewport) → pendolo zero pixel da sempre; fix = un solo clear per frame. MSAA 4x. Verifica oggettiva a t=20 s: anello centrato (centro ±1 px, raggi h/v 586/586 = 0.998), 287 tinte nel disco (corde colorate), pendolo lit=20.3% del viewport sinistro, cifre grandi 3148 px magenta con PI_VISUAL_LABEL_COLOR=1. Soak 45 s: 13.4% CPU, log vuoto. Nota tecnica: il check che legge il framebuffer va fatto DOPO il pass HUD (il clear cancella l'HUD del frame precedente) — la prima misurazione delle etichette era falsata proprio da questo.

## Riepilogo finale

Sessione completata: 16 commit (STEP 0-15). pi_calc.cpp unificato con barriera RAM dura (nessun override) e verifica integrata; Pi-visual completo di pendolo con coreografia 14π/226π, chord streaming con fade, rate condivisa tarata (100M cifre in ~120 s), HUD, 4 palette, screenshot/reset/fullscreen. README e memory aggiornati. fractals.cpp/Hydrogen3D.cpp mai esistiti nel repo: nessun riferimento toccato; pi300k.cpp, pi100m.cpp, pi300k.txt, pi100m.txt intatti (mtime e contenuti originali).

## Prossimi passi

- Estensioni opzionali proposte (da NON implementare senza richiesta): audio sonificato dal pendolo, esportazione PNG del chord diagram, rate configurabile da CLI.

## Decisioni (aggiunta STEP 8)

- (STEP 8) Coreografia in coordinate θ convertite a tempo con rate_inverse(digits): i dolly-in restano agganciati a 14π e 226π qualunque sia la lunghezza del file. t(14π)≈89 s, t(226π)≈120 s con i parametri attuali.
- (STEP 8) Pendolo e chord derivano dallo stesso progresso: θ_target = THETA_MAX·(cifre consumate/cifre totali), con cifre consumate = stream.base_index. K_θ implicito: THETA_MAX/digits_totali ≈ 7.1e-6 rad/cifra.

## Note operative per l'agente

- Lo smoke test dell'app grafica va fatto DA SOLO: `(./Pi-visual >/tmp/pivis.log 2>&1 & echo $! > /tmp/pivis.pid); sleep 4; kill $(cat /tmp/pivis.pid)` — combinandolo con make nello stesso comando l'harness va in hang (già successo allo STEP 5).
- I file di riferimento fractals.cpp e Hydrogen3D.cpp NON esistono (vedi Problemi aperti): i "riusa da..." del brief si intendono come "implementa con tecniche standard".

## Rilavorazione grafica (richiesta utente 19/09, commit `53bdc3d` + seguente)

Richieste: corde curve morbide non rette, cifre leggibili, centratura, pendolo visibile. Poi seconda passata: partenza lenta del chord, slow-motion sui passaggi ravvicinati del pendolo, cambio colore a ogni quasi-contatto, saturazione al posto della luminosità.

- **Corde Bézier quadratiche** (32 segmenti, spessore ~2 px, alpha ~0.35) con ancoraggi distribuiti su 48 punti lungo l'arco di ogni settore: niente più ventaglio convergente. Cifre grandi (52 px SDL_ttf) colorate attorno all'anello, mondo→pixel via ortho del chord.
- **Bug critico trovato**: ogni viewport faceva `glClear`, ma `glClear` ignora il viewport → il clear del pannello destro cancellava ogni frame il pendolo appena disegnato. Un solo clear per frame.
- **Diagnostica oggettiva**: `PI_VISUAL_CHECK=1` legge il framebuffer a t=20 s e misura (centratura, tinte, pendolo, screenshot in pictures/). Trovò il bug del clear; le false letture iniziali erano sonde che leggevano *prima* del pass HUD. `PI_VISUAL_LABEL_COLOR=1` disegna le cifre in magenta puro per contarle (3148 px).
- **Rate a due stadi**: rampa lineare 60 cifre/s fino a 100k cifre (t≈1665 s), poi saturazione esponenziale verso 9M/s. Ordine di grandezza del corretto (100M @ ~2 min) mantenuto via fattore SCALE nel blocco `#if 1` — il `#if 0` accanto conserva i numeri originali (1665→~2 min), da NON togliere: è la referenza per i test lunghi.
- **Coreografia pendolo in spazio-θ** (niente keyframe-tempo): θ̇ = R·slow(θ) con finestre gaussiane di slow-motion (0.10×) centrare sui passaggi ravvicinati 14π (gap 22/7) e 226π (gap 355/113); camera zoom: 1→14→1→190→1 centrata su (2,0), dove nasce il pendolo. Disallineamento tempo↔θ della versione precedente eliminato.
- **Cambio colore per svolta**: colore per riga calcolato dall'indice della svolta (θ/π): viola→giallo→verde→ciano→magenta→arancio (14 colori ciclici), via attributo per-vertice `a_row`; le righe dal passaggio in poi ereditano il nuovo colore, così durante lo slow-motion si vede la linea cambiare tinta dal punto di quasi-contatto.
- **Saturazione invece di luminosità**: nessun fade verso il bianco; alpha cresce con la velocità (0.18→0.80) e la saturazione HSL va da 0.35 a 1.0: le prime corde sono grigie pastello, le ultime full colore (ragnatela accesa). 100% saturazione quando la matrice delle transizioni è satura.
- **Budget di disegno**: a rate satura il push generava 150k corde/frame (98% CPU); tetto 400 corde/frame (consumate comunque tutte: HUD onesto, rolling window si rinnova) → CPU max 23%.
- **camera.h trimmato** (−178 righe): solo `mat4_ortho`, `mat4_transform_point`, `keyframe_eval` (ancora usato per lo zoom), v3 rimasti di servizio; lo stack 3D (mul/inverse/lookAt/perspective) era morto.

## Prossimi passi (rilavorazione grafica)

- Verificare a schermo con l'utente le nuove velocità iniziali (rampa 60 cifre/s) e i due slow-motion cinematografici: se il primo slow a 14π tarda troppo col file lungo (t≈1665·fattore), abbassare soglia rampa 100k→30k cifre.

## Terza passata (feedback utente: corde quasi invisibili, zoom bloccato, slow-motion mai vista)

Causa scoperta: il workflow interrotto aveva lasciato su disco SOLO PARTE del lavoro — la "partenza lenta" descritta sopra non era mai stata scritta (restava la vecchia esponenziale RATE_A=8/K=0.115) e la coreografia era ancora la vecchia strisciata 1→14→1→190→1 sparsa su 188 rad. Tre i difetti percepiti:

- **Corde "trasparenti/assenti"**: la ragnatela full-file disegnata fantomatica dall'avvio + fade di 6 s → il diagramma sembrava già pieno ma spento. RISOLTO con la ragnatela CRESCENTE: `chord_web` globale parte VUOTA, alimentata da `ChordStream::push` (update + flag web_dirty), layer statico ricontestualizzato ogni CHORD_REBUILD_EVERY=512 corde; fade 6→30 s; alpha streaming 0.55→0.75; statico 0.10/0.24→0.30/0.55. Rimossa l'infrastruttura a_sat (StreamVertex 8→7 float, attributo location 3 eliminato): la saturazione costante al 100% è ciò che l'utente voleva raggiungere subito dopo la partenza.
- **Zoom "nel posto e momento sbagliati"**: la vecchia curva teneva zoom 14 per ~90 s con schermo quasi vuoto (i veri contatti a (2,0) sono solo 14π e 226π). RISOLTO: dolly-in = EVENTI LOCALI con bump smoothstep su ±DOLLY_W=3π attorno ai contatti: zoom = 1 + 13·b(14π) + 189·b(226π); fuori dagli eventi zoom 1.00 (misurato a 0π, 100π, 250π). Centro sempre verso (2,0) (k=zoom/14).
- **Slow motion mai vista**: prima era distribuita/assorbita nella strisciata; ora è dentro lo stesso bump degli zoom: θ̇ = R·slow(θ), SLOW_R=0.12, misurato min 0.12 esattamente dentro i due eventi (probe /tmp/coreo_probe: 14π a 12,9 s, 226π a 161 s, fine curva 161 s con SCALE=0.1).
- **rate_of a due stadi**: rampa LINEARE RATE_RAMP=60 cifre/s fino a RATE_RAMP_END=100k, poi esponenziale continua da lì (RATE_A=2, K=0.115, max 9e6). SCALE=0.1 in `#if 1` (tempi reali ~28 min di rampa; `#if 0` per la run reale). Il clock t della scena è moltiplicato per SCALE; HUD mostra rate_of(t)/SCALE (cifre/s reali).
- **Pulizia**: chord_data (classificatore pre-accumulato) e max_transition era morti → rimossi; ChordData::reset→clear (ragnatela riparte vuota con R); th_0 e keyframe_eval non più usati.
- **Verifica live** (PI_VISUAL_CHECK, t-scena 20 s = 200 s reali): centratura 0.998, 533 tinte nel disco (prima 26), pendolo θ=19.1π con 7485 punti, CPU 17-18%, log pulito.

## Prossimi passi (terza passata)

- L'utente deve valutare a schermo: velocità iniziale (600 cifre/s in test, 60 reali), i due eventi di dolly+slow a 14π (t≈129 s) e 226π (t≈1610 s) in test, la ragnatela crescente.
- Per la run reale (rampa 28 min): mettere `#if 0` sul blocco SCALE.

## Quarta passata (feedback utente: colore per quasi-contatto, close-up rapido e mirato, corde flebili)

Richieste: (1) colore che cambia al passaggio ravvicinato e non a ogni periodo; (2) zoom rapido e mirato sul punto di quasi-scontro, senza perdere mai il pendolo, con slow-motion a ogni close-up, almeno i primi 3; (3) corde che diventano più flebili (non più bianche), ricche all'inizio; (4) file 314.159 cifre per finire prima; (5) efficienza.

- **Contatti come fonte unica** (`visual/pendulum.h`): k = {7, 113, 33102} → θ_c = 14π, 226π, 66204π. La separazione visibile è il **miss** = distanza MINIMA curva↔(2,0) = δ*² con δ* = −φ/(1+π), φ = Im Z(θ_c): 1.8014e-4 / 2.0914e-9 / 8.4222e-10 (concorda col minimo numerico nel self-test). Zoom_i = 0.72/miss → 4,0e3 / 3,44e8 / 8,55e8. La "punta del ricciolo" a zoom alto è invisibile: le due linee quasi-parallele (curva di ritorno vs ramo nascita) sono la caratteristica risolvibile.
- **Vertici come offset da (2,0)**: float32 con valori ~2 non reggono miss ~1e-9; con offset sì. Snap ESATTO dei campioni sia su θ_c (punta ricciolo) sia su δ* (incrocio y=0 / miss).
- **Coreografia**: rate per segmento PEND_R = {0.9, 14, 2400, 40} rad/s, finestre pend_window = 1.61·R, slow 0.10 al centro di ogni evento, ponti smoothstep, zoom log-interpolato, camera con offset pend_cam_cx (centro offset da (2,0), vincolo −2 al zoom 1) e verticale fissa a 0; passo adattivo pend_point_step (base 0.008, raffinato 0.0202·√d con floor = miss).
- **Slow GLOBALE**: `slow = pend_slow(theta_cur)` moltiplica il dt della scena → pendolo E chord rallentano insieme a ogni close-up. `t` = tempo di scena, `t_raw` orologio grezzo con clamp.
- **Chord senza additivo**: blending alpha standard (niente più schiacciamento verso il bianco); alpha streaming decrescente per indice chord_stream_alpha(k) = 0.85·(1+k/400)^−0.30 con floor 0.08; il render disegna SOLO le corde con birth > t−FADE (2-3 corde invece di 768k vertici → grosso risparmio GPU); MSAA 4x→2x.
- **Rate cifre**: RATE_SLOW = 0.4 cifre/s (2,5 s/cifra) fino a 80 s (~32 cifre), poi esponenziale RATE_K2=0.203 → 100 cifre/s a ~500 cifre (t≈114 s), cap 9e6. File default `pi_314159.txt`; cifre esaurite a t≈153 s. Close-up a t≈52/120/244 s, durata 10 s l'uno (indipendenti: il pendolo integra rate(θ), non rate_of(t)).
- **Ritocco su richiesta**: l'utente ha trovato l'inizio del chord troppo lento (era RATE_SLOW=0.1 → 10 s/cifra, cifra 8 a t=87 s): alzato a 0.4 → cifra 8 a 20 s, cifra 500 a 114 s, file a 153 s; "delicato ma non rallento". Vincolo di test t8 aggiornato (60..100 → 10..40), tutti gli altri invariati e OK.
- **PendulumBirth**: VBO statico θ∈[0,0.06] sempre disegnato (l'altra metà del quasi-contatto; il buffer circolare lo espellerebbe dopo ~1048 rad).
- **Fix screenshot**: screenshot_save usa il GL_VIEWPORT corrente → i PNG dei check partivano dal viewport del pannello destro (1352×1466 invece di 2704×1466); fix = glViewport(0,0,w,h) prima di ogni blocco selfcheck.
- **Fix geometria close-up 1** (verifica numerica sui PNG con decodificatore zlib Python in /tmp/rowscan.py): al 1° contatto il salto di θ per frame (0.9·slow·dt ≈ 1.4e-3 rad) era MINORE del passo adattivo al cross (4.8e-3 rad) → la testa disegnata si fermava ESATTAMENTE sul centro e la seconda linea mancava nella metà superiore (dove il ramo nascita la farebbe risaltare). Fix = `pend_shot_th(i) = cross_th + 1.1·step(cross_th)`: lo scatto aspetta che la testa superi l'incrocio di un passo, così il segmento finale attraversa tutta l'altezza. Risultato misurato: due linee a x=1091 e x=1311 (220 px) su TUTTA l'altezza in tutti e 3 i close-up; sotto il centro solo la curva (il ramo nascita nasce a (2,0) e va in alto) — corretto.
- **Self-check a 3 fasi**: fase A t>20 s (centratura 0.998, pendolo OK), fase B base_index≥400 (351 tinte > 15), close-up i quando θ > pend_shot_th(i) e zoom > 0.9·zoom_i (log con θ, zoom, testa animata e testa DISEGNATA vs miss). Reset aggiornato (t, t_raw_prev).
- **Verifiche**: make pulito (0 warning); chord_test e camera_test OK; run PI_VISUAL_CHECK 265 s verde (351 tinte, close-up 1/2/3 scattati, nessun processo residuo, log senza errori).
- Fix minore: typo `// ...---- if (reset_requested)` su stessa riga (cascata di errori) separato; duplicazione della voce PI_VISUAL_CHECK nel README (IT) rimossa.

## Prossimi passi (quarta passata)

- Controllo visivo dell'utente sui tre close-up e sulla ragnatela; se la separazione delle due linee o i tempi non convince, si ritocca pend_shot_th/zoom_i/coreografia.

## Riassetto repository (richiesta utente, dopo chiusura grafica)

- **Rimossi** i sorgenti dei due programmi storici `pi300k.cpp` e `pi100m.cpp` (`git rm`) e i relativi eseguibili `pi300k`/`pi100m`: ora che esiste `pi_calc.cpp` a N variabile non servono più. **Conservati** i file di cifre: `pi300k.txt` (300.016 byte, tracciato) e `pi100m.txt` (100.000.002 byte, git-ignorato) restano su disco.
- `.gitignore`: rimossa la sezione "Eseguibili" con i pattern `pi300k`/`pi100m` morti; invariate le regole sui txt (`pi100m.txt` resta ignorato, `pi_*.txt` idem) e `!Makefile`.
- **README riscritto** (EN+IT): rimosse le sezioni `pi300k.cpp` e `pi100m.cpp` (tabella caratteristiche, prestazioni, limiti, record); "The algorithm" e "Viewing the digit files" ricollocate come sezioni top-level valide per `pi_calc`; intro, sezione `pi_calc`, bullet del chord (file di default), project layout e note aggiornati; intro del visualizzatore IT allineata all'EN (slow globale, ~4 minuti).
- Commenti allineati: testa di `pi_calc.cpp` (architettura da record senza riferimenti ai file rimossi), `pi_reader.h`; default del self-test CHORD_TEST → `pi_314159.txt`.
- Verifiche post-rimozione: grep senza riferimenti orfani, `make clean && make` pulito, chord_test e camera_test OK.
