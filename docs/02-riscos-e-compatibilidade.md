# Riscos, Limites e Regras de Compatibilidade — RetroHub PS2

> Documento de análise de risco. Derivado de [`01-analise-arquitetura-opl.md`](01-analise-arquitetura-opl.md).
> Nenhum código foi escrito ainda.

---

## 1. Matriz de riscos

Severidade: 🔴 crítico · 🟠 alto · 🟡 médio · 🟢 baixo

### R-01 🔴 Quebra da compatibilidade de jogos

**Descrição.** O que faz um jogo rodar não é a interface, é o conjunto
`ee_core` + `cdvdman` + `sbPrepare()` + `sysLaunchLoaderElf()`. Qualquer alteração — mesmo
aparentemente inofensiva, como reordenar campos de uma struct compartilhada — pode transformar
"98% dos jogos funcionam" em "nada roda", e o sintoma será uma tela preta sem log.

**Probabilidade.** Média (a tentação de "só ajustar aqui" é constante).
**Impacto.** Fatal para o projeto.

**Mitigação.**
1. Declarar `ee_core/`, `modules/`, `system.c`, `supportbase.c:sbPrepare` como **congelados**.
2. CI que falha se um commit tocar esses caminhos sem a label explícita `engine-change`.
3. Nunca alterar: `struct cdvdman_settings_common`, `struct cdvdman_settings_bdm`,
   `struct EECoreConfig_t`, `bd_fragment_t`, `BDM_MAX_FRAGS`, `USBExtreme_game_entry_t`.
4. Suite de regressão em hardware real com pelo menos 20 jogos cobrindo: CD, DVD SL, DVD DL, ZSO,
   UL multi-parte, jogo com VMC, jogo com cheats, jogo com GSM.

---

### R-02 🔴 Estouro de memória (32 MB)

**Descrição.** O requisito "capas grandes" é diretamente antagônico ao orçamento de RAM. Uma capa
512×512 em `GS_PSM_CT24` ocupa **768 KB** em RAM principal. Uma grade de 4×3 = 12 capas visíveis,
com cache de 16 entradas, consome **12 MB** — mais de um terço da RAM do console, antes de contar
lista de jogos, atlas de fontes, módulos IRX embutidos (~2 MB) e o próprio código.

**Probabilidade.** Alta se não houver orçamento explícito.
**Impacto.** Travamentos aleatórios, difíceis de reproduzir.

**Mitigação.** Resolvida em [`06-decisao-capas.md`](06-decisao-capas.md).
1. **Orçamento fixo declarado**: a UI não pode passar de **8 MB** de heap dinâmico.
2. **Capas em PNG paletizado de 8 bits** (`GS_PSM_T8` + CLUT de 1.024 B). `textures.c` já suporta
   (`texReadPixels8`, `textures.c:317`). Custo: 1/3 a 1/4 de uma capa RGB.
3. **Resolução única: 192×276** — sem arquivo de miniatura. Uma capa = **53 KB**.
4. Contador de heap em builds de debug, exibido no rodapé.
5. Rejeitar PNG acima de **256 KB** antes do `malloc` do buffer de arquivo (`textures.c:430`, hoje
   sem limite) e capas acima do limite de dimensão (`maxSize`, `textures.c:104`).

**Tabela de orçamento:**

| Item | Orçamento |
|---|---|
| Cache de capas (24 × 192×276 T8) | 1,27 MB |
| Cache de ícones (20 × 64×64 T8) | 0,1 MB |
| Fundo estático 640×480 T8 | 0,3 MB |
| Atlas de fontes (4 × 256×256 CT32) | 1,0 MB |
| Índice de biblioteca (2.000 jogos) | 1,0 MB |
| Nós de menu / submenu (2.000) | 0,3 MB |
| Pico transitório de decodificação | 0,15 MB |
| Folga operacional | 3,9 MB |
| **Total UI** | **8,0 MB** |

---

### R-03 🟠 Queda de desempenho / perda de fluidez

**Descrição.** O EE roda a 294 MHz sem cache de dados generoso. `guiMainLoop` dorme em VSync;
tudo que a UI faz precisa caber em **16,7 ms (NTSC)** ou **20 ms (PAL)**. Desenhar 12 capas com
overlay, mais textos, mais um fundo animado, mais busca incremental, pode estourar o frame e
derrubar para 30 fps.

