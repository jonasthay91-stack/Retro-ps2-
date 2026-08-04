# Arquitetura Proposta — RetroHub PS2

> Proposta de arquitetura para um front-end moderno sobre o motor do Open PS2 Loader.
> Baseado em [`01-analise-arquitetura-opl.md`](01-analise-arquitetura-opl.md) e nas regras de
> [`02-riscos-e-compatibilidade.md`](02-riscos-e-compatibilidade.md).

---

## 1. Princípio arquitetural

```
┌─────────────────────────────────────────────────────────────┐
│  CAMADA NOVA — RetroHub                            (escrita) │
│  Hub · Grade de capas · Busca · Filtros · Favoritos          │
│  Índice de biblioteca · Metadados estendidos · Temas RH      │
└────────────────────────────┬────────────────────────────────┘
                             │  item_list_t  (iosupport.h)
                             │  config_set_t (config.h)
                             │  rm* / fnt* / cache* / io*
┌────────────────────────────▼────────────────────────────────┐
│  CAMADA PRESERVADA — Serviços do OPL              (reusada) │
│  renderman · fntsys · texcache · ioman · config · sound      │
│  bdmsupport · hddsupport · ethsupport · appsupport           │
└────────────────────────────┬────────────────────────────────┘
                             │  sbPrepare() / sysLaunchLoaderElf()
┌────────────────────────────▼────────────────────────────────┐
│  MOTOR — CONGELADO                             (intocado)   │
│  ee_core · modules/iopcore · cdvdman · mcemu · IOPRP        │
└─────────────────────────────────────────────────────────────┘
```

**Regra de ouro:** o RetroHub adiciona arquivos; quase não modifica os existentes. Isso mantém
`git merge` com o upstream trivial (R-08) e torna a quebra do motor improvável por construção (R-01).

### 1.1 Arquivos novos

```
src/rh_index.c        Índice persistente da biblioteca
src/rh_meta.c         Leitura/escrita de metadados estendidos
src/rh_library.c      Modelo de biblioteca: filtros, ordenação, categorias
src/rh_hub.c          Tela inicial (hub de categorias)
src/rh_grid.c         Elemento de tema: grade de capas
src/rh_card.c         Elemento de tema: card de detalhes do jogo
src/rh_search.c       Busca por nome (teclado virtual + filtro incremental)
src/rh_favorites.c    Favoritos e recentes
src/rh_theme_ext.c    Registro dos novos tipos de elemento de tema
include/rh_*.h        Headers correspondentes
```

### 1.2 Arquivos existentes tocados (mínimo)

| Arquivo | Mudança | Zona |
|---|---|---|
| `src/themes.c` | Registrar novos tipos no parser (tabela de despacho) | 🟢 |
| `src/gui.c` | Registrar `GUI_SCREEN_HUB`, `_GRID`, `_SEARCH` | 🟢 |
| `src/menusys.c` | Trocar bubble sort por `qsort` sobre array | 🟢 |
| `include/config.h` | Adicionar chaves `$RH_*` / `rh_*` | 🟡 (só adição) |
| `Makefile` | Adicionar `rh_*.o` a `FRONTEND_OBJS` | 🟢 |

Nada mais. **Zero linhas** em `ee_core/`, `modules/`, `system.c`, `sbPrepare`.

---

## 2. Modelo de dados

### 2.1 O problema

Os metadados hoje vivem espalhados em `CFG/<startup>.cfg` — um arquivo por jogo, lido sob demanda.
Para ordenar 1.000 jogos por ano seria preciso abrir 1.000 arquivos. Inviável.

### 2.2 A solução: índice binário

Um arquivo único, mapeável, com registros de tamanho fixo.

```
<prefixo>/RH/library.idx
```

