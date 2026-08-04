# Análise Técnica Completa — Open PS2 Loader (OPL)

> **Base analisada:** `ps2homebrew/Open-PS2-Loader`, commit `3e3f34e9` (06/06/2026), versão `v1.2.0-Beta`.
> **Escopo:** engenharia reversa documental do código-fonte. **Nenhum arquivo do OPL foi modificado.**
> Todas as referências `arquivo.c:linha` apontam para a árvore do OPL upstream.

---

## 1. Estrutura completa do projeto

### 1.1 Diretórios de primeiro nível

| Diretório | Tamanho | Conteúdo | Executa em |
|---|---|---|---|
| `src/` | 840 KB | Aplicação principal (GUI, temas, suporte a dispositivos, launcher) | **EE** (MIPS R5900, 32 MB RAM) |
| `include/` | 192 KB | 39 headers públicos do EE | EE |
| `modules/` | 2,3 MB | Drivers IRX (USB, HDD, SMB, MX4SIO, mcemu, cdvdman, pademu…) | **IOP** (MIPS R3000A, 2 MB RAM) |
| `ee_core/` | 408 KB | Núcleo residente que sobrevive ao `ExecPS2` e hospeda o jogo | EE (residente em jogo) |
| `gfx/` | 580 KB | 89 PNGs embutidos no ELF (ícones, fundos, overlays) | — |
| `audio/` | 256 KB | 8 efeitos sonoros em formato ADPCM (`.adp`) | — |
| `lng/`, `lng_tmpl/` | 32 KB | 31 traduções + template | — |
| `thirdparty/` | 24 KB | Fonte `PoeVeticaNew.ttf` embutida | — |
| `pc/` | 152 KB | Ferramentas de PC: `iso2opl`, `opl2iso`, `genvmc`, `ziso.py` | PC (host) |
| `misc/` | 20 KB | Tema padrão `conf_theme_OPL.cfg`, ícones de Memory Card | — |
| `labs/`, `notes/` | 212 KB | Experimentos e documentação de xparam | — |

**Total: ~86.000 linhas de C/ASM** (148 `.c`, 160 `.h`, 13 `.S`).

### 1.2 Módulos de `src/` por responsabilidade

**Camada de apresentação**
```
gui.c        (62 KB)  Loop principal, transições, diálogos de configuração, plasma de fundo
guigame.c    (55 KB)  Tela de configurações por jogo
themes.c     (62 KB)  Motor de temas: parsing, elementos, desenho
menusys.c    (35 KB)  Lista de menus/submenus, navegação, paginação
renderman.c  (15 KB)  Abstração sobre gsKit: modos de vídeo, escala, quads
fntsys.c     (22 KB)  FreeType + atlas de glifos + cache
textures.c   (17 KB)  Decodificação PNG (libpng) → GSTEXTURE
texcache.c   (4,6 KB) Cache LRU assíncrono de capas
atlas.c      (4,5 KB) Alocador binário de espaço em atlas
dia.c/dialogs.c (75 KB) Framework declarativo de diálogos
sound.c      (15 KB)  audsrv: SFX (ADPCM) + BGM (Ogg Vorbis)
pad.c        (13 KB)  libpadx, repetição de teclas, sensibilidade analógica
lang.c       (6 KB)   Sistema de idiomas
```

**Camada de dados / dispositivos**
```
iosupport.h            Interface abstrata `item_list_t` (vtable de dispositivo)
supportbase.c (27 KB)  Código comum: leitura de lista, ISO9660, ul.cfg, fragmentos
bdmsupport.c  (35 KB)  USB / iLink / MX4SIO / ATA via camada BDM
hddsupport.c  (27 KB)  HDD interno APA + formato HDLoader
ethsupport.c  (29 KB)  SMBv1 sobre rede
appsupport.c  (14 KB)  Homebrew (ELF) — a base do futuro "Emuladores/Homebrew"
config.c      (17 KB)  Parser/serializador de `.cfg` (chave=valor)
ioman.c       (7,4 KB) Fila de I/O assíncrona com thread worker
```

**Camada de lançamento**
```
opl.c      (68 KB)  main(), init, configuração global, orquestração de módulos
system.c   (35 KB)  sysLaunchLoaderElf(), patching de kernel, envio de IRX ao IOP
ioprp.c            Reconstrução de IOPRP (imagem de módulos IOP)
cheatman.c         Carregamento de cheats (.cht)
gsm.c              GS Mode Selector (forçar modos de vídeo em jogo)
xparam.c           Patches Deckard (consoles slim tardios)
vmc_groups.c (41 KB) Virtual Memory Cards
```

### 1.3 Fluxo de dados macro

```
                 ┌───────────────────────────────────────────┐
   EE (32MB)     │  opl.elf (OPNPS2LD.ELF)                   │
                 │  ┌─────────┐  ┌──────────┐  ┌──────────┐  │
                 │  │  GUI    │→ │ themes   │→ │renderman │──┼──► gsKit ──► GS (4MB VRAM)
                 │  │ (main)  │  │ menusys  │  │  fntsys  │  │
                 │  └────┬────┘  └────┬─────┘  └──────────┘  │
                 │       │            │                      │
                 │       │      ┌─────▼──────┐               │
                 │       │      │ texcache   │               │
                 │       │      └─────┬──────┘               │
                 │  ┌────▼────────────▼──────┐               │
                 │  │  ioman (thread worker)  │               │
                 │  └────────────┬───────────┘               │
                 │  ┌────────────▼───────────┐               │
                 │  │ item_list_t (vtable)   │               │
                 │  │ bdm / hdd / eth / app  │               │
                 │  └────────────┬───────────┘               │
                 └───────────────┼───────────────────────────┘
                            SIFRPC / fileXio
                 ┌───────────────▼───────────────────────────┐
   IOP (2MB)     │ bdm.irx, bdmfs_fatfs.irx, usbmass_bd.irx, │
                 │ mx4sio_bd.irx, IEEE1394_bd.irx, ps2atad,  │
                 │ ps2hdd, smbman, mcman, audsrv...          │
                 └───────────────────────────────────────────┘
                                  │  ao lançar o jogo
                 ┌────────────────▼──────────────────────────┐
                 │ ExecPS2(ee_core) → cdvdman falsificado    │
                 │  → o jogo acredita estar lendo cdrom0:    │
                 └───────────────────────────────────────────┘
```

---

## 2. Linguagem utilizada

| Componente | Linguagem | Toolchain |
|---|---|---|
| Aplicação EE | **C** (C99, GCC) | `ee-gcc` do PS2SDK |
| ee_core / engine | **C + Assembly MIPS** (`.S`) | `ee-gcc` + `ee-as` |
| Módulos IOP | **C** (freestanding, sem libc) | `iop-gcc` |
| Build | **GNU Make** | Makefile recursivo |
| Ferramentas auxiliares | **Python 3** (`lang_compiler.py`, `ziso.py`), **Shell** | — |
| Ferramentas de PC | **C** (`iso2opl`, `opl2iso`, `genvmc`) | gcc host |

**Não há C++, nem alocação dinâmica de alto nível, nem RTTI/exceptions.** Toda a interface é C puro
com `malloc`/`free` e ponteiros de função como mecanismo de polimorfismo (`item_list_t`,
`theme_element_t::drawElem`).

