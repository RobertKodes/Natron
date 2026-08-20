# GOAL — Panou de chat AI nativ în Natron (fork C++)

> **Instrucțiuni pentru agentul care implementează.** Citește tot fișierul înainte de a scrie prima linie de cod. Este autonom: nu depinde de nicio conversație anterioară.

---

## 0. Misiunea, într-o frază

Adaugă în Natron un **panou dockabil nativ „AI Assistant"**, în care utilizatorul discută cu un agent AI care poate construi și modifica efectiv graful de noduri — **fără ca utilizatorul să introducă vreo cheie de API**.

## 1. Principiile care guvernează tot design-ul

Nu le încălca. Dacă o cerință de mai jos pare să le contrazică, oprește-te și întreabă.

1. **Autentificarea rămâne în CLI.** Natron nu se conectează la niciun LLM. Pornește CLI-ul de agent instalat local (`claude`, ulterior `codex`) ca **proces copil** și vorbește cu el pe stdin/stdout. CLI-ul are deja OAuth-ul de abonament al utilizatorului în propriul store. **Natron nu citește, nu stochează și nu loghează niciodată credențiale de furnizor.**
2. **Direcția de control se inversează.** Agentul nu e „în" Natron. Natron rulează un **server MCP** și îi dă agentului mâini: `node_create`, `node_connect`, `param_set` etc. Panoul de chat e doar fața vizibilă a acestei bucle.
3. **Aprobările se dau în Natron, nu în CLI.** Pentru orice operație distructivă, serverul MCP blochează și ridică un dialog Qt nativ, cu numele reale ale nodurilor. Nu depindem de modelul de permisiuni al niciunui CLI.
4. **Un turn de agent = un singur Ctrl+Z.** Ăsta e motivul pentru care facem fork C++ în loc de plugin Python. Dacă undo-ul grupat nu iese, fork-ul și-a pierdut rațiunea de a fi.

## 2. De ce fork C++ și nu plugin Python

Natron expune `PyPanel` (`Gui/PythonPanels.h:121`), deci un panou de chat s-ar putea scrie și în PySide, fără recompilare. S-a ales conștient fork-ul C++ pentru: **undo grupat**, integrare reală în layout și meniuri, streaming performant, și acces direct la comenzile existente de undo. Nu propune revenirea la varianta Python.

---

## 3. Fapte verificate despre codebase — folosește-le, nu le redescoperi

Toate au fost verificate în repo la commit-ul `3763d80`. Confirmă-le rapid dacă vrei, dar nu le presupune greșite.

| Fapt | Locație |
|---|---|
| Șablonul exact pentru panoul nou: cum se instanțiază și se înregistrează un panou | `GuiPrivate::createScriptEditorGui()` — `Gui/GuiPrivate.cpp:421` |
| Semnătura de înregistrare | `void Gui::registerTab(PanelWidget* tab, ScriptObject* obj)` — `Gui/Gui.h:242` |
| Clasa de bază a panourilor; are `pushUndoCommand()` virtual și `getUndoStack()` protected virtual | `Gui/PanelWidget.h:43` |
| Model de panou de imitat (`QWidget` + `PanelWidget`) | `Gui/ScriptEditor.h:46`, `Gui/ScriptEditor.cpp` |
| Accesor de imitat | `Gui::getScriptEditor()` — `Gui/Gui40.cpp:551` |
| Comenzi de undo existente pentru noduri — **refolosește-le, nu reimplementa** | `AddMultipleNodesCommand`, `RemoveMultipleNodesCommand`, `ConnectCommand`, `InsertNodeCommand` — `Gui/NodeGraphUndoRedo.h:83,113,144,174` |
| Stiva de undo a grafului | `NodeGraph::getUndoStack()` — `Gui/NodeGraph.h:278`; `Gui::registerNewUndoStack()` — `Gui/Gui.h:162` |
| Pattern existent de proces copil + IPC (QProcess + QLocalServer + QLocalSocket) — **studiază-l înainte să scrii backend-ul** | `Engine/ProcessHandler.h:83`, `Engine/ExistenceCheckThread.cpp` |
| API-ul Python al aplicației, ca referință pentru ce trebuie expus prin MCP | `Engine/PyAppInstance.h:284-323`, `Engine/PyNode.h:241-332` |
| **`Gui/Gui.pro` listează fiecare fișier manual** (`SOURCES` și `HEADERS`, alfabetic) | `Gui/Gui.pro:196,334` |
| **`Gui/CMakeLists.txt` face `file(GLOB *.cpp)`** → fișierele noi sunt luate automat | `Gui/CMakeLists.txt:22` |