**Probabilidade.** Alta.
**Impacto.** Contradiz o requisito central de "fluidez".

**Mitigação.**
1. **Orçamento de draw calls**: máximo **80 primitivas/frame**. Cada `rmDrawPixmap` é uma
   primitiva; `rmDrawOverlayPixmap` são duas.
2. Desativar o plasma Perlin sempre que houver `Background` estático (é CPU pura, `gui.c:1269`).
3. Ordenação incremental em vez de bubble sort (ver R-04).
4. Busca com debounce: só refiltra depois de 250 ms sem digitação.
5. Contador de FPS visível em debug (já existe, `gui.c:56-58`).
6. Nunca fazer I/O no frame — tudo via `ioPutRequest`.

---

### R-04 🟠 Escalabilidade com bibliotecas grandes

**Descrição.** `submenuSort()` é bubble sort O(n²) sobre lista encadeada com `strcmp`
(`menusys.c:561`). Com 1.000 jogos são ~500.000 comparações de string por ordenação — segundos de
congelamento. E `sbReadList()` reconstrói a lista inteira a cada refresh. Ordenar por 4 critérios
(nome/ano/gênero/desenvolvedora) multiplica o problema.

**Probabilidade.** Certa, para o público-alvo (HDs de 1-2 TB com centenas de jogos).
**Impacto.** UI inutilizável exatamente para quem mais quer o produto.

**Mitigação.**
1. Substituir lista encadeada por **array de ponteiros** + `qsort` da libc → O(n log n).
2. **Índice persistente** (`retrohub.idx`) construído uma vez pelo RetroHub Manager ou no primeiro
   boot, contendo metadados já parseados. Boots seguintes só leem o índice.
3. Manter **índices pré-ordenados** (4 arrays de `u16`), reconstruídos apenas quando a biblioteca
   muda. Trocar ordenação vira troca de ponteiro: instantânea.
4. Filtragem por categoria/favorito como máscara de bits, não como nova lista.

---

### R-05 🟠 Regressão do sistema de temas

**Descrição.** Existem centenas de temas de OPL feitos pela comunidade. Adicionar tipos de
elemento é seguro; **mudar o espaço virtual 640×480, a semântica de coordenadas negativas ou
renomear tipos existentes quebra todos eles**.

**Probabilidade.** Média.
**Impacto.** Perda de acervo e de boa vontade da comunidade.

**Mitigação.**
1. Regra: **só adicionar** tipos de elemento e propriedades. Nunca renomear, nunca remover.
2. Manter `renderman` com o espaço 640×480 intocado.
3. Um tema legado de OPL deve continuar renderizando idêntico. Testar com 5 temas populares.
4. Novos elementos (`CoverGrid`, `Card`, `TabBar`) são opt-in: tema que não os declara continua
   usando `ItemsList`.

---

### R-06 🟠 Corrupção de dados do usuário

**Descrição.** Escrever no `ul.cfg`, renomear jogos, gravar VMC ou reescrever `.cfg` em um
pendrive FAT tem risco real de corrupção — especialmente com perda de energia. O OPL já é
conservador aqui (`gEnableWrite` é opt-in) e verifica cadeias de clusters
(`USBMASS_IOCTL_CHECK_CHAIN`) antes de usar VMC.

**Probabilidade.** Baixa se as regras forem seguidas.
**Impacto.** Perda de saves e de biblioteca.

**Mitigação.**
1. Novos metadados (favoritos, recentes, contadores) vão para **arquivos separados**, nunca
   sobrescrevendo `ul.cfg` nem os `.cfg` de jogo existentes de forma destrutiva.
2. Escrita atômica: gravar `arquivo.tmp` → `fsync` → renomear.
3. Manter `gEnableWrite` como opt-in para operações destrutivas.
4. O índice `retrohub.idx` deve ser **descartável**: se corromper ou faltar, reconstruir do zero
   sem perda.

---

### R-07 🟡 Fragmentação de ISOs