Bibliotecas externas ligadas (`Makefile:129`):
`gsKit`, `dmaKit`, `libpng`, `zlib`, `freetype`, `libvux` (VU0 math), `libmc`, `libcdvd`,
`netman`, `ps2ips`, `audsrv`, `libvorbisfile`, `libpadx`, `libpoweroff`, `fileXio`, `elf-loader`.

---

## 3. Como é feita a renderização da interface

### 3.1 Camada base: `renderman.c`

O OPL **não** desenha diretamente no GS. Tudo passa por `renderman`, que fornece um espaço de
coordenadas **virtual fixo de 640×480 com pixels quadrados** (`renderman.h:5-16`). Isso é a
decisão arquitetural mais importante do renderizador: temas escritos para 640×480 funcionam em
qualquer modo de vídeo.

**Tabela de modos** (`renderman.c:37-71`) — 14 modos:

| Grupo | Modos | Passes | Cor |
|---|---|---|---|
| Padrão (bordas pretas) | AUTO, PAL, NTSC, 480p, 576p, VGA640 | 1 | `GS_PSM_CT24` |
| Tela cheia (HIRES) | PAL704, NTSC704, 480p704, 576p704 | 2 | CT24 |
| Alta definição (HIRES) | 720p, 1080i | 3 | `GS_PSM_CT16S` |
| Legado (não-entrelaçado) | PAL256, NTSC224 | 1 | CT24 |

Cada modo carrega `PAR1`/`PAR2` (Pixel Aspect Ratio) para corrigir pixels não-quadrados de PAL/NTSC.

**Escala** (`renderman.c:271-272`):
```c
#define X_SCALE(x) (((x)*iDisplayWidth) / 640)
#define Y_SCALE(y) (((y)*iDisplayHeight) / 480)
```
`iDisplayWidth/Height` já descontam overscan (`rmSetOverscan`, `renderman.c:437`).

**Widescreen é anamórfico**, não uma resolução nova: `rmSetAspectRatio(RM_ARATIO_16_9)` apenas
troca `iAspectWidth` de 4 para 3, e larguras marcadas com `SCALING_RATIO` são multiplicadas por
`3/4` (`renderman.c:412-425`). Posições **não** são escaladas — só dimensões.

**Primitivas disponíveis** (única API de desenho que os temas podem usar):
- `rmDrawPixmap(txt, x, y, aligned, w, h, scaled, color)` — sprite texturizado
- `rmDrawOverlayPixmap(...)` — sprite + quad deformado por 4 cantos (usado para a "caixa de jogo")
- `rmDrawRect(x, y, w, h, color)` — retângulo sólido/alfa
- `rmDrawLine(x1, y1, x2, y2, color)` — linha

Não existe rotação, escala não-uniforme arbitrária, blur, sombra ou shader. **Qualquer efeito
visual novo precisa ser construído a partir dessas quatro primitivas.**

### 3.2 Sincronização de frame

`rmEndFrame()` (`renderman.c:105-131`) no modo padrão:
1. `gsKit_set_finish` + `gsKit_queue_exec` + `gsKit_finish` (espera o GS terminar)
2. `SleepThread()` — a thread da GUI **dorme** até o handler de VSync acordá-la
   (`rmOnVSync`, `renderman.c:133-142`)
3. Faz o flip manual de `DISPFB2` (double buffering)

Ou seja: **a taxa de quadros está travada em VSync (50/60 Hz)** e a thread principal fica
bloqueada entre frames — é isso que libera CPU para a thread de I/O carregar capas.

No modo HIRES (múltiplos passes) o caminho é `gsKit_hires_sync` + `gsKit_hires_flip`, sem
`SleepThread` — **e sem `gsKit_clear`** (`renderman.c:100`), o que já é um detalhe relevante de
custo.

### 3.3 Ordem de desenho

Não há Z-buffer (`gsGlobal->ZBuffering = GS_SETTING_OFF`, `renderman.c:196`). A ordenação é feita
por um contador `order` incrementado a cada primitiva e passado ao gsKit — **a ordem de desenho é
literalmente a ordem das chamadas**. Isso significa que a ordem dos elementos no arquivo de tema
define o empilhamento visual.

### 3.4 Loop de frame completo

`guiMainLoop()` (`gui.c:1560-1599`):
```c
while (!gTerminate) {
    guiStartFrame();          // guiLock() + rmStartFrame() + guiFrameId++
    guiReadPads();            // atualiza guiInactiveFrames
    guiShow();                // renderScreen() do handler ativo (+ fade de transição)
    guiDrawOverlays();        // popups, relógio, FPS em debug
    guiShowNotifications();
    guiHandleDeferredOps();   // aplica updates enfileirados por outras threads
    guiEndFrame();            // rmEndFrame() + guiUnlock()
    screenHandler->handleInput();
    if (gFrameHook) gFrameHook();
}
```

Transições entre telas são um **cross-fade de 26 frames** com curva `fade()` quíntica
(`gui.c:1484-1517`): metade desenha a tela antiga com alfa crescente, metade a nova com alfa
decrescente, sobrepondo um `rmDrawRect` preto.

### 3.5 Fundo procedural (plasma)

Quando o tema não define uma imagem de `Background`, o OPL gera **ruído Perlin 3D animado** em
tempo real (`guiDrawBGPlasma`, `gui.c:1269`), calculado com instruções **VU0** (`VU0MixVec`,
`gui.c:1121`) numa textura pequena (`PLASMA_W × PLASMA_H`) esticada para a tela inteira. É barato,
mas ainda assim é trabalho de CPU por frame — desligável ao definir um `Background` estático.

---

## 4. Como funciona o sistema de menus

### 4.1 Estruturas

Duas listas duplamente encadeadas (`menusys.h`):

```c
menu_item_t     // uma "aba" (= um dispositivo: USB, HDD, ETH, APPS)
  ├── icon_id, text_id, visible, userdata (→ item_list_t*)
  ├── submenu     → submenu_list_t*   (todos os jogos daquela aba)
  ├── current     → item selecionado
  ├── pagestart   → primeiro item visível da página
  ├── hints       → menu_hint_item_t* (dicas de botão no rodapé)
  └── execCross / execTriangle / execCircle / execSquare / refresh  (callbacks)

submenu_item_t  // um jogo
  ├── icon_id, text, text_id, id
  ├── cache_id[]  // um slot por cache de imagem do tema
  └── cache_uid[] // validação de reuso de slot
```

`cache_id`/`cache_uid` são arrays alocados dinamicamente com tamanho
`gTheme->gameCacheCount` (`menusys.c:389-405`) — ou seja, **cada jogo carrega um par de inteiros
por elemento de imagem do tema**. Trocar de tema força `submenuRebuildCache()` em toda a lista.

### 4.2 Cinco telas

`gui.h:56-60` define os `screenHandler`s:

| ID | Tela | Render | Input |
|---|---|---|---|
| `GUI_SCREEN_MAIN` | Lista de jogos | `menuRenderMain` | `menuHandleInputMain` |
| `GUI_SCREEN_MENU` | Menu principal (config) | `menuRenderMenu` | `menuHandleInputMenu` |
| `GUI_SCREEN_INFO` | Ficha do jogo (□) | `menuRenderInfo` | `menuHandleInputInfo` |
| `GUI_SCREEN_GAME_MENU` | Config por jogo (△) | `menuRenderGameMenu` | `menuHandleInputGameMenu` |
| `GUI_SCREEN_APP_MENU` | Config de homebrew | `menuRenderAppMenu` | `menuHandleInputAppMenu` |

### 4.3 Navegação