```c
/* ---- Cabeçalho (64 bytes) ---- */
typedef struct {
    char     magic[8];        // "RHIDX\0\0\0"
    u16      version;         // 1
    u16      recordSize;      // sizeof(rh_entry_t)
    u32      count;           // número de entradas
    u32      strTabOffset;    // offset da tabela de strings
    u32      strTabSize;
    u32      sourceHash;      // hash do estado da biblioteca (invalidação)
    u64      builtAt;         // timestamp de construção
    u8       reserved[28];
} rh_index_header_t;

/* ---- Entrada (64 bytes, alinhada) ---- */
typedef struct {
    char     startup[13];     // "SLUS_200.02" + NUL  (chave primária)
    u8       device;          // IO_MODES: qual item_list_t
    u8       format;          // GAME_FORMAT_*
    u8       media;           // SCECdPS2CD / SCECdPS2DVD

    u32      nameOff;         // offset na tabela de strings
    u32      developerOff;
    u32      genreOff;        // índice de gênero (ver 2.4)

    u16      year;            // 0 = desconhecido
    u8       players;         // 0 = desconhecido
    u8       region;          // RH_REGION_*
    u8       genreId;         // enum, para filtro rápido
    u8       compat;          // 0-5 estrelas (compatibilidade conhecida)
    u8       flags;           // RH_FLAG_FAVORITE | _HIDDEN | _PS1 | _APP | _EMU
    u8       category;        // RH_CAT_*

    u32      sizeMB;
    u32      lastPlayed;      // epoch, 0 = nunca
    u16      playCount;
    u16      _pad;

    u8       reserved[8];
} rh_entry_t;                 // 64 bytes exatos
```

**1.000 jogos = 64 KB** de índice + tabela de strings (~60 KB). Cabe folgado no orçamento de 1 MB.

### 2.3 Índices pré-ordenados

Junto ao índice, quatro arrays de `u16` (2 KB cada para 1.000 jogos):

```c
u16 *orderByName;       // ordenação alfabética
u16 *orderByYear;
u16 *orderByGenre;
u16 *orderByDeveloper;
```

Construídos uma vez com `qsort`. **Trocar critério de ordenação = trocar um ponteiro.**
Instantâneo, resolvendo R-04.

### 2.4 Tabela de gêneros

Enum fixo para permitir filtro por bitmask e evitar strings duplicadas:

```c
enum rh_genre {
    RH_GENRE_UNKNOWN = 0, RH_GENRE_ACTION,    RH_GENRE_ADVENTURE,
    RH_GENRE_RPG,         RH_GENRE_SHOOTER,   RH_GENRE_PLATFORM,
    RH_GENRE_RACING,      RH_GENRE_FIGHTING,  RH_GENRE_SPORTS,
    RH_GENRE_PUZZLE,      RH_GENRE_STRATEGY,  RH_GENRE_SIMULATION,
    RH_GENRE_HORROR,      RH_GENRE_MUSIC,     RH_GENRE_PARTY,
    RH_GENRE_COUNT
};
```

O nome exibido vem do sistema de idiomas (`lang.c`), então o gênero é traduzível de graça.

### 2.5 Categorias da tela inicial

```c
enum rh_category {
    RH_CAT_PS2 = 0,     // jogos PS2 (CD/DVD/UL/ZSO/HDL)
    RH_CAT_PS1,         // jogos PS1 (VCD/POPS, se presente)
    RH_CAT_EMULATOR,    // homebrew marcado como emulador
    RH_CAT_HOMEBREW,    // demais homebrew (APPS/)
    RH_CAT_COUNT
};
```

`Favoritos` e `Recentes` **não são categorias** — são *visões* sobre todas as categorias
(máscara de flag e ordenação por `lastPlayed`).

### 2.6 Metadados por jogo — extensão de `CFG/<startup>.cfg`

O OPL já lê `Title`, `Genre`, `Release`, `Developer`, `Description` como `AttributeText`
(`misc/conf_theme_OPL.cfg:88-123`). Só faltam campos. Novas chaves com prefixo `$RH_`:

