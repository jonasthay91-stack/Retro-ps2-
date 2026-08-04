# Plano de Desenvolvimento — RetroHub PS2

> Plano em 8 fases. Cada fase tem entregáveis, critérios de aceite e gatilhos de parada.
> Nenhuma fase começa antes de a anterior passar em seus critérios.

---

## Visão geral

| Fase | Nome | Entregável principal | Risco dominante |
|---|---|---|---|
| 0 | Baseline | Build reproduzível + suite de regressão | — |
| 1 | Fundações | `qsort`, tabela de despacho, contadores | R-08 |
| 2 | Índice e metadados | `library.idx` + esquema `$RH_*` | R-04, R-06 |
| 3 | Extensão de temas | Novos tipos de elemento renderizando | R-05 |
| 4 | Tela inicial | Hub de 7 categorias | R-10 |
| 5 | Grade de capas | Tela de jogos com painel de detalhes | R-02, R-03, R-09 |
| 6 | Busca, ordenação, filtros | Navegação completa | R-03, R-04 |
| 7 | Identidade e polimento | Tema padrão, ícones, sons, configurações | R-02 |
| 8 | RetroHub Manager (PC) | Aplicativo Windows | R-12 |

**Fases 0-2 não têm mudança visível.** É deliberado: são elas que tornam as fases seguintes
possíveis dentro dos orçamentos.

---

## Fase 0 — Baseline e infraestrutura

**Objetivo.** Ter um ponto de comparação confiável antes de tocar em qualquer coisa.

### Tarefas
1. Fork de `ps2homebrew/Open-PS2-Loader`; `master` espelha o upstream, trabalho em `retrohub/*`.
2. Reproduzir o build oficial: `make clean release` no container
   `ghcr.io/ps2homebrew/ps2homebrew:main`. Comparar o ELF com o release oficial.
3. Montar o **conjunto de regressão**: 20 jogos cobrindo CD, DVD single-layer, DVD dual-layer,
   ZSO, UL multi-parte, jogo com VMC, jogo com cheats, jogo com GSM.
4. Gravar o baseline: cada um dos 20 jogos rodando no OPL original, em hardware real.
5. Medir e registrar o baseline de desempenho: tempo de boot, FPS, pico de heap, tempo de
   ordenação — com 0, 100 e 1.000 jogos.