`menuHandleInputMain()` (`menusys.c:957`):

| Botão | Ação |
|---|---|
| ←/→ | `menuPrevH`/`menuNextH` — troca de **dispositivo** (aba) |
| ↑/↓ | `menuPrevV`/`menuNextV` — move na lista de jogos |
| L1/R1 | página anterior/próxima |
| L2/R2 | primeira/última página |
| ✕ (ou ○ em consoles JP) | `execCross` — **lançar jogo** |
| △ | `execTriangle` — configurações do jogo |
| □ | `execSquare` — tela de informações |
| ○ | `execCircle` |
| START | menu principal |
| SELECT | `refresh` — recarregar a lista do dispositivo |

O botão de confirmação é detectado por região do console: `gSelectButton = (região == JAPAN) ? KEY_CIRCLE : KEY_CROSS` (`opl.c`, dentro de `init()`).

Velocidade de rolagem é implementada como **atraso de auto-repetição do pad**, não como animação:
`setButtonDelay(KEY_UP, 500 - gScrollSpeed*200)` (`gui.c:1625-1638`).

### 4.4 Ordenação

`submenuSort()` (`menusys.c:561`) é um **bubble sort com troca de nós** na lista encadeada,
comparando `submenuItemGetText()`. Para bibliotecas grandes (500+ jogos) isso é O(n²) com
`strcmp` — um dos pontos mais caros de todo o sistema.

### 4.5 Atualização assíncrona

Outras threads nunca tocam o menu diretamente. Elas criam um `struct gui_update_t`
(`gui.h:22-46`) e chamam `guiDeferUpdate()`; o loop de frame aplica via `guiHandleDeferredOps()`
(`gui.c:1061`). Operações: `GUI_OP_ADD_MENU`, `APPEND_MENU`, `SELECT_MENU`, `CLEAR_SUBMENU`,
`SORT`, `ADD_HINT`.

---

## 5. Como são carregadas as capas

Esta é a parte mais relevante para o RetroHub, porque é onde está o custo de I/O e memória.

### 5.1 Convenção de arquivos

Capas ficam em `<prefixo>/ART/` com o padrão:

```
ART/<startup>_<sufixo>.png
```

Onde `<startup>` é o **código do disco** (ex.: `SLUS_200.02`) e `<sufixo>` vem do tipo de
elemento do tema (`themes.c:1062-1068`):

| Sufixo | Elemento de tema | Cache padrão | Uso |
|---|---|---|---|
| `COV` | `ItemCover` | 10 entradas | Capa frontal |
| `ICO` | `ItemIcon` | 20 entradas | Ícone/disco |
| `BG` | `Background` (pattern) | — | Fundo por jogo |
| `SCR`, `SCR2` | `GameImage` | — | Screenshots |
| `LGO` | `GameImage` | — | Logotipo |

Exemplo: `mass0:/ART/SLUS_200.02_COV.png`.

### 5.2 Caminho de resolução

`bdmGetImage()` (`bdmsupport.c:578-589`):
```c
if (isRelative) snprintf(path, ..., "%s%s/%s_%s", prefix, folder, value, suffix);
else            snprintf(path, ..., "%s%s_%s",    folder, value, suffix);
return texDiscoverLoad(resultTex, path, -1);
```
`hddGetImage` e `ethGetImage` são idênticos em forma. Para homebrew, `appGetImage`
(`appsupport.c:458`) delega a `oplGetAppImage()` (`opl.c:466-501`), que **procura em todos os
dispositivos por ordem de prioridade** (`appsPriority`, 0 = mais rápido).

`texDiscoverLoad()` (`textures.c:552`) tenta as extensões conhecidas e decodifica via **libpng**
(`texLoadAll`, `textures.c:412`), suportando PNG de 4/8/24/32 bits — os de 4 e 8 bits viram
texturas **CLUT** (paletizadas), o que é ~4× mais econômico em VRAM.

Limite rígido de tamanho: `maxSize = 720*512*4` bytes (`textures.c:104`) — capas maiores são
rejeitadas.

### 5.3 O cache LRU assíncrono

`texcache.c` é o coração do carregamento de arte.

**Estrutura** (`texcache.h`): um `image_cache_t` por elemento de imagem do tema, com N
`cache_entry_t` (textura + `lastUsed` + `UID` + ponteiro de request pendente `qr`).

**Algoritmo** de `cacheGetTexture()` (`texcache.c:120-180`):

```
1. cacheId == -2  → esta entrada já falhou; devolve NULL (não tenta de novo)
2. cacheId >= 0 e UID confere:
      qr != NULL        → ainda carregando, devolve NULL
      lastUsed == 0     → falhou; marca -2
      senão             → lastUsed = guiFrameId; devolve a textura   ← HIT
3. guiInactiveFrames < list->delay  → o usuário ainda está rolando; NÃO carrega  ← anti-thrash
4. Procura a entrada com menor lastUsed (LRU) e não pendente
5. Cria load_image_request_t, marca qr, atribui novo UID
6. ioPutRequest(IO_CACHE_LOAD_ART, req)   → thread de I/O
7. Devolve NULL neste frame
```

O passo 3 é a otimização crítica de UX: `MENU_MIN_INACTIVE_FRAMES = 8` (`iosupport.h:56`) e o
`delay` configurável por dispositivo (`usb_frames_delay`) evitam disparar leituras de disco
enquanto o usuário segura o direcional.

**Do lado da thread de I/O**, `cacheLoadImage()` (`texcache.c:16-45`) revalida `cacheUID` (se o
slot foi reciclado nesse meio-tempo, descarta o trabalho), libera a textura antiga com `texFree`
e chama `handler->itemGetImage(...)`.

### 5.4 Ciclo de vida da textura na VRAM

`gsKit_TexManager_bind()` (chamado em `rmDrawQuad`, `renderman.c:283`) faz o upload sob demanda
para a VRAM; `gsKit_TexManager_nextFrame()` no fim de cada frame gerencia a expulsão. A RAM
principal é liberada por `cacheClearItem(item, 1)` que chama `rmUnloadTexture` + `free`
(`texcache.c:53-70`).

**Consequência prática:** uma capa 512×512 em CT24 ocupa **768 KB** em RAM principal. Com o cache
padrão de 10 entradas, um tema com capas grandes pode consumir **7,5 MB dos 32 MB do console**.
Este é o principal orçamento a administrar num front-end de "capas grandes".

---

## 6. Como funciona o gerenciamento de memória

### 6.1 Não há alocador customizado

O OPL usa `malloc`/`free`/`memalign` da newlib do PS2SDK. Não há pool, arena, slab ou
garbage collection. As regras são convencionais e a disciplina é manual.

### 6.2 Mapa de memória do EE

| Região | Uso |
|---|---|
| `0x00082000` | Patch de alarme |
| `0x00084000 – 0x00100000` | Limpo antes do lançamento (`system.c`, `memset`) |
| `0x00097000` | `OPL_MOD_STORAGE` — área padrão de armazenamento de módulos IRX (`iosupport.h:52`) |
| ~`0x00100000+` | ee_core + heap do OPL |

### 6.3 Alocações significativas