**Consecință practică:** la fiecare fișier nou, editezi doar `Gui/Gui.pro`. CMake se descurcă singur. Dar **ambele build-uri trebuie să rămână valide** — e o greșeală clasică listată în `skills/natron-maintainer/SKILL.md`.

## 4. Reguli de cod obligatorii (din `skills/natron-maintainer/SKILL.md`)

- `#include <Python.h>` **primul** în fiecare TU (PYTHON BLOCK).
- Tot codul între `NATRON_NAMESPACE_ENTER` / `NATRON_NAMESPACE_EXIT`.
- `QT_NO_CAST_FROM_ASCII` e activ → orice literal: `QString::fromUtf8("…")` sau `tr("…")`.
- PIMPL: `AIChatPanelPrivate` etc., headerul public ține doar `_imp`.
- **`Engine` nu include niciodată un header din `Gui`.** Tot codul de aici stă în `Gui/`.
- Header de licență GPL identic cu al celorlalte fișiere din `Gui/`.
- Tipuri noi → adaugă-le în `Gui/GuiFwd.h`.
- Stil: `astyle -p -H -f -j -z2 -c -k3 -U -A8 -n <fișier>` înainte de commit.

---

## 5. Planul de execuție

Lucrează în ordinea asta. **Commit după fiecare fază.** Nu trece mai departe cu o fază neterminată.

### Faza 0 — recon (fără cod)

Produ un raport scurt, cu `fișier:linie` pentru fiecare afirmație:

1. Cum își construiește `ScriptEditor` UI-ul și cum se leagă de `PanelWidget`.
2. Cum se serializează un panou în layout-ul proiectului (ca să se redeschidă unde a fost lăsat).
3. **Cum grupez N mutații într-un singur undo.** Verifică dacă `QUndoStack::beginMacro/endMacro` e calea corectă aici sau dacă Natron împinge comenzile într-un fel care cere altă soluție. Ăsta e punctul critic — dacă răspunsul e neclar, nu-l trece cu vederea.
4. Cum se creează un nod din C++ (`CreateNodeArgs`) și cum se setează valoarea unui knob.

**Checkpoint:** dacă punctul 3 n-are răspuns concret, oprește-te și raportează înainte de Faza 1.

### Faza 1 — `Gui/AIMcpServer.h/.cpp`

Server MCP peste JSON-RPC 2.0, transport HTTP pe loopback (`QTcpServer`).

- Port efemer ales de sistem; **bind strict pe `127.0.0.1`**; token bearer aleator generat la pornire; orice request fără tokenul corect → respins.
- Metode: `initialize`, `tools/list`, `tools/call`.
- Tools v1: `natron_status`, `graph_list_nodes`, `node_create(pluginID, label?, x?, y?)`, `node_connect(node, inputIndex, source)`, `node_delete(node)`, `param_get(node, param)`, `param_set(node, param, value, dimension?)`.
- **Toate mutațiile se execută pe thread-ul GUI** (`Qt::BlockingQueuedConnection`) — cererile vin de pe thread de rețea.
- Erori structurate, niciodată excepții peste JSON-RPC. Coduri: `NO_PROJECT`, `NODE_NOT_FOUND`, `PARAM_NOT_FOUND`, `PARAM_TYPE_MISMATCH`, `CONNECT_INVALID`, `PLUGIN_NOT_FOUND`. Fiecare eroare are `code`, `message` și un `hint` acționabil.
- PIMPL. Adaugă fișierele în `Gui/Gui.pro`.