```ini
# --- Já existentes no OPL (reusar como estão) ---
Title=Shadow of the Colossus
Genre=Adventure
Release=2005
Developer=Team Ico
Description=Um jovem viaja a uma terra proibida...
Rating=5

# --- Novas do RetroHub ---
$RH_Players=1
$RH_Region=NTSC-U
$RH_Compat=5
$RH_Category=0
$RH_Favorite=1
$RH_LastPlayed=1754308800
$RH_PlayCount=17
$RH_GenreId=2
```

**Compatibilidade:** o OPL original ignora chaves desconhecidas (`config.c` é um dicionário
genérico). Um cartão preparado pelo RetroHub continua funcionando no OPL padrão, e vice-versa.
Isso satisfaz RI-4.

### 2.7 Favoritos e recentes

Dois arquivos pequenos e **descartáveis** em `RH/`:

```
RH/favorites.cfg    startup=1  (uma linha por favorito)
RH/recent.cfg       startup=timestamp  (máximo 32 entradas, LRU)
```

Motivo de não gravar no `.cfg` do jogo a cada partida: escrita em FAT é o maior risco de corrupção
(R-06) e o `.cfg` do jogo pode estar num dispositivo somente-leitura. Estes dois arquivos são
regeneráveis e a perda deles é cosmética.

### 2.8 Invalidação do índice

`sourceHash` no cabeçalho combina, por dispositivo:
- `st_mtime` de `CD/` e `DVD/`
- tamanho de `ul.cfg`
- contagem de arquivos

É exatamente a informação que `bdmNeedsUpdate()` já coleta (`bdmsupport.c:151-253`) — nenhum I/O
adicional. Se o hash difere, reconstrói o índice em background; até lá, usa o índice antigo.

**Se o índice estiver ausente ou corrompido, o RetroHub cai no caminho clássico do OPL**
(`sbReadList` + `sbPopulateConfig`) e funciona normalmente, apenas mais devagar. Isso satisfaz RI-8.

---

## 3. Arquitetura da interface

### 3.1 Máquina de estados de telas

Estende os `screenHandler`s existentes (`gui.h:56-60`), sem remover nenhum:

```
        ┌──────────────┐
        │   INTRO      │  (guiIntroLoop, logo + som de boot)
        └──────┬───────┘
               ▼
        ┌──────────────┐   START    ┌──────────────┐
        │  HUB (novo)  │───────────▶│  SETTINGS    │
        │  categorias  │◀───────────│  (MENU)      │
        └──────┬───────┘            └──────────────┘
               │ ✕
               ▼
        ┌──────────────┐   □        ┌──────────────┐
        │  GRID (novo) │───────────▶│  DETAIL      │
        │  capas       │◀───────────│  (INFO)      │
        └──┬────┬──────┘            └──────────────┘
           │    │ △                 ┌──────────────┐
           │    └──────────────────▶│  GAME_MENU   │
           │ SELECT                 └──────────────┘
           ▼
        ┌──────────────┐
        │ SEARCH (novo)│
        └──────────────┘
```

Telas novas: `GUI_SCREEN_HUB`, `GUI_SCREEN_GRID`, `GUI_SCREEN_SEARCH`.
Telas preservadas: `MAIN` (lista clássica, disponível como modo de visualização alternativo),
`MENU`, `INFO`, `GAME_MENU`, `APP_MENU`.