| O quê | Onde | Tamanho |
|---|---|---|
| Stack da thread de I/O | `ioman.c:20` | **96 KB** estáticos |
| Textura do plasma | `gui.c:145` | `PLASMA_W*PLASMA_H*4` |
| Atlas de fontes | `fntsys.c:24-29` | até **4 atlas de 256×256** por fonte |
| Cache de capas | `texcache.c` | N × tamanho da imagem |
| Lista de jogos | `supportbase.c` | `sizeof(base_game_info_t)` (≈ 200 B) × N jogos |
| `cache_id`/`cache_uid` | `menusys.c:397` | 2 × `gameCacheCount` × 4 B × N jogos |
| Módulos IRX embutidos | `.data` do ELF | ~2 MB |

Para 1.000 jogos com um tema de 3 caches de imagem: `200 B × 1000` (lista) + `2×3×4 B × 1000`
(cache ids) + nós de `submenu_list_t` ≈ **~250 KB só de metadados** — aceitável, mas cresce
linearmente e **é reconstruído inteiro a cada refresh**.

### 6.4 Concorrência

Três threads:
1. **GUI** (prioridade 31) — loop de frame, dorme em VSync
2. **I/O worker** (prioridade 30, `ioman.c:184`) — dorme em `SleepThread()`, acorda por `WakeupThread`
3. **Rede** (quando SMB está ativo)

Sincronização por **semáforos EE**: `gProcSemaId`/`gEndSemaId` (fila de I/O),
`gGUILockSemaId` (`guiLock`/`guiUnlock` envolve todo o frame), `gFontSemaId`, `bdmLoadModuleLock`.

`ioBlockOps(1)` (`ioman.c:301`) eleva temporariamente a prioridade da thread chamadora para 90 e
faz **busy-wait** até esvaziar a fila — usado antes de operações destrutivas.

### 6.5 Riscos observáveis no código

- `ioRegisterHandler` faz `return` em caminhos de erro **sem `SignalSema(gProcSemaId)`**
  (`ioman.c:60-72`) — deadlock latente (só ocorre em condições de erro de programação).
- `cacheGetTexture` aloca `load_image_request_t` sem verificar `NULL`.
- Não há verificação de retorno de `malloc` na maioria dos caminhos de UI.

---

## 7. Como o OPL detecta dispositivos (USB / HDD / SMB / MX4SIO)

### 7.1 A abstração central: `item_list_t`

`iosupport.h:83-146` define a vtable que **todo** backend implementa:

```c
typedef struct _item_list_t {
    short int mode;              // BDM_MODE..BDM_MODE4, ETH_MODE, HDD_MODE, APP_MODE
    char appsPriority, enabled;
    unsigned char flags;         // MODE_FLAG_NO_COMPAT / COMPAT_DMA / NO_UPDATE
    int delay, updateDelay;
    void *priv;                  // dados por dispositivo
    void *owner;                 // opl_io_module_t

    int   (*itemNeedsUpdate)(...);   // polling: mudou algo?
    int   (*itemUpdate)(...);        // reconstruir lista → devolve contagem
    int   (*itemGetCount)(...);
    void *(*itemGet)(..., int id);
    char *(*itemGetName)(..., int id);
    char *(*itemGetStartup)(..., int id);   // código do disco
    void  (*itemLaunch)(..., int id, config_set_t*);   // ← lança o jogo
    config_set_t *(*itemGetConfig)(..., int id);
    int   (*itemGetImage)(...);      // ← carrega arte
    void  (*itemDelete)/(*itemRename)/(*itemCleanUp)/(*itemShutdown);
    int   (*itemCheckVMC)(...);
    int   (*itemIconId)(...);
} item_list_t;
```

**Este é o contrato que o RetroHub deve preservar integralmente.** Uma UI nova só precisa consumir
esta interface; nada dela conhece USB, SMB ou APA.

Modos disponíveis (`iosupport.h:6-16`): `BDM_MODE`..`BDM_MODE4` (5 dispositivos de bloco),
`ETH_MODE`, `HDD_MODE`, `APP_MODE` — total `MODE_COUNT = 8`.

### 7.2 BDM: USB, iLink, MX4SIO e ATA sob um único driver

A grande simplificação do OPL moderno: **USB, iLink (Firewire), MX4SIO (SD no slot de MC) e
ATA/IDE são todos "block devices"** expostos como `mass0:` … `mass4:` (`MAX_BDM_DEVICES = 5`,
`bdmsupport.h:18`).

Carregamento de módulos (`bdmLoadModules`, `bdmsupport.c:109`):
```
bdm.irx            ← gerenciador de dispositivos de bloco
bdmfs_fatfs.irx    ← FAT12/16/32/exFAT
usbd.irx + usbmass_bd.irx        ← USB (sempre)
  [opcionais, via bdmLoadBlockDeviceModules, bdmsupport.c:76]
  iLinkman.irx + IEEE1394_bd.irx ← se gEnableILK
  mx4sio_bd.irx                  ← se gEnableMX4SIO
  ps2dev9 + ps2atad (hddLoadModules) ← se gEnableBdmHDD
bdmevent.irx       ← notificações de conexão/desconexão
```

**Detecção de tipo** (`bdmUpdateDeviceData`, `bdmsupport.c:784-861`):
1. `fileXioDopen("massN:/")` — se abre, o dispositivo existe
2. `fileXioIoctl2(dir, USBMASS_IOCTL_GET_DRIVERNAME, ...)` devolve o nome do driver
3. Mapeamento:
   | `bdmDriver` | Tipo |
   |---|---|
   | `"usb"` | `BDM_TYPE_USB` |
   | `"sd"` (len 2) | `BDM_TYPE_ILINK` |
   | `"sdc"` (len 3) | `BDM_TYPE_SDC` (**MX4SIO**) |
   | `"ata"` (len 3) | `BDM_TYPE_ATA` (+ flag `MODE_FLAG_COMPAT_DMA`) |
4. Para ATA, consulta `xhdd0:` via devctl para LBA48 e maior modo UDMA suportado
   (limitado a UDMA 4 por compatibilidade — `bdmsupport.c:770-781`)
5. Marca `menuItem.visible = 1` (dispositivo apareceu) ou `0` (sumiu)

**Hot-plug**: `bdmevent.irx` incrementa `BdmGeneration` via `SifAddCmdHandler`
(`bdmsupport.c:69-73`). `bdmNeedsUpdate()` compara `bdmDeviceTick != BdmGeneration` — se igual,
retorna 0 imediatamente e **não faz I/O nenhum**. É um contador de geração, barato e correto.

Ao detectar conexão/desconexão toca `SFX_BD_CONNECT`/`SFX_BD_DISCONNECT`.

**Polling adicional** por `bdmNeedsUpdate()`: `stat()` em `<prefix>CD` e `<prefix>DVD` comparando
`st_mtime`, e `sbIsSameSize()` sobre `ul.cfg`. Também aproveita para carregar temas (`THM/`) e
idiomas (`LNG/`) do dispositivo e criar as pastas padrão (`sbCreateFolders`).

### 7.3 HDD interno (APA)

`hddsupport.c` — caminho separado do BDM porque usa o **sistema de partições APA da Sony**, não FAT:
- Módulos: `ps2dev9`, `ps2atad`, `ps2hdd`, `ps2fs` (PFS)
- Jogos no **formato HDLoader**: partições `PP.<ID>.<nome>` com cabeçalho `hdl_game_info_t`
- Configs em `hdd0:__common/OPL/CFG/`, prefixo `gHDDPrefix`
- Suporta cache de lista (`gHDDGameListCache`) porque varrer a tabela APA é lento

### 7.4 SMB (ETH)