**Descrição.** Limite rígido de **64 fragmentos** (`BDM_MAX_FRAGS`, `cdvd_config.h:65`). Não é um
risco introduzido pelo RetroHub, mas é a causa nº 1 de "o jogo não abre" — e o RetroHub Manager
pode e deve preveni-lo.

**Mitigação.**
1. O Manager verifica fragmentação **na hora de copiar** e avisa.
2. Mensagem de erro no PS2 mais clara que a atual, indicando desfragmentação/recópia.
3. Documentar: formatar com clusters grandes, copiar ISOs para disco vazio.

---

### R-08 🟡 Divergência do upstream

**Descrição.** O OPL continua evoluindo (correções de compatibilidade, novos dispositivos). Um
fork que reescreve a UI inteira tende a divergir e perder essas correções.

**Probabilidade.** Alta ao longo do tempo.
**Impacto.** Perda progressiva de compatibilidade.

**Mitigação.**
1. **Não fazer fork com reescrita espalhada.** Manter os arquivos de motor **byte-idênticos** ao
   upstream para que `git merge` seja trivial.
2. Concentrar o código novo em arquivos novos (`src/rh_*.c`) em vez de reescrever os existentes.
3. Rotina mensal de sincronização com o upstream.
4. Documentar cada divergência intencional.

---

### R-09 🟡 Consumo de VRAM (4 MB)

**Descrição.** VRAM abriga: framebuffer duplo, atlas de fontes, e todas as texturas ligadas no
frame. Em 640×448 CT24 o framebuffer duplo já consome ~2,3 MB. Sobram ~1,7 MB para texturas.

**Probabilidade.** Alta com capas grandes.
**Impacto.** Artefatos gráficos, texturas piscando.

**Mitigação.**
1. Texturas paletizadas (T8) — mesma economia do R-02, agora em VRAM.
2. Confiar no `gsKit_TexManager` (já faz expulsão por frame). Com capa única de 192×276 em T8, as
   9 texturas visíveis somam ~477 KB de VRAM — cabe folgado no espaço restante após o framebuffer
   duplo.
3. Considerar `GS_PSM_CT16` para capas quando em modo HIRES.

---

### R-10 🟡 Tempo de inicialização

**Descrição.** Requisito explícito é "inicialização rápida". Hoje o boot faz: reset do IOP, carga
de módulos, varredura de dispositivos, montagem de ISOs para ler `SYSTEM.CNF`, carga de temas e
idiomas de todos os dispositivos. Adicionar carregamento de metadados e capas pode piorar.

**Mitigação.**
1. **Índice persistente** elimina a varredura de ISOs (maior custo do primeiro boot).
2. Carregamento **lazy** — nenhuma capa é lida antes da tela inicial aparecer.
3. A tela inicial (hub de categorias) não precisa de dados de jogo: pode aparecer em <1 s.
4. Varredura de dispositivos continua assíncrona (já é, via `ioman`).

---

### R-11 🟢 Complexidade do parser de temas

**Descrição.** Adicionar muitos tipos de elemento aumenta a cadeia de `else if` em
`thmLoadThemeElements` e o consumo de código.

**Mitigação.** Substituir a cadeia por tabela de despacho `{nome, init_fn}` — refatoração local,
zona verde, sem risco.

---

### R-12 🟢 Escopo do RetroHub Manager (PC)

**Descrição.** O Manager depende de bases de dados externas para capas e metadados. Riscos:
disponibilidade de API, direitos de imagem, e o escopo crescer sem controle.

**Mitigação.**
1. Arquitetura de **provedores plugáveis**; nenhum provedor obrigatório.
2. Funcionar 100% offline com metadados manuais.
3. Definir o formato de índice **primeiro**; o Manager é só um produtor desse formato.
4. Tratar como projeto separado, fase posterior.

---

## 2. Regras invioláveis do projeto

Estas regras existem para tornar R-01 e R-08 improváveis por construção.

### RI-1 — Congelamento do motor
Os seguintes caminhos são **somente leitura** para o RetroHub:
```
ee_core/**
modules/**
src/system.c
src/ioprp.c
src/xparam.c
src/supportbase.c        (funções sbPrepare, sbCreatePath, sbProbeISO9660*, sbGetISO9660MaxLBA)
```