### Faza 2 — undo grupat (faza cea mai importantă)

- `beginAgentTransaction(const QString& label)` / `endAgentTransaction()` în `AIMcpServer`.
- Tot ce se întâmplă între ele → **o singură comandă pe stiva de undo**, cu textul `"AI: <label>"` (ex. `"AI: adaugă denoise"`).
- Refolosește comenzile din `Gui/NodeGraphUndoRedo.h`. Nu reimplementa crearea/ștergerea/conectarea de noduri.
- **Sigur la eșec:** dacă un tool crapă la mijloc, tranzacția se închide corect și stiva rămâne consistentă. Zero macro-uri lăsate deschise, în orice cale de execuție.

**Checkpoint:** explică în scris ce se întâmplă dacă `node_create` reușește și `node_connect` eșuează imediat după. Dacă nu poți răspunde precis, design-ul e greșit.

### Faza 3 — `Gui/AIAgentBackend.h/.cpp`

- Bază abstractă `AIAgentBackend : QObject` — `start(cwd, mcpUrl, token)`, `send(text)`, `interrupt()`, `stop()`; semnale `textChunk(QString)`, `toolCall(QString name, QString argsJson)`, `toolResult(QString name, bool ok)`, `errorOccurred(QString)`, `finished()`.
- `ClaudeCodeBackend`: pornește binarul `claude` în mod non-interactiv cu streaming JSON, scrie un config MCP temporar care indică spre `mcpUrl` cu tokenul bearer, parsează stdout incremental (JSONL).

> **Nu presupune numele flag-urilor CLI-ului.** Rulează `claude --help` și verifică ce suportă versiunea instalată efectiv pe mașina asta, apoi folosește exact acele flag-uri. Verifică și că modul non-interactiv folosește autentificarea de abonament și **nu** cere `ANTHROPIC_API_KEY` — dacă cere, oprește-te și raportează: e o presupunere fundamentală a proiectului. Dacă `claude` nu e instalat, oprește-te și spune-mi.

- Configul temporar conține tokenul nostru MCP → permisiuni restrictive, șters la `stop()`, niciodată în folderul proiectului.
- Parser tolerant: tipurile de evenimente necunoscute se ignoră în loc să crape.
- Citire non-blocantă a stdout-ului; UI-ul nu îngheață niciodată.
- Oprire curată: `interrupt()` → `terminate()` → `kill()` cu timeout. **Zero procese orfane**, inclusiv la închiderea aplicației.

### Faza 4 — `Gui/AIChatPanel.h/.cpp`

Modelat după `Gui/ScriptEditor.h/.cpp`.

- `class AIChatPanel : public QWidget, public PanelWidget`, PIMPL, constructor `AIChatPanel(Gui* gui)`.
- UI: transcript cu bule de conversație; input multi-linie (Enter trimite, Shift+Enter linie nouă); buton Stop; indicator de stare al backend-ului.
- `toolCall` se randează compact: `⚙ node_create(Grade) ✓`.
- **Dialog nativ de confirmare** înainte ca serverul MCP să execute orice operație distructivă (`node_delete`, salvare peste fișier existent, render lung).
- Suprascrie `getUndoStack()` și `pushUndoCommand()` din `PanelWidget`.
- Serializare de layout: panoul se redeschide unde a fost lăsat (vezi Faza 0, punctul 2).
- Injectează automat în fiecare mesaj contextul vizibil: nodurile selectate, viewer-ul activ, frame-ul curent. Ăsta e avantajul pe care un CLI din terminal nu-l are — utilizatorul poate spune „mai cald aici".
- Etichetă vizibilă: *powered by <backend> — conversația și conținutul proiectului pleacă la furnizor*.
- Stări de onboarding: **CLI găsit și autentificat** → chat gata; **găsit, neautentificat** → „rulează `claude` o dată în terminal" + buton de recheck; **negăsit** → instrucțiuni de instalare + selector manual de cale.