`ethsupport.c`:
- Pilha: `ps2dev9` → `smap` → `netman` → `ps2ip`/`ps2ips` → `smbman`
- IP estático ou DHCP (`ps2_ip_use_dhcp`), NetBIOS opcional via `nbns.c`
- Configuração em `CONFIG_NETWORK`: share, usuário, senha, porta
- `gNetworkStartup` > 0 = carregando, < 0 = erro (códigos em `iosupport.h:24-49`)
- Estados de erro específicos: `ERROR_ETH_SMB_CONN`, `_LOGON`, `_OPENSHARE`, `_LISTSHARES`…

### 7.5 Layout de pastas esperado (idêntico em todos os dispositivos)

`sbCreateFolders()` (`supportbase.c:822`) cria:
```
<prefixo>/
  CD/     ISOs de CD
  DVD/    ISOs de DVD
  CFG/    <startup>.cfg  — configurações e metadados por jogo
  ART/    <startup>_COV.png, _ICO.png, _BG.png, _SCR.png…
  THM/    temas
  LNG/    idiomas
  VMC/    <nome>.bin  — memory cards virtuais
  CHT/    <startup>.cht — cheats
  APPS/   homebrew (ELF)
  ul.cfg  índice do formato USB Extreme
```

---

## 8. Como funciona a leitura das ISOs

### 8.1 Três formatos de jogo

`supportbase.h:12-16`:
```c
GAME_FORMAT_USBLD    // USB Extreme: ul.cfg + partes ul.<crc32>.<startup>.<NN>
GAME_FORMAT_OLD_ISO  // SLUS_123.45.NomeDoJogo.iso
GAME_FORMAT_ISO      // NomeDoJogo.iso  (moderno)
```

### 8.2 Validação de nome

`isValidIsoName()` (`supportbase.c`):
- Extensão `.iso` ou `.zso` (case-insensitive)
- Se `size >= 17` **e** `name[4]=='_'` **e** `name[8]=='.'` **e** `name[11]=='.'` → formato antigo
  (o ID do disco está no próprio nome)
- Senão → formato moderno, nome livre até `ISO_GAME_NAME_MAX = 160` chars

### 8.3 Descoberta do código do disco

`scanForISO()` (`supportbase.c`) para cada arquivo válido:

1. **Formato antigo**: extrai `startup` dos 11 primeiros caracteres do nome. Zero I/O.
2. **Formato moderno**: consulta o **cache de lista** (`queryISOGameListCache`). Se não houver:
   ```c
   fileXioMount("iso:", fullpath, FIO_MT_RDONLY);
   GetStartupExecName("iso:/SYSTEM.CNF;1", startup, GAME_STARTUP_MAX-1);
   fileXioUmount("iso:");
   ```
   Ou seja: **monta a ISO como sistema de arquivos e lê `SYSTEM.CNF`** para extrair a linha
   `BOOT2 = cdrom0:\SLUS_200.02;1` (`ps2cnf.c`).

Este é o passo mais caro da varredura, e por isso existe o cache em disco (`loadISOGameListCache`
/ `updateISOGameList`) — o mesmo mecanismo que o RetroHub deve estender para guardar metadados.

### 8.4 ZSO (ISO comprimida)

`zso.c` + `lz4.c`. `ProbeZISO(fd)` detecta o cabeçalho; leitura setor a setor via
`ziso_read_sector()`. O tamanho do cache de descompressão é configurável
(`settings->common.zso_cache = bdmCacheSize`).

### 8.5 Detecção de DVD dual-layer

`sbGetISO9660MaxLBA()` devolve o LBA máximo declarado no descritor primário. Depois
`sbProbeISO9660(path, game, layer1_offset)` (`supportbase.c`) verifica se **naquele offset existe
outro descritor ISO9660** (`buffer[0]==1 && "CD001"`):
- Encontrou → é dual-layer; `layer1_start = maxLBA - 16`
- Não encontrou → single-layer; `layer1_start = 0`

Para USBLD multi-parte, calcula ainda em qual parte a camada 1 começa
(`layer1_part = layer1_start / 0x80000`).

### 8.6 Fragmentação — a restrição mais importante

O jogo **não** é lido pelo sistema de arquivos em tempo de execução. O OPL monta uma **lista de
fragmentos LBA** antes de sair, e o `cdvdman` falsificado lê setores brutos do dispositivo de bloco.

`bdmLaunchGame()` (`bdmsupport.c:428-458`):
```c
int iFragCount = fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_FRAGLIST, NULL, 0,
                               &settings->frags[iTotalFragCount],
                               sizeof(bd_fragment_t) * (BDM_MAX_FRAGS - iTotalFragCount));
if (iFragCount > BDM_MAX_FRAGS)   // BDM_MAX_FRAGS = 64
    → erro _STR_ERR_FRAGMENTED
```

**Limite: 64 fragmentos** (`cdvd_config.h:65`). ISOs muito fragmentadas simplesmente não rodam.
O mesmo vale para VMCs, com verificação extra da cadeia de clusters
(`USBMASS_IOCTL_CHECK_CHAIN`) porque escrita em arquivo fragmentado corromperia o FAT.

---

## 9. Como funciona o carregamento dos jogos

Sequência completa a partir do ✕ na lista.

### 9.1 Fase 1 — Preparação (no backend, ex.: `bdmLaunchGame`)

1. **VMC (slots 0 e 1)**: `sysCheckVMC` lê o superbloco; obtém LBA inicial
   (`USBMASS_IOCTL_GET_LBA`) e valida a cadeia de clusters. Grava
   `bdm_vmc_infos_t` diretamente **dentro da imagem do IRX** `bdm_mcemu.irx`, procurando o
   marcador mágico `0xC0DEFAC0 + vmc_id` (`bdmsupport.c:401-410`).
2. **Escolha do cdvdman**: `bdm_ata_cdvdman.irx` se driver ATA, senão `bdm_cdvdman.irx`.
3. **`sbPrepare()`** (`supportbase.c`) — o patching central:
   - Localiza a "patch zone" procurando `cdvdman_settings_common_sample` byte a byte dentro do IRX
   - Escreve `NumParts`, `media`, `DiscID` (binário, de `configGetDiscIDBinary`)
   - Traduz o `compatmask` em flags do IOP:
     | Bit | Nome | Flag IOP |
     |---|---|---|
     | `COMPAT_MODE_1` | Accurate Reads | `IOPCORE_COMPAT_ACCU_READS` |
     | `COMPAT_MODE_2` | Alternative Read | `IOPCORE_COMPAT_ALT_READ` |
     | `COMPAT_MODE_3` | Unhook Syscalls | (tratado no ee_core) |
     | `COMPAT_MODE_4` | Skip Videos | `IOPCORE_COMPAT_0_SKIP_VIDEOS` |
     | `COMPAT_MODE_5` | Emulate DVD-DL | `IOPCORE_COMPAT_EMU_DVDDL` |
     | `COMPAT_MODE_6` | Disable IGR | `IOPCORE_ENABLE_POFF` |
   - Define `fakemodule_flags` (CDVDFSV, CDVDSTM, USBD, DEV9, ATAD…)
   - Inicializa GSM, cheats, pademu e OSD config
4. **Lista de fragmentos** (seção 8.6)
5. **Dual-layer** (seção 8.5)
6. **Cheats**: `sbLoadCheats(prefix, startup)` lê `CHT/<startup>.cht`
7. **Last played**: grava `last_played` em `CONFIG_LAST`
8. **DMA/spindown** para ATA
9. **`deinit(NO_EXCEPTION, mode)`** — desmonta tudo: GUI, temas, fontes, dispositivos.
   ⚠️ Isso libera `bdmGames`, então o ponteiro `game` fica inválido depois desta linha.