### RI-2 — Estruturas binárias compartilhadas
Nunca alterar layout, ordem de campos, tamanho ou alinhamento de:
`cdvdman_settings_common`, `cdvdman_settings_bdm`, `EECoreConfig_t`, `bd_fragment_t`,
`bdm_vmc_infos_t`, `USBExtreme_game_entry_t`, `hdl_game_info_t`.

### RI-3 — Contrato `item_list_t`
A vtable de `include/iosupport.h` só pode ser **estendida** (campos novos ao final). A UI nova
consome; não modifica.

### RI-4 — Compatibilidade de configuração
Chaves existentes em `include/config.h` nunca são renomeadas ou removidas. Chaves novas do
RetroHub usam o prefixo `$RH_` (por jogo) ou `rh_` (globais).

### RI-5 — Compatibilidade de temas
O espaço virtual 640×480 e os 17 tipos de elemento existentes são imutáveis. Extensões são
aditivas e opt-in.

### RI-6 — Layout de pastas
`CD/`, `DVD/`, `CFG/`, `ART/`, `THM/`, `LNG/`, `VMC/`, `CHT/`, `APPS/`, `ul.cfg` permanecem como
estão. Dados do RetroHub vão para `RH/` (pasta nova) para não colidir.

### RI-7 — Sem I/O no frame
Nenhuma chamada de sistema de arquivos dentro de `guiMainLoop`. Tudo via `ioPutRequest`.

### RI-8 — Degradação graciosa
Falta de índice, de capa, de metadado ou de tema **nunca** impede o usuário de lançar um jogo. O
caminho `lista → ✕ → jogo` precisa funcionar mesmo com todo o resto ausente.

### RI-9 — Orçamentos
Heap da UI ≤ 8 MB. Primitivas por frame ≤ 80. Tempo até a tela inicial ≤ 3 s.

### RI-10 — Verificação em hardware real
Emulador (PCSX2) serve para iterar UI. **Nenhuma release** sem teste em PS2 físico —
fat e slim, USB e HDD.

---

## 3. Estratégia de teste

| Nível | O quê | Onde |
|---|---|---|
| Build | `make clean release` + variantes da matriz | CI |
| Formato | `clang-format` | CI |
| Estático | Compilar com `-Wall -Wextra`; sem novos warnings | CI |
| UI / regressão visual | 5 temas legados renderizando idêntico ao OPL | PCSX2 |
| Memória | Pico de heap durante navegação de 1.000 jogos | Build de debug |
| Desempenho | FPS estável em grade de capas com rolagem rápida | PCSX2 + hardware |
| Compatibilidade | 20 jogos: CD, DVD SL/DL, ZSO, UL, VMC, cheats, GSM | **Hardware real** |
| Dispositivos | USB, MX4SIO, iLink, HDD interno, SMB | **Hardware real** |
| Inicialização | Tempo até a tela inicial, com 0 / 100 / 1.000 jogos | **Hardware real** |
| Robustez | Remover pendrive durante navegação; ISO corrompida; capa inválida; índice corrompido | **Hardware real** |

**Baseline obrigatório:** antes de qualquer mudança, gravar os mesmos 20 jogos rodando no OPL
original. Toda comparação é contra esse baseline.

---

## 4. Riscos por fase do plano

| Fase | Risco dominante | Gatilho de parada |
|---|---|---|
| 1 — Baseline e build | Toolchain divergente | Build não reproduz o ELF oficial |
| 2 — Índice e metadados | R-04, R-06 | Índice corrompe dados do usuário |
| 3 — Novos elementos de tema | R-05, R-09 | Tema legado renderiza diferente |
| 4 — Tela inicial (hub) | R-10 | Boot > 3 s |
| 5 — Grade de capas | R-02, R-03, R-09 | FPS < 50 ou heap > 8 MB |
| 6 — Busca/ordenação/filtros | R-03, R-04 | Ordenação > 100 ms com 1.000 jogos |
| 7 — Áudio e polimento | R-02 | Heap > 8 MB |
| 8 — RetroHub Manager | R-12 | — (projeto separado) |

Regra: **qualquer gatilho de parada disparado interrompe a fase até ser resolvido.** Não se
acumula dívida de desempenho ou memória entre fases — num sistema de 32 MB ela nunca é paga depois.