### 3.2 Tela inicial (Hub)

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│   RetroHub PS2                                    ⌚ 14:32     │
│                                                                │
│   ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐                  │
│   │        │ │        │ │        │ │        │                  │
│   │  PS2   │ │  PS1   │ │ EMULA- │ │ HOME-  │                  │
│   │        │ │        │ │ DORES  │ │ BREW   │                  │
│   │  428   │ │   61   │ │    7   │ │   23   │                  │
│   └────────┘ └────────┘ └────────┘ └────────┘                  │
│                                                                │
│   ┌────────┐ ┌────────┐ ┌────────┐                             │
│   │ FAVO-  │ │ RECEN- │ │ CONFI- │                             │
│   │ RITOS  │ │  TES   │ │ GURAÇÕ.│                             │
│   │   19   │ │    8   │ │        │                             │
│   └────────┘ └────────┘ └────────┘                             │
│                                                                │
│   USB ●   HDD ●   MX4SIO ○   SMB ○           ✕ Abrir  ▲ Opções │
└────────────────────────────────────────────────────────────────┘
```

**Custo de renderização:** 7 tiles + 7 rótulos + 7 contadores + barra de status ≈ **30 primitivas**.
Bem dentro do orçamento de 80 (R-03).

**Custo de inicialização:** os contadores vêm do cabeçalho do índice. **A tela inicial não precisa
ler nenhum jogo.** Aparece assim que o índice é lido — sub-segundo. Satisfaz R-10.

Indicadores de dispositivo (`USB ● HDD ●`) vêm de `menuItem.visible` de cada `item_list_t` — dado
já mantido pelo OPL.

### 3.3 Tela de jogos (grade de capas)

```
┌────────────────────────────────────────────────────────────────┐
│  ← PS2                              🔍 Busca      Nome ▼   428 │
│ ┌──────────────────────────────────┐ ┌───────────────────────┐ │
│ │ ┏━━━━━┓  ┌─────┐  ┌─────┐  ┌───┐ │ │ Shadow of the Colossus│ │
│ │ ┃     ┃  │     │  │     │  │   │ │ │                       │ │
│ │ ┃CAPA ┃  │     │  │     │  │   │ │ │ 2005 · Team Ico       │ │
│ │ ┃FOCO ┃  │     │  │     │  │   │ │ │ Aventura · 1 jogador  │ │
│ │ ┗━━━━━┛  └─────┘  └─────┘  └───┘ │ │ NTSC-U · SCUS_974.72  │ │
│ │                                  │ │                       │ │
│ │ ┌─────┐  ┌─────┐  ┌─────┐  ┌───┐ │ │ Compat.  ★★★★★        │ │
│ │ │     │  │     │  │     │  │   │ │ │ Jogado   há 2 dias    │ │
│ │ │     │  │     │  │     │  │   │ │ │ ★ Favorito            │ │
│ │ └─────┘  └─────┘  └─────┘  └───┘ │ │                       │ │
│ └──────────────────────────────────┘ └───────────────────────┘ │
│  ✕ Jogar   ▲ Opções   ■ Detalhes   ● Voltar   SELECT Busca     │
└────────────────────────────────────────────────────────────────┘
```

**Decisão de projeto crítica:** existe **uma única textura de capa por jogo** (192×276, T8). O
painel de detalhes a desenha em tamanho nativo; a grade desenha a mesma textura reduzida pelo GS
em hardware (~128×184). Isso resolve R-02 (RAM) e R-09 (VRAM) e, principalmente, elimina a
complexidade de dois caches e quatro estados de carregamento — ver
[`06-decisao-capas.md`](06-decisao-capas.md).

Contagem de primitivas: 8 capas reduzidas + 1 capa em foco + moldura + ~10 textos + cabeçalho + rodapé
≈ **35 primitivas**. Confortável.

**Painel lateral** exibe todos os campos exigidos: capa (a em foco), nome, ano, desenvolvedora,
gênero, número de jogadores, região, código do disco, compatibilidade, última vez jogado, favorito.

### 3.4 Busca

- SELECT abre um campo de busca com o teclado virtual existente (`guiShowKeyboard`, `gui.c:955`)
- Filtro **incremental por prefixo e por substring**, aplicado sobre `orderByName`
- **Debounce de 250 ms** — não refiltra a cada tecla (R-03)
- O filtro produz uma máscara de bits (`u32 mask[count/32]`), nunca uma nova lista — sem alocação
  durante a digitação

### 3.5 Ordenação e filtros

Barra superior da grade, alternável com L1/R1:

| Ordenação | Fonte |
|---|---|
| Nome | `orderByName` |
| Ano | `orderByYear` |
| Gênero | `orderByGenre` |
| Desenvolvedora | `orderByDeveloper` |

Filtros combináveis (máscara): categoria, gênero, região, favoritos, "não jogados".

### 3.6 Modo de visualização

Configurável, para respeitar tanto o público quanto o hardware:

| Modo | Capas visíveis | Custo | Quando |
|---|---|---|---|
| **Grade** (padrão) | 8 reduzidas + 1 em foco | ~35 prims | Uso normal |
| **Lista + capa** | 1 capa | ~25 prims | Bibliotecas gigantes, consoles lentos |
| **Clássico OPL** | conforme o tema | — | Compatibilidade / preferência |

---

## 4. Extensões do sistema de temas

Aditivas, opt-in, sem quebrar tema legado (RI-5).

### 4.1 Novos tipos de elemento

| Tipo | Descrição | Propriedades |
|---|---|---|
| `CoverGrid` | Grade de capas | `cols`, `rows`, `cover_w`, `cover_h`, `gap_x`, `gap_y`, `focus_scale`, `pattern`, `count` |
| `GameCard` | Painel de detalhes | `fields` (lista ordenada), `line_height`, `label_color` |
| `TabBar` | Barra de categorias | `orientation`, `item_w`, `item_h`, `icon_size` |
| `HubTile` | Tile da tela inicial | `index`, `icon`, `label_id`, `show_count` |
| `SearchBox` | Campo de busca | `placeholder_id`, `max_len` |
| `StatusBar` | Barra de dispositivos/relógio | `show_devices`, `show_clock` |
| `AttributeBadge` | Selo (região, jogadores, compat.) | `attribute`, `style` |

Implementação: cada um é `init<X>()` + `draw<X>()` + uma entrada na tabela de despacho —
exatamente o padrão já usado por `initItemsList`/`drawItemsList` (`themes.c:840-905`).

### 4.2 Tabela de despacho

Substituir a cadeia de `else if` (`themes.c:1023-1082`) por:

```c
static const struct {
    const char *name;
    void (*init)(const char *themePath, config_set_t *cfg, theme_t *thm,
                 theme_element_t *elem, const char *name);
} rh_element_registry[] = {
    { "CoverGrid",      initCoverGrid      },
    { "GameCard",       initGameCard       },
    { "TabBar",         initTabBar         },
    { "HubTile",        initHubTile        },
    { "SearchBox",      initSearchBox      },
    { "StatusBar",      initStatusBar      },
    { "AttributeBadge", initAttributeBadge },
    { NULL, NULL }
};
```

O parser tenta primeiro os tipos originais do OPL, depois este registro. Tema legado nunca alcança
o registro novo → comportamento idêntico garantido.

### 4.3 Novas seções de tela

```
hubN:      elementos da tela inicial
gridN:     elementos da grade de capas
searchN:   elementos da busca
```

Prefixos `mainN`, `infoN`, `appsMainN`, `appsInfoN` permanecem intactos.

### 4.4 Tema legado

Um tema sem seções `hub*`/`grid*` recebe automaticamente o layout embutido do RetroHub para essas
telas, e mantém seu próprio layout nas telas clássicas. Nenhum tema quebra.

---

## 5. Identidade visual

Requisito explícito: *"não copiar o PS5; identidade própria inspirada em consoles modernos e
front-ends retrô"*.

### 5.1 Direção

- **Grade sobre lista.** A capa é o objeto principal; o texto é apoio.
- **Foco por escala e contraste**, não por brilho ou borda grossa. A capa em foco cresce ~15% e as
  demais escurecem. É barato: uma variação de tamanho no `rmDrawPixmap` e um `rmDrawRect` com alfa.
- **Tipografia como estrutura.** Uma família, três pesos visuais obtidos por tamanho e cor.
- **Espaço negativo generoso.** Baixa densidade transmite modernidade e reduz draw calls — os dois
  objetivos alinhados.
- **Cor com propósito.** Uma cor de destaque por tema, aplicada a foco e ações. Fundo neutro escuro.
- **Movimento sóbrio.** Transições de 12-16 frames com easing. Nada de paralaxe ou partículas.

### 5.2 Grid de layout

Sobre o espaço virtual 640×480:
- Margem externa: 32 px (seguro para overscan de TVs CRT)
- Coluna base: 8 px · Calha: 16 px
- Altura de linha de texto: 22 px (contra os 19 px do OPL clássico — mais respiro)
- Área segura de título: 576×416

### 5.3 Paleta padrão ("Midnight")

| Papel | Cor |
|---|---|
| Fundo | `#0E1116` |
| Superfície | `#171B22` |
| Superfície elevada | `#212733` |
| Texto primário | `#E6EAF0` |
| Texto secundário | `#8B94A3` |
| Destaque | `#4CC2FF` |
| Favorito | `#FFC857` |
| Sucesso / compat. alta | `#5FD68A` |
| Alerta / compat. baixa | `#F2725A` |