### 9.2 Fase 2 — `sysLaunchLoaderElf()` (`system.c:796-1020`)

```
1.  AddHistoryRecordUsingFullPath(filename)      → histórico do OSD
2.  cleareffects.irx                             → silencia libsd
3.  memset(0x00084000 .. 0x00100000, 0)          → limpa RAM baixa
4.  GetModStorageLocation(startup, compatFlags)  → onde colocar os IRX
5.  sendIrxKernelRAM(...)                        → copia todos os IRX para a RAM do kernel
6.  ApplyDeckardXParam(filename)                 → patches para consoles slim
7.  initKernel(eh->entry, ModuleStorageEnd, ...) → substitui EELOAD, protege a região
8.  Localiza EECoreConfig_t no binário do ee_core pelos magics EE_CORE_MAGIC_0/1
9.  Preenche a config: ExitPath, GameModeDesc, GameID, IP/máscara/gateway,
    cheats, GSM, pademu, OSD config, ModStorageStart/End, _CompatMask
10. Copia os PT_LOAD do ELF do ee_core para os endereços virtuais e zera .bss
11. argv = ["rom0:PS2LOGO"]? + "cdrom0:\SLUS_XXX.XX;1"
12. fileXioExit(); SifExitRpc(); FlushCache(0); FlushCache(2);
13. ExecPS2(eh->entry, NULL, argc, argv)          ← ponto sem retorno
```

O `mode_str` identifica o caminho ao ee_core: `"BDM_USB_MODE"`, `"BDM_ILK_MODE"`,
`"BDM_M4S_MODE"`, `"BDM_ATA_MODE"`, `"HDD_MODE"`, `"SMB_MODE"`.

### 9.3 Fase 3 — ee_core em jogo

O `ee_core` fica **residente**, sobrevive ao `LoadExecPS2` do jogo e:
- Substitui `cdvdman`/`cdvdfsv` do IOP pelos falsificados → o jogo lê `cdrom0:` e recebe setores
  do USB/HDD/SMB
- Hospeda a **engine de cheats** (`cheat_engine.S`)
- Hospeda **GSM** (`gsm_engine.S`) para forçar modos de vídeo
- Hospeda o **hook de pad** para IGR (In-Game Reset) e pademu
- Emula Memory Card via `mcemu` quando há VMC

### 9.4 Auto-start

`main()` (`opl.c`) aceita `argc >= 5`:
- `argv[4] == "mini"` → `autoLaunchHDDGame(argv)` — **pula toda a GUI**
- `argv[4] == "bdm"` → `autoLaunchBDMGame(argv)`

Usa `miniDeinit()` em vez de `deinit()`. Também existe o "Last Played Auto Start"
(`gAutoStartLastPlayed`, contagem regressiva `RemainSecs` em `menuHandleInputMain`).

---

## 10. Como funciona o sistema de temas

### 10.1 Formato

Um tema é uma **pasta** em `THM/` contendo `conf_theme.cfg` + PNGs. Sintaxe do `.cfg`
(`misc/conf_theme_OPL.cfg`):

```
main0:
	type=Background
	default=background
	width=DIM_INF
	height=DIM_INF
main3:
	type=ItemCover
	default=cover
	x=-86
	y=-296
	overlay=case
	overlay_ulx=0
	overlay_uly=9
	...
```

Prefixos de seção definem em qual tela o elemento aparece:

| Prefixo | Tela |
|---|---|
| `mainN` | Lista de jogos |
| `infoN` | Ficha do jogo (□) |
| `appsMainN` | Lista de homebrew |
| `appsInfoN` | Ficha de homebrew |

### 10.2 Os 17 tipos de elemento

`themes.c:33-77`:

| Tipo | Descrição |
|---|---|
| `AttributeText` | Texto de um atributo do `.cfg` do jogo (`Title`, `Genre`, `Release`, `Developer`, `Description`, `#Size`…) |
| `StaticText` | Texto fixo |
| `AttributeImage` | Imagem escolhida pelo valor de um atributo |
| `GameImage` | Imagem por jogo com `pattern` (`COV`, `ICO`, `BG`, `SCR`…) e `count` de cache |
| `StaticImage` | Imagem fixa |
| `Background` | Fundo (se ausente → plasma Perlin) |
| `MenuIcon` / `MenuText` | Ícone e nome do dispositivo ativo |
| `ItemsList` | **A lista de jogos** |
| `ItemIcon` | Ícone do item selecionado (`ICO`, cache 20) |
| `ItemCover` | Capa do item selecionado (`COV`, cache 10) |
| `ItemText` | Nome do item selecionado |
| `HintText` / `InfoHintText` | Barra de dicas de botões |
| `LoadingIcon` | Animação de carregamento (8 frames) |
| `BdmIndex` | Indicador de qual `massN:` |
| `GameCountText` | Contador de jogos |

### 10.3 Propriedades comuns

`theme_element_t` (`themes.h:64-83`): `type`, `posX`, `posY`, `aligned`, `width`, `height`,
`scaled`, `color`, `font`, `extended` (dados específicos), `drawElem`/`endElem` (ponteiros de
função), `next`.

Convenções do parser:
- **Coordenada negativa** = medida a partir da borda oposta
- `DIM_INF` = preencher toda a dimensão
- `POS_MID` = centro
- `aligned` → `ALIGN_*` de `renderman.h`
- `font=N` seleciona um dos até `THM_MAX_FONTS = 16` fontes carregadas

### 10.4 Carregamento

`thmAddElements(path, sep, forceRefresh)` (`themes.c`) → `listDir()` com callback
`thmReadEntry`, até `THM_MAX_FILES = 64` temas. Temas são varridos em **todos** os dispositivos
(`bdmNeedsUpdate` chama `thmAddElements("<prefix>THM")`), então um pendrive pode adicionar temas
ao vivo.

O tema padrão está **embutido no ELF** (`conf_theme_OPL.o` em `MISC_OBJS`), garantindo que a UI
sempre funcione sem arquivos externos.

### 10.5 Sistema de renderização baseado em ponteiros de função

Cada elemento carrega seu próprio `drawElem`. `menuRenderElements()` (`menusys.c:928`)
simplesmente percorre a lista encadeada chamando cada um. Exemplo — `drawItemsList()`
(`themes.c:840-877`):

```c
submenu_list_t *ps = menu->item->pagestart;
while (ps && (others++ < itemsList->displayedItems)) {
    color = (ps == item) ? gTheme->selTextColor : elem->color;
    if (itemsList->decoratorImage) { ...rmDrawPixmap(ícone 20×20)... }
    fntRenderString(elem->font, x, posY, ..., submenuItemGetText(&ps->item), color);
    posY += MENU_ITEM_HEIGHT;   // 19 px fixos
    ps = ps->next;
}
```

**Observação de projeto:** a lista é estritamente vertical, altura de linha fixa em 19 px
(`opl.h:236`), sem animação e sem scroll suave. Uma grade de capas exige um novo tipo de elemento.

### 10.6 Deduplicação de recursos

`findDuplicate()` (`themes.c:494-497`) verifica se outro elemento já criou o mesmo cache/textura e
os compartilha via flags `cacheLinked`/`defaultTextureLinked`/`overlayTextureLinked` — evita
carregar `cover.png` duas vezes.