### Faza 5 — înregistrare și integrare

1. Membru `AIChatPanel* _aiChatPanel` în `Gui/GuiPrivate.h`, lângă `_scriptEditor`.
2. `GuiPrivate::createAIChatPanelGui()`, copiat 1:1 după `createScriptEditorGui()` (`Gui/GuiPrivate.cpp:421`): `setScriptName("aiChatPanel")`, `setLabel(tr("AI Assistant"))`, `setVisible(false)`, `registerTab(...)`.
3. Apeleaz-o de unde e apelată `createScriptEditorGui()`.
4. `Gui::getAIChatPanel()` lângă `Gui::getScriptEditor()` (`Gui/Gui40.cpp:551`).
5. Intrare de meniu care afișează panoul.
6. Toate fișierele noi în `Gui/Gui.pro` (`SOURCES` + `HEADERS`); forward-decls în `Gui/GuiFwd.h`.

### Faza 6 — build

```bash
cmake -S . -B build -DNATRON_QT6=OFF
cmake --build build -j
```

Repară erorile una câte una. Dacă lipsesc dependințe de sistem (Qt, Boost, OpenFX, PySide/Shiboken — vezi `INSTALL_WINDOWS.md`), **spune-mi ce să instalez; nu ocoli problema comentând cod.**

Verifică apoi și că `qmake Project.pro` rămâne valid (fișierele sunt în `Gui.pro`).

---

## 6. Criterii de acceptare

- [ ] Panoul „AI Assistant" apare în meniu, se andochează oriunde, se salvează în layout și se redeschide acolo.
- [ ] Utilizatorul scrie „adaugă un denoise pe plate" și graful se modifică live, fără să fi introdus vreo cheie.
- [ ] **Un singur Ctrl+Z anulează tot ce a făcut agentul într-un turn.**
- [ ] Operațiile distructive cer confirmare printr-un dialog Qt nativ, cu nume reale de noduri.
- [ ] Butonul Stop oprește agentul instantaneu, în orice moment.
- [ ] Zero credențiale scrise vreodată pe disc de Natron.
- [ ] Serverul MCP e legat exclusiv pe loopback și refuză cererile fără token.
- [ ] Zero procese copil orfane după închiderea proiectului sau a aplicației.
- [ ] Build-ul CMake trece; `Gui.pro` e actualizat; niciun `#include` din `Gui` în `Engine`.

## 7. Ce să NU faci

- Nu comenta cod ca să treacă build-ul. E cel mai frecvent mod în care un build „reușește" fals.
- Nu inventa semnături de API. Dacă nu ești sigur, **citește headerul și citează linia**.
- Nu reimplementa crearea/conectarea de noduri — comenzile de undo există deja.
- Nu stoca chei de API și nu adăuga un câmp de „API key" în preferințe. Dacă ajungi să simți nevoia, ai luat-o pe drumul greșit — vezi principiul 1.
- Nu adăuga telemetrie.
- Nu face refactor de cod fără legătură cu misiunea asta.
- Nu sări peste Faza 0. Fazele 1 și 2 sunt fundația; UI-ul e partea ușoară. Dacă începi cu UI-ul, îl rescrii.

## 8. Când te blochezi

Oprește-te și raportează, cu opțiuni și o recomandare — nu improviza — dacă: modul non-interactiv al CLI-ului cere o cheie de API; undo-ul grupat nu se poate face curat cu infrastructura existentă; sau build-ul cere dependințe pe care nu le pot instala eu.