Temas alternativos previstos: "Aurora" (claro), "Retro CRT" (âmbar sobre preto),
"Slate" (neutro, alto contraste).

### 5.4 Ícones

Traço uniforme de 2 px no espaço 640×480, grade de 24×24 e 48×48, sem gradientes.
Formato: PNG paletizado (economiza VRAM e casa com o traço plano).

---

## 6. Áudio

Reusa integralmente `sound.c` (audsrv), que já suporta SFX em ADPCM e BGM em Ogg Vorbis.

| Recurso | Como |
|---|---|
| Sons de navegação | `SFX_CURSOR`, `SFX_CONFIRM`, `SFX_CANCEL`, `SFX_TRANSITION` — já existem |
| Som de boot | `SFX_BOOT`, já sincronizado com o fade da intro (`gui.c:1531-1537`) |
| Música de fundo | `bgmStart()` / `bgmStop()`, caminho em `gDefaultBGMPath` |
| Volumes independentes | `gSFXVolume`, `gBootSndVolume`, `gBGMVolume` — já existem |

**Trabalho necessário:** substituir os 8 arquivos `.adp` por um conjunto novo com identidade
própria, e adicionar 2-3 sons (foco na grade, erro, favoritar). Nenhuma mudança de código.

---

## 7. Orçamentos de execução