### 10.7 Fontes

`fntsys.c` usa **FreeType** com uma cache de glifos em **atlas de 256×256**, até 4 atlas por fonte
(`fntsys.c:24-29`). `atlas.c` implementa um alocador binário (divide o retângulo em `leaf1`/`leaf2`).
Fonte padrão: `PoeVeticaNew.ttf` embutida, tamanho 17. Temas podem carregar TTFs próprias
(`fntLoadFile`). `fntUpdateAspectRatio()` invalida **toda** a cache de glifos ao mudar resolução.

### 10.8 Cores

`theme_t` define `bgColor[3]`, `textColor`, `uiTextColor`, `selTextColor`. Se o tema não define,
usam-se as cores globais do usuário (`gDefaultBgColor` etc., configuráveis na UI).

---

## 11. Como funciona a compilação

### 11.1 Pré-requisitos

- **PS2SDK** (`$PS2SDK`) com toolchains `ee-gcc` e `iop-gcc`
- **gsKit** (`$GSKIT`)
- Ports do PS2SDK: `libpng`, `zlib`, `freetype`, `libvorbis`, `libogg`, `audsrv`
- Python 3 (para `lang_compiler.py`)
- Imagem oficial de referência: `ghcr.io/ps2homebrew/ps2homebrew:main`

### 11.2 Alvos

```bash
make                 # build padrão
make release         # build de release (strip + pack)
make clean
make debug           # debug da UI via UDPTTY
make iopcore_debug   # UI + módulos IOP em debug
make ingame_debug    # UI + debug em jogo
make eesio_debug     # debug via EE SIO
make deci2_debug     # DECI2
```

Flags de configuração (`Makefile:22-44`):
```bash
make EXTRA_FEATURES=1   # ativa RTL + IGS
make RTL=1              # suporte a idiomas da direita para a esquerda
make IGS=1              # In-Game Screenshot
make PADEMU=1           # emulação de pad (padrão: 1)
make DTL_T10000=1       # kit de desenvolvimento
make NOT_PACKED=1       # não comprimir o ELF
make LOCALVERSION=-retrohub   # sufixo de versão personalizado
```

### 11.3 Pipeline de build

```
1. Módulos IOP (modules/*)      → *.irx
2. ee_core (ee_core/*)          → ee_core.elf
3. bin2c: todo artefato binário vira um objeto .o com símbolo C
      *.irx  → extern void *xxx_irx; extern int size_xxx_irx;
      *.png  → extern void *xxx_png;
      *.adp  → áudio
      *.ttf  → poeveticanew_raw
      conf_theme_OPL.cfg → conf_theme_OPL_cfg
4. lang_compiler.py: lng_src/*.lng → lang_internal.c + lng/*.lng
5. Compila FRONTEND_OBJS (src/*.c)
6. Linka → opl.elf
7. strip → opl_stripped.elf
8. pack  → OPNPS2LD.ELF  (artefato final)
```

**Consequência importante:** *tudo* — módulos IOP, PNGs, sons, fonte, tema padrão — é **embutido
no ELF**. O binário final é autossuficiente; não há arquivos soltos obrigatórios. Isso explica o
tamanho e também por que adicionar assets custa RAM permanentemente.

### 11.4 Versionamento

`OPL_VERSION` é derivado de `git rev-list --count HEAD + 2`, hash curto e flag `-dirty`
(`Makefile:56-72`). Uma tag exata substitui tudo.

### 11.5 CI

`.github/workflows/compilation.yml`: matriz `EXTRA_FEATURES=[0,1]` × `PADEMU=[0,1]`, container
oficial, artefatos `OPNPS2LD-*.ELF`. Há também `check-format.yml` (clang-format, config em
`.clang-format`) e `OPLTestISO.yml`.

---

## 12. Quais partes podem ser modificadas sem quebrar a compatibilidade

Classificação em quatro zonas de risco.

### 🟢 ZONA VERDE — Alteração livre

Camada puramente de apresentação. Nada aqui é conhecido pelo motor de jogo.

| Arquivo | O que pode ser feito |
|---|---|
| `themes.c` | Adicionar tipos de elemento novos (grade de capas, cards, painéis) |
| `menusys.c` | Substituir navegação, ordenação, paginação, filtros |
| `gui.c` | Novo loop de telas, transições, telas inteiramente novas |
| `guigame.c` | Redesenhar a tela de opções por jogo |
| `dialogs.c` / `dia.c` | Novos diálogos |
| `fntsys.c` | Novas fontes, tamanhos, efeitos de texto |
| `gfx/` | Todos os PNGs |
| `audio/` | Todos os sons |
| `lng/` | Traduções |
| `misc/conf_theme_OPL.cfg` | Layout do tema padrão |

**Regra prática:** se o arquivo só chama `rm*`, `fnt*`, `cache*` e a vtable `item_list_t`, é zona
verde.

### 🟡 ZONA AMARELA — Alterável com cuidado

| Arquivo | Restrição |
|---|---|
| `texcache.c` | Pode-se mudar a política (LRU→ARC, pré-carregamento, mipmaps), **mas** manter a semântica de `qr`/`UID` — é o que impede corrupção quando um slot é reciclado durante uma leitura assíncrona |
| `renderman.c` | Pode-se adicionar primitivas novas. **Não** alterar o espaço virtual 640×480 nem a tabela de modos: temas legados quebram |
| `config.c` | Pode-se **adicionar** chaves. Nunca renomear/remover chaves existentes — configs de usuários e do `OPL Manager` deixariam de carregar |
| `ioman.c` | Ajustar tamanho de fila/prioridade é seguro; mudar o modelo de threads não |
| `appsupport.c` | Base para "Emuladores"/"Homebrew" — extensível, mas mantém o contrato de `item_list_t` |
| `sound.c` | Adicionar novos SFX é seguro; trocar audsrv não |

### 🟠 ZONA LARANJA — Só com testes em hardware real

| Arquivo | Motivo |
|---|---|
| `bdmsupport.c`, `hddsupport.c`, `ethsupport.c` | Detecção e enumeração. Um erro aqui = dispositivo invisível |
| `supportbase.c` | `sbReadList`, `sbPrepare`, ISO9660, fragmentos — coração da compatibilidade |
| `config.c` (formato do arquivo) | Compatibilidade com ferramentas de PC existentes |
| `opl.c` | Sequência de init/deinit, ordem de carregamento de módulos |

### 🔴 ZONA VERMELHA — **NÃO TOCAR**

Isto é o "motor" que o RetroHub explicitamente preserva.

| Componente | Por quê |
|---|---|
| `ee_core/**` | Núcleo residente em jogo. Qualquer alteração afeta a compatibilidade de **todos** os jogos |
| `modules/iopcore/cdvdman/**` | Emulação de CDVD — o jogo depende dela byte a byte |
| `modules/iopcore/cdvdfsv/**` | Servidor de arquivos falsificado |
| `modules/mcemu/**` | Emulação de Memory Card |
| `modules/hdd/**`, `modules/network/**` | Drivers IOP |
| `system.c:sysLaunchLoaderElf()` | Sequência exata de patch de kernel + `ExecPS2` |
| `supportbase.c:sbPrepare()` | Localização e escrita da "patch zone" nos IRX |
| `ioprp.c`, `xparam.c` | Reconstrução de IOPRP e patches Deckard |
| Estruturas `cdvdman_settings_*` | Layout binário compartilhado EE↔IOP |
| `EECoreConfig_t` | Layout binário compartilhado com o ee_core |
| Constante `BDM_MAX_FRAGS = 64` | Tamanho fixo da estrutura no IOP |