6. **Medir o custo de carregar uma capa** — o número que decide o desenho da grade. Separar as
   três parcelas, em cada dispositivo (USB, MX4SIO, iLink, HDD interno, SMB):

   | Parcela | Como medir |
   |---|---|
   | `open()` do arquivo | tempo de abertura isolado, com cache de FAT frio e quente |
   | `read()` do PNG | tempo por KB, com um PNG-8 de 192×276 (~30 KB) |
   | decodificação libpng | tempo de `texLoadAll` descontando I/O |

   Medir também **8 capas em sequência** (uma página de grade) para capturar o custo agregado de
   abertura, que é o suspeito principal.

   **Decisão que este número destrava:** se a abertura de arquivo dominar, a Fase 5 adota
   `RH/covers.pak` (ver [`06-decisao-capas.md`](06-decisao-capas.md#plano-b-condicional-rhcoverspak));
   se o volume de dados dominar, a alavanca é comprimir mais a capa; se a decodificação dominar,
   a alavanca é reduzir a resolução.
7. CI: build da matriz + `clang-format` + **guarda que falha se um commit tocar
   `ee_core/`, `modules/`, `src/system.c`, `src/ioprp.c`, `src/xparam.c` sem a label
   `engine-change`**.
8. Criar `DIVERGENCIAS.md`.

### Critérios de aceite
- [ ] Build reproduz o ELF oficial
- [ ] 20/20 jogos do baseline funcionam em hardware real
- [ ] Números de baseline registrados
- [ ] **Custo de carregamento de capa medido nos 5 dispositivos**, com as três parcelas separadas
- [ ] CI verde, incluindo a guarda de motor

**Gatilho de parada:** build não reproduz → resolver toolchain antes de seguir.

---

## Fase 1 — Fundações invisíveis

**Objetivo.** Preparar o terreno sem mudar nada que o usuário veja. Todas as mudanças desta fase
são candidatas a PR para o upstream.

### Tarefas
1. **Ordenação O(n log n).** Substituir `submenuSort()` (bubble sort em lista encadeada,
   `menusys.c:561`) por: materializar array de ponteiros → `qsort` → reconstruir os elos.
2. **Tabela de despacho de temas.** Trocar a cadeia de `else if` (`themes.c:1023-1082`) por um
   registro `{nome, init_fn}`. Puramente mecânico, sem mudança de comportamento.
3. **Instrumentação.** Em build de debug: contador de heap, contador de primitivas por frame,
   cronômetro de boot, tempo de ordenação. Exibidos no rodapé.
4. **Correção do semáforo** em `ioRegisterHandler` (`ioman.c:60-72`): liberar `gProcSemaId` nos
   caminhos de erro.
5. **Verificação de `malloc`** nos caminhos novos e nos mais críticos da UI.
6. Registrar tudo em `DIVERGENCIAS.md` e abrir PRs upstream para 1, 2 e 4.

### Critérios de aceite
- [ ] Ordenação de 1.000 jogos ≤ 100 ms (baseline: segundos)
- [ ] Tema legado renderiza **idêntico** ao baseline (comparação de captura, 5 temas)
- [ ] 20/20 jogos de regressão continuam funcionando
- [ ] Instrumentação funcionando

**Gatilho de parada:** qualquer diferença visual em tema legado.

---

## Fase 2 — Índice e metadados

**Objetivo.** Ter a biblioteca inteira em memória, ordenável e filtrável, sem I/O por jogo.

### Tarefas
1. Implementar `rh_index.c`: leitura, escrita e validação de `RH/library.idx`
   (formato em [`03-arquitetura`](03-arquitetura-retrohub-ps2.md#22-a-solução-índice-binário)).
2. Implementar `rh_meta.c`: ler/escrever chaves `$RH_*` em `CFG/<startup>.cfg`, preservando as
   chaves existentes intactas.
3. Implementar `rh_library.c`: modelo em memória, os 4 índices pré-ordenados, máscara de filtro.
4. **Construtor de índice no console:** varre via `item_list_t` (`itemGetCount`, `itemGetName`,
   `itemGetStartup`, `itemGetConfig`) e grava o `.idx`. Roda na thread de I/O, nunca no frame.
5. **Invalidação** via `sourceHash`, reusando os sinais que `bdmNeedsUpdate()` já coleta.
6. **Fallback:** índice ausente ou corrompido → caminho clássico do OPL (`sbReadList` +
   `sbPopulateConfig`). Testar explicitamente com `.idx` truncado, com magic errado e com versão
   futura.
7. Escrita atômica (`.tmp` → rename) para todo arquivo em `RH/`.
8. `rh_favorites.c`: `RH/favorites.cfg` e `RH/recent.cfg` (máx. 32, LRU).

### Critérios de aceite
- [ ] Índice de 1.000 jogos construído em ≤ 20 s (primeiro boot); carregado em ≤ 200 ms (demais)
- [ ] Consumo do índice + strings + ordenações ≤ 1 MB para 1.000 jogos
- [ ] Índice corrompido/ausente → UI funciona normalmente pelo caminho clássico
- [ ] Cartão preparado pelo RetroHub continua funcionando no **OPL original sem modificação**
- [ ] Remover o pendrive durante a construção do índice não trava nem corrompe
- [ ] 20/20 jogos de regressão continuam funcionando

**Gatilho de parada:** qualquer corrupção de dados do usuário, ou o OPL original deixar de ler um
cartão preparado pelo RetroHub.

---

## Fase 3 — Extensão do sistema de temas

**Objetivo.** Os novos elementos existem e desenham; ainda sem telas novas.

### Tarefas
1. `rh_theme_ext.c`: registro dos novos tipos.
2. Implementar `CoverGrid` (`rh_grid.c`) — o mais complexo: paginação, foco, integração com
   `texcache`. Capa única de 192×276 em T8, reduzida por hardware na grade
   (ver [`06-decisao-capas.md`](06-decisao-capas.md)).
3. Implementar `GameCard` (`rh_card.c`) — painel de campos configuráveis.
4. Implementar `TabBar`, `HubTile`, `SearchBox`, `StatusBar`, `AttributeBadge`.
5. Novas seções de tela no parser: `hubN`, `gridN`, `searchN`.
6. Layout embutido de fallback para temas sem essas seções.
7. Extensão do `texcache`: cache de **24 entradas** e pré-carregamento direcional. **Sem alterar a
   semântica de `qr`/`UID`.**
8. **Prioridade de carregamento na grade:** separar a passagem de requisição da passagem de
   desenho. Pedir na ordem `foco → vizinhas → resto da página`; desenhar na ordem de layout.
9. Limite de tamanho de arquivo PNG (256 KB) antes do `malloc` em `textures.c:430`.

### Critérios de aceite
- [ ] 5 temas legados renderizam idênticos ao baseline
- [ ] Tema de teste com `CoverGrid` renderiza 8 capas + 1 em foco a 60 fps
- [ ] Cache de 24 capas ≤ 1,27 MB; heap ≤ 8 MB
- [ ] A capa em foco é sempre a primeira a aparecer ao entrar numa página
- [ ] Primitivas/frame ≤ 80
- [ ] Nenhuma leitura de arquivo dentro do loop de frame
- [ ] PNG acima de 256 KB rejeitado sem travar

**Gatilho de parada:** FPS abaixo de 50, ou heap acima de 8 MB, ou regressão em tema legado.

---

## Fase 4 — Tela inicial (Hub)

**Objetivo.** A primeira tela que o usuário vê é o hub do RetroHub.

### Tarefas
1. `rh_hub.c`: `GUI_SCREEN_HUB` com render e input.
2. Sete tiles: PS2, PS1, Emuladores, Homebrew, Favoritos, Recentes, Configurações.
3. Contadores lidos do cabeçalho do índice (custo zero de I/O).
4. Barra de status com dispositivos detectados e relógio.
5. Navegação: ←/→/↑/↓, ✕ entra, START abre configurações.
6. `GUI_SCREEN_HUB` vira a tela inicial após a intro.
7. Classificação de categoria: `RH_CAT_*`, derivada do dispositivo/formato e de `$RH_Category`.
8. Configuração para voltar ao comportamento clássico (lista de dispositivos) — respeita usuários
   que preferem o OPL como é.

### Critérios de aceite
- [ ] Tela inicial visível em ≤ 3 s a partir do boot, com 1.000 jogos indexados
- [ ] Contadores corretos em todas as categorias
- [ ] Dispositivo conectado/removido atualiza a barra de status ao vivo
- [ ] Categoria vazia é exibida desabilitada, não some (previsibilidade de layout)
- [ ] 20/20 jogos de regressão continuam funcionando

**Gatilho de parada:** boot acima de 3 s.

---

## Fase 5 — Tela de jogos

**Objetivo.** A tela principal de navegação, com capas grandes e ficha completa.

### Tarefas
1. `GUI_SCREEN_GRID` com render e input.
2. Grade + painel lateral com os 11 campos exigidos: capa, nome, ano, desenvolvedora, gênero,
   nº de jogadores, região, código do disco, compatibilidade, última vez jogado, favorito.
3. Foco por escala + escurecimento das demais capas.
4. Paginação com L1/R1; L2/R2 para primeira/última página.
5. ✕ lança o jogo — **via `itemLaunch()` da vtable, sem tocar no motor**.
6. △ abre `GUI_SCREEN_GAME_MENU` (tela existente do OPL).
7. □ abre `GUI_SCREEN_INFO` (tela existente).
8. Marcar/desmarcar favorito (botão a definir — provavelmente L3 ou △ longo).
9. Atualização de `lastPlayed`/`playCount` no lançamento, gravando em `RH/recent.cfg`.
10. Modos de visualização: grade / lista+capa / clássico.
11. **Se a medição da Fase 0 apontar a abertura de arquivo como gargalo:** implementar
    `RH/covers.pak` com fallback automático para PNGs soltos em `ART/`
    (ver [`06-decisao-capas.md`](06-decisao-capas.md#plano-b-condicional-rhcoverspak)).
    Caso contrário, **não implementar** — seria complexidade sem ganho medido.

### Critérios de aceite
- [ ] 60 fps (NTSC) / 50 fps (PAL) estáveis durante rolagem rápida contínua
- [ ] Heap ≤ 8 MB
- [ ] Rolagem rápida não dispara I/O (o anti-thrash de `guiInactiveFrames` funciona na grade)
- [ ] Entrar numa página nova preenche as 8 capas sem travar a navegação
- [ ] Voltar a uma página já visitada é instantâneo (cache cobre 3 páginas)
- [ ] Jogo sem capa exibe placeholder de tamanho fixo **com o nome do jogo** — o layout não "pula"
      quando a capa chega, e "sem capa" é indistinguível de "carregando"
- [ ] Biblioteca sem nenhuma arte continua totalmente navegável pelos títulos
- [ ] Capa corrompida/inválida é rejeitada sem travar
- [ ] Capa RGB legada (não paletizada) funciona, apenas ocupando mais memória
- [ ] Lançamento de jogo funciona nos 5 dispositivos (USB, MX4SIO, iLink, HDD, SMB)
- [ ] 20/20 jogos de regressão em hardware real

**Gatilho de parada:** qualquer queda de FPS abaixo de 50, ou qualquer jogo do baseline que pare
de funcionar.

---

## Fase 6 — Busca, ordenação e filtros

**Objetivo.** Navegação completa em bibliotecas de milhares de jogos.

### Tarefas
1. `rh_search.c`: `GUI_SCREEN_SEARCH` usando o teclado virtual existente.
2. Filtro incremental com debounce de 250 ms, produzindo máscara de bits.
3. Alternância de ordenação (nome / ano / gênero / desenvolvedora) por troca de ponteiro.
4. Filtros combináveis: categoria, gênero, região, favoritos, não jogados.
5. Indicador visual do filtro ativo e contador de resultados.
6. Persistir a última ordenação/filtro por categoria.
7. Estado vazio ("nenhum resultado") com ação clara de limpar filtro.

### Critérios de aceite
- [ ] Busca em 1.000 jogos: ≤ 30 ms por tecla
- [ ] Troca de ordenação: imperceptível (≤ 16 ms)
- [ ] Filtros combinam corretamente
- [ ] Nenhuma alocação de memória durante a digitação
- [ ] Estado vazio não deixa o usuário preso

**Gatilho de parada:** busca acima de 50 ms por tecla.

---

## Fase 7 — Identidade e polimento

**Objetivo.** O RetroHub parece um produto, não um patch.

### Tarefas
1. Tema padrão "Midnight" completo — layout, cores, tipografia.
2. Conjunto de ícones próprio (traço 2 px, 24/48 px, PNG paletizado).
3. Temas adicionais: "Aurora", "Retro CRT", "Slate".
4. Novos sons de navegação com identidade própria (substituir os 8 `.adp`; adicionar foco, erro,
   favoritar).
5. Tela de configurações visuais: modo de visualização, tamanho de capa, densidade da grade,
   fundo personalizado, tema, cores.
6. Configurações de áudio: BGM opcional, volumes (já existem — apenas expor bem).
7. Tela inicial personalizável: ordem e visibilidade dos tiles.
8. Transições e easing consistentes (12-16 frames).
9. Revisão de todos os textos e tradução para os 31 idiomas do OPL.
10. Documentação de usuário: como preparar o pendrive, como funcionam as capas, formato dos
    metadados.
11. Documentação de tema: referência completa dos novos tipos de elemento.

### Critérios de aceite
- [ ] Todos os orçamentos respeitados no tema padrão
- [ ] Os 4 temas funcionam em 4:3 e 16:9, em PAL, NTSC e 480p
- [ ] Teste de legibilidade em CRT real (texto pequeno, área segura)
- [ ] Documentação de usuário e de tema completas
- [ ] 20/20 jogos de regressão
- [ ] Teste em PS2 fat **e** slim

**Gatilho de parada:** ilegibilidade em CRT, ou orçamento estourado.

---

## Fase 8 — RetroHub Manager (PC)

Detalhado em [`05-retrohub-manager-pc.md`](05-retrohub-manager-pc.md).

Projeto separado, dependente apenas do **formato de índice e do esquema de metadados** definidos na
Fase 2. Pode ser iniciado em paralelo a partir da Fase 3, mas só faz sentido entregar depois que o
formato estiver estável.

---

## Marcos

| Marco | Quando | Significado |
|---|---|---|
| **M1 — Fundação segura** | Fim da Fase 2 | Índice funcionando, motor intocado, zero regressão |
| **M2 — Prova visual** | Fim da Fase 5 | Grade de capas rodando em hardware real dentro dos orçamentos |
| **M3 — Navegação completa** | Fim da Fase 6 | Utilizável como front-end principal |
| **M4 — Produto** | Fim da Fase 7 | Primeira release pública |
| **M5 — Ecossistema** | Fim da Fase 8 | Manager de PC disponível |

---

## Disciplina de trabalho

### Ordem inegociável
As Fases 0-2 vêm antes de qualquer pixel novo. É tentador começar pela grade de capas — é a parte
visível e gratificante. Mas sem a Fase 1 a ordenação trava, e sem a Fase 2 a grade faz I/O por
jogo e nenhuma otimização de renderização salva. Construir a UI primeiro significa reconstruí-la
depois.

### Regra da regressão
**Toda fase termina com os 20 jogos do baseline rodando em hardware real.** Não "na próxima
fase", não "antes da release". Num sistema onde a falha se manifesta como tela preta sem log,
descobrir a regressão três fases depois custa dias de bisect.

### Regra do orçamento
Orçamentos de memória e de frame são verificados ao fim de cada fase, não no fim do projeto. Num
console de 32 MB, dívida de desempenho nunca é paga depois — ela só é descoberta quando já é cara
demais para corrigir.

### Regra do upstream
Toda melhoria genérica vira PR para o OPL. Reduz divergência, melhora o ecossistema e valida as
mudanças com os mantenedores que conhecem o motor melhor do que qualquer um.

---

## Como retomar este projeto

O estado atual é: **análise concluída, nenhuma linha de código escrita** — exatamente como
solicitado.

O próximo passo concreto é a **Fase 0**: fork, build reproduzível e conjunto de regressão. Nada
antes disso.

Duas decisões dependem do dono do projeto e valem ser fechadas antes da Fase 2:

1. **`RH/` por dispositivo ou centralizado?** Por dispositivo é mais robusto (um pendrive leva seu
   próprio índice); centralizado é mais simples. Recomendação: **por dispositivo**.
2. **Fork público desde o início ou desenvolvimento privado até M2?** Público desde o início
   facilita contribuições e feedback da comunidade de PS2, que é ativa e criteriosa.
   Recomendação: **público desde o início**, deixando claro que é um trabalho em andamento.