Contratos verificáveis, derivados de R-02, R-03, R-09, R-10.

| Recurso | Orçamento | Como medir |
|---|---|---|
| Heap da UI | ≤ 8 MB | Contador em build de debug, exibido no rodapé |
| Primitivas/frame | ≤ 80 | Contador `order` de `renderman.c` |
| FPS | 50 (PAL) / 60 (NTSC) estáveis | Medidor existente (`gui.c:56-58`) |
| Tempo até a tela inicial | ≤ 3 s | Cronômetro do boot |
| Ordenação de 1.000 jogos | ≤ 100 ms | Instrumentação de `qsort` |
| Busca incremental | ≤ 30 ms por tecla | Instrumentação |
| Capa (única) | 192×276, T8 (53 KB) | Validação no carregamento |
| Cache de capas | 24 entradas (1,27 MB) | Configuração do tema |

---

## 8. Estratégia de capas

> **Decisão fechada em [`06-decisao-capas.md`](06-decisao-capas.md).** Esta seção é o resumo.

### 8.1 Um único arquivo

```
ART/<startup>_COV.png    192×276  PNG paletizado 8 bits  (~53 KB em RAM)
```

**Não existe arquivo de miniatura.** A grade desenha essa mesma textura reduzida pelo GS em
hardware (~128×184, escala 0,67×); o painel de detalhes a desenha no tamanho nativo.

Um arquivo só elimina dois caches, quatro estados de carregamento e a transição visual entre
miniatura e capa — a simplificação que mais contribui para a fluidez.

