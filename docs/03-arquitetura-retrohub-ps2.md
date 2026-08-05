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

### 3.3 Tela de jogos (grade de capas) — **visualização principal**

A grade é o modo padrão do RetroHub. A lista clássica do OPL continua disponível como modo
alternativo, mas não é o caminho principal.

#### Geometria (padrão do tema)

**Alvo primário: adaptador HDMI em TV moderna.** Isso muda duas coisas em relação a um projeto
pensado para tubo: não há overscan cortando as bordas, e a tela é 16:9.

Margem de segurança de **16 px** (em vez dos 32 px que um CRT exigiria), cabeçalho de 36 px e barra
de dicas de 28 px deixam **608 × 384** para o conteúdo.

| Parâmetro | Valor |
|---|---|
| Slot da capa | **128 × 184** |
| Gap | 12 px |
| Escala da textura (192×276 → 128×184) | **0,67×** |
| Grade em **4:3** | 3 colunas × 2 linhas = **6 capas** · painel 184 px |
| Grade em **16:9** | 4 colunas × 2 linhas = **8 capas** · painel 246 px |

#### A grade se adapta ao aspecto — de graça

O widescreen do OPL é **anamórfico**: `rmSetupQuad` multiplica larguras por 3/4 em 16:9, mas
**não escala posições** (`renderman.c:271-289`). Consequência prática: em 16:9 a mesma capa ocupa
96 unidades de coordenada em vez de 128, e **sobra espaço horizontal para uma coluna a mais**.

```
4:3   ├─3 colunas─┤├painel┤     6 capas · painel 184
      408 coord    184

16:9  ├──4 colunas──┤├painel┤   8 capas · painel 246
      411 coord      185
```

Mesmo tamanho visual de capa, mesma escala de 0,67×, mesma linha de código. `CoverGrid` expõe
`cols_4_3` e `cols_16_9` e escolhe conforme `gWideScreen`.

#### Por que não uma grade mais densa

A densidade tem um teto técnico. Com o conteúdo de 608×384 e duas linhas, o slot é de 184 px de
altura — o que fixa a capa em 128 px de largura pela proporção da caixa de PS2.

| Grade | Painel restante | Veredito |
|---|---|---|
| **4:3 · 3 col** | 184 px | **Escolhido** |
| 4:3 · 4 col | 44 px | Painel inutilizável |
| **16:9 · 4 col** | 246 px | **Escolhido** |
| 16:9 · 5 col | 106 px | Painel inutilizável |
| 3 linhas (qualquer) | — | Slot cai para ~110 px → escala **0,43×** |

**O limite duro é a escala de 0,5×.** O GS reduz com bilinear de apenas 2×2 amostras; abaixo de
metade do tamanho original ele pula texels e a imagem serrilha — e numa tela LCD nítida isso
aparece muito mais do que apareceria num tubo. Três linhas de capas exigiriam texturas de origem
menores, contradizendo o requisito de capas grandes.

#### Recomendação de modo de vídeo: 480p

Com adaptador HDMI, vale usar `GS_MODE_DTV_480P` (`renderman.c:41`) em vez do 480i padrão:

- **Sem flicker de entrelaçamento.** Em 480i, linhas horizontais de 1 px tremem. Em 480p não.
  Isso libera separadores finos, bordas de 1 px e texto menor.
- **Nitidez real**, sem o `fPAR *= 2.0f` do modo interlaced frame (`renderman.c:400-403`).

Continua sendo escolha do usuário — a lista de 14 modos do OPL é preservada intacta.

#### E quem ainda usa tubo?

Não é abandonado, e não custa nada: basta o tema declarar margem de 32 px. Nessa configuração a
grade vira 3×2 em 4:3 (slot 118×170, escala 0,61×) e 4×2 em 16:9. **Uma linha de configuração de
tema, zero código condicional.**

#### Foco sem tremor de layout

O slot tem tamanho fixo. O que muda entre focado e não focado é o preenchimento dele:

- **Focado:** a capa preenche o slot inteiro (128×184), sem escurecimento
- **Não focado:** desenhada a 92% centrada no slot (118×169), com `rmDrawRect` escuro por cima

Isso dá diferença de escala **e** de contraste sem nunca reorganizar a grade. Nada se desloca
quando o foco muda.