**Teste decisivo de segurança:** *"esta mudança altera algum byte que chega ao IOP ou ao ee_core?"*
Se sim → zona vermelha.

### 12.1 Contratos que devem ser preservados

1. **Layout de pastas** (`CD/`, `DVD/`, `CFG/`, `ART/`, `THM/`, `VMC/`, `CHT/`, `APPS/`, `ul.cfg`)
   — ferramentas de PC e usuários dependem dele.
2. **Nomenclatura de arte** `<startup>_<sufixo>.png`.
3. **Formato dos `.cfg`** — `chave=valor`, chaves `#`/`$` reservadas.
4. **`item_list_t`** — se a UI nova consumir esta vtable sem alterá-la, todos os backends
   continuam funcionando.
5. **`ul.cfg`** — estrutura binária `USBExtreme_game_entry_t` de 64 bytes.
6. **Espaço virtual 640×480** dos temas.

---

## 13. Métricas e limites relevantes para o projeto

| Recurso | Limite | Fonte |
|---|---|---|
| RAM do EE | 32 MB | hardware |
| RAM do IOP | 2 MB | hardware |
| VRAM do GS | 4 MB | `renderman.c:14` |
| Resolução virtual da UI | 640×480 | `renderman.h` |
| Dispositivos BDM simultâneos | 5 | `bdmsupport.h:18` |
| Modos de vídeo | 14 | `renderman.c:37` |
| Fragmentos por jogo | **64** | `cdvd_config.h:65` |
| Temas carregáveis | 64 | `themes.h:9` |
| Fontes por tema | 16 | `themes.h:10` |
| Atlas por fonte | 4 × 256×256 | `fntsys.c:24-29` |
| Cache de capas (`COV`) | 10 entradas | `themes.c:1067` |
| Cache de ícones (`ICO`) | 20 entradas | `themes.c:1064` |
| Tamanho máx. de textura | 720×512×4 = 1,4 MB | `textures.c:104` |
| Altura de linha do menu | 19 px | `opl.h:236` |
| Nome de ISO | 160 chars | `supportbase.h:3` |
| `startup` (código do disco) | 12 chars | `supportbase.h:5` |
| Chave de config | 32 chars | `config.h:132` |
| Valor de config | 256 chars | `config.h:133` |
| Fila de I/O | 64 requests / 64 handlers | `ioman.c:12-13` |
| Stack da thread de I/O | 96 KB | `ioman.c:18` |
| Frames inativos p/ carregar arte | 8 | `iosupport.h:56` |
| Frames de transição | 26 | `gui.c:1489` |

---

## 14. Descobertas que mudam o desenho do RetroHub

Cinco fatos do código que economizam trabalho significativo:

1. **Metadados já existem.** O tema padrão já referencia `Title`, `Genre`, `Release`,
   `Developer`, `Description`, `Rating`, `Vmode`, `Aspect`, `Scan` como `AttributeText`
   (`misc/conf_theme_OPL.cfg:88-174`). São chaves lidas de `CFG/<startup>.cfg`. **Ano, gênero,
   desenvolvedora e descrição não precisam de formato novo** — basta popular esses `.cfg` e
   adicionar `Players`, `Region`, `Favorite`, `LastPlayed`, `PlayCount`.

2. **Favoritos e recentes têm base pronta.** `CONFIG_LAST` já grava `last_played`
   (`bdmsupport.c:497-500`) e `OSDHistory.c` mantém histórico. Um `$Favorite=1` no `.cfg` do jogo
   e um índice `recent.cfg` cobrem o requisito sem tocar em zona vermelha.

3. **O cache assíncrono resolve "capas grandes".** `texcache.c` já tem anti-thrash por
   `guiInactiveFrames`, LRU e validação por UID. Uma grade de capas 4×3 precisa apenas de um
   `cacheCount` maior e de capas em **PNG paletizado (8 bits)**, que custam 1/4 da VRAM.

4. **A vtable `item_list_t` isola completamente a UI dos dispositivos.** Uma interface nova pode
   ser escrita inteira sem tocar em `bdmsupport.c`/`hddsupport.c`/`ethsupport.c`.

5. **O motor de temas é extensível por design.** Adicionar `ELEM_TYPE_COVER_GRID`,
   `ELEM_TYPE_CARD`, `ELEM_TYPE_TAB_BAR` é apenas: novo enum + nome em `elementsType[]` + função
   `initX`/`drawX` + um `else if` no parser. **Zero impacto em compatibilidade.**

---

## 15. Pontos fracos identificados (oportunidades)

| # | Problema | Local | Impacto |
|---|---|---|---|
| 1 | Ordenação O(n²) (bubble sort em lista encadeada) | `menusys.c:561` | Travamento perceptível com 500+ jogos |
| 2 | Lista de jogos reconstruída inteira a cada refresh | `supportbase.c:sbReadList` | Espera longa |
| 3 | Sem busca por nome | — | Navegar 1.000 jogos exige rolagem |
| 4 | Sem categorias/filtros | — | Requisito central do RetroHub |
| 5 | Layout de lista fixo em 19 px, sem grade | `themes.c:840`, `opl.h:236` | Impede "capas grandes" |
| 6 | Sem scroll suave (só repetição de tecla) | `gui.c:1625` | Sensação datada |
| 7 | Plasma Perlin custa CPU todo frame | `gui.c:1269` | Desperdício quando há fundo estático |
| 8 | `ioRegisterHandler` pode não liberar semáforo | `ioman.c:60-72` | Deadlock latente |
| 9 | Retornos de `malloc` raramente verificados | vários | Instabilidade sob pressão de memória |
| 10 | Montar ISO para ler `SYSTEM.CNF` é lento | `supportbase.c:scanForISO` | Mitigado por cache, ainda pesa no 1º boot |
| 11 | Metadados sem esquema definido/documentado | `CFG/*.cfg` | Cada ferramenta inventa o seu |
| 12 | Sem indexação persistente de biblioteca | — | Base do RetroHub Manager |

---

## 16. Conclusão da análise

O OPL é, na prática, **dois programas em um**:

- Um **motor de compatibilidade** (`ee_core` + `modules/iopcore` + `sbPrepare` +
  `sysLaunchLoaderElf`) — resultado de mais de uma década de engenharia reversa, extremamente
  sensível, e que o RetroHub **preserva integralmente**.
- Um **front-end** (`gui`, `menusys`, `themes`, `renderman`, `fntsys`, `texcache`) — bem
  estruturado, desacoplado por uma vtable limpa (`item_list_t`) e por um motor de temas
  data-driven, e **totalmente substituível**.

A fronteira entre os dois é nítida e definida por `item_list_t` (`iosupport.h:83`). Isso torna o
objetivo do projeto — nova experiência de usuário sobre o mesmo motor — não apenas viável, mas o
caminho natural que a arquitetura do OPL já sugere.

**Próximos documentos:**
- [`02-riscos-e-compatibilidade.md`](02-riscos-e-compatibilidade.md)
- [`03-arquitetura-retrohub-ps2.md`](03-arquitetura-retrohub-ps2.md)
- [`04-plano-de-desenvolvimento.md`](04-plano-de-desenvolvimento.md)
- [`05-retrohub-manager-pc.md`](05-retrohub-manager-pc.md)