### 8.2 PNG paletizado

`textures.c` já decodifica PNG de 8 bits para `GS_PSM_T8` + CLUT de 1.024 B (`texReadPixels8`,
`textures.c:317`; alocação em `textures.c:518-522`). O caminho de 8 bits é o mais barato do
arquivo — um `memcpy` por linha, sem processamento por pixel.

Ganho: **3–4× em RAM e VRAM** contra uma capa RGB do mesmo tamanho, **sem código novo**.

Capas RGB legadas continuam funcionando, apenas mais pesadas (regra RI-8).

### 8.3 Política de cache

Estende `texcache.c` sem alterar sua semântica (R-01/zona amarela):
- **Um cache, 24 entradas ≈ 1,27 MB** (cobre 3 páginas de grade: atual, anterior e próxima)
- Pré-carregamento direcional: ao mover o foco, enfileira a próxima capa na direção do movimento
- `guiInactiveFrames` continua governando quando é seguro carregar — o anti-thrash existente é
  exatamente o que uma grade precisa
- Placeholder de tamanho fixo quando a capa não existe: o layout nunca "pula"

---

## 9. Sequência de inicialização proposta

```
 0 ms   reset() → sysReset, módulos base            [inalterado]
        ↓
        init() → pad, config, renderman, lang,      [inalterado]
                 themes, gui, ioman, menusys
        ↓
        guiIntroLoop() — logo + som de boot          [inalterado]
        ↓
+300ms  RH: lê RH/library.idx do dispositivo padrão  ← NOVO, rápido
        ↓
+500ms  ►► TELA INICIAL VISÍVEL (contadores do índice)
        ↓
        (em background, via ioman)
        - enumera dispositivos                       [inalterado]
        - valida sourceHash de cada dispositivo
        - se divergiu: reconstrói índice e atualiza contadores
        - carrega capas sob demanda
```

O usuário vê a tela inicial em meio segundo. Tudo o mais acontece atrás, e a UI se atualiza via
`guiDeferUpdate` — o mecanismo assíncrono que o OPL já possui.

---

## 10. Estratégia de integração com o upstream

| Prática | Objetivo |
|---|---|
| Fork com `master` espelhando o upstream | `git merge upstream/master` sempre limpo |
| Desenvolvimento em `retrohub/*` | Isolamento |
| Código novo em arquivos novos (`rh_*.c`) | Conflitos quase nulos |
| Mudanças em arquivos existentes: pequenas, localizadas, comentadas com `/* RetroHub: ... */` | Merge previsível |
| Sincronização mensal com o upstream | Não perder correções de compatibilidade |
| `DIVERGENCIAS.md` listando cada alteração em arquivo do OPL | Auditoria |

**Contribuir de volta:** melhorias genuinamente genéricas (`qsort` no lugar do bubble sort, tabela
de despacho de temas, correção do semáforo em `ioRegisterHandler`) devem virar PRs para o OPL
upstream. Reduz a divergência e beneficia todo mundo.

---

## 11. O que este documento deliberadamente não decide

Pontos que precisam de validação prática antes de fechar:

1. **Grade 4×2 vs 4×3 vs 5×3** — depende do teste de legibilidade em CRT real e do custo medido.
2. **Se a busca precisa de índice invertido** — para ≤ 2.000 jogos, varredura linear sobre a
   tabela de strings provavelmente basta.
3. **Se `RH/` fica por dispositivo ou centralizado** — por dispositivo é mais robusto (pendrive
   removível carrega seu próprio índice); centralizado é mais simples.
4. **PS1 via POPS** — depende de o usuário ter POPS instalado; a categoria pode simplesmente ficar
   vazia se não houver.
5. **Detecção automática de "emulador" vs "homebrew"** — provavelmente exige uma lista curada ou
   marcação manual em `$RH_Category`.

Cada um desses vira uma decisão registrada durante a fase correspondente do plano.