#### Divisão de informação

O requisito lista 11 campos por jogo. Eles não cabem — nem deveriam — todos no painel de 182 px:

| Onde | Campos |
|---|---|
| **Painel da grade** (navegação) | Capa, nome, ano, desenvolvedora, gênero, favorito |
| **Tela de detalhes** (□) | Os 11 campos completos, incluindo jogadores, região, código do disco, compatibilidade e última vez jogado |

A tela de detalhes já existe no OPL (`GUI_SCREEN_INFO`) e é reaproveitada.



```
   16   ┌───────────────────────────────────────────────────────┐
        │ < PS2                        Busca    Nome v      428 │ 36
   52   ├──────────────────────────────────────┬────────────────┤
        │ #########  +-------+  +-------+      │ +------------+ │
        │ #       #  |       |  |       |      │ |            | │
        │ #  FOCO #  |  92%  |  |  92%  |      │ |    CAPA    | │
        │ # 128x184  | +dim  |  | +dim  |      │ |   176x253  | │
        │ #########  +-------+  +-------+      │ |            | │
        │ +-------+  +-------+  +-------+      │ +------------+ │
        │ |       |  |       |  |       |      │ Shadow of the  │
        │ |       |  |       |  |       |      │ Colossus       │
        │ |       |  |       |  |       |      │ 2005 Team Ico  │
        │ +-------+  +-------+  +-------+      │ Aventura     * │
  436   ├──────────────────────────────────────┴────────────────┤
        │ X Jogar  /\ Opcoes  [] Detalhes  O Voltar  SEL Buscar │ 28
  464   └───────────────────────────────────────────────────────┘
          |<----------- 408 ------------>|<------ 184 ------->|

    Em 16:9 entra uma quarta coluna no mesmo espaco (411 coord),
    e o painel cresce para 246 visual. Mesma capa, mesma escala.
```

**Uma única textura de capa por jogo** (192×276, T8) serve os dois usos: o painel a desenha a
176×253 (0,92×) e a grade a 128×184 (0,67×), ambas reduções feitas pelo GS em hardware. Isso
resolve R-02 (RAM) e R-09 (VRAM) e elimina a complexidade de dois caches e quatro estados de
carregamento — ver [`06-decisao-capas.md`](06-decisao-capas.md).

**Contagem de primitivas:** 6–8 capas da grade + 5–7 retângulos de escurecimento + 1 capa em foco
+ 2 da moldura + ~8 textos + cabeçalho + rodapé ≈ **28–34 primitivas**. Bem dentro do teto de 80.

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
| **Grade** (padrão) | 6–8 reduzidas + 1 em foco | ~28–34 prims | Uso normal |
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
- Margem externa: 16 px (alvo HDMI/LCD, sem overscan) · 32 px no perfil CRT opcional
- Coluna base: 8 px · Calha: 16 px
- Altura de linha de texto: 22 px (contra os 19 px do OPL clássico — mais respiro)
- Área útil: 608×448 (HDMI) · 576×416 (CRT)

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
hardware (128×184, escala 0,67×); o painel a desenha a 176×253 (0,92×).

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
- **Um cache, 24 entradas ≈ 1,27 MB** (cobre 3 páginas em 16:9, 4 em 4:3)
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

1. ~~Densidade da grade~~ — **decidido: 3×2 em 4:3, 4×2 em 16:9**, slot 128×184. O limite é a
   escala de redução de 0,5× do GS. Resta validar num painel real via adaptador HDMI se a margem
   de 16 px não é cortada por alguma TV com overscan forçado.
2. **Se a busca precisa de índice invertido** — para ≤ 2.000 jogos, varredura linear sobre a
   tabela de strings provavelmente basta.
3. **Se `RH/` fica por dispositivo ou centralizado** — por dispositivo é mais robusto (pendrive
   removível carrega seu próprio índice); centralizado é mais simples.
4. **PS1 via POPS** — depende de o usuário ter POPS instalado; a categoria pode simplesmente ficar
   vazia se não houver.
5. **Detecção automática de "emulador" vs "homebrew"** — provavelmente exige uma lista curada ou
   marcação manual em `$RH_Category`.

Cada um desses vira uma decisão registrada durante a fase correspondente do plano.
