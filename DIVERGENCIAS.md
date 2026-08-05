# Divergências em relação ao Open PS2 Loader

Registro de tudo que o RetroHub muda no código do OPL. Cada entrada existe para responder a três
perguntas: **o que mudou**, **por quê**, e **serve de PR para o upstream?**

O código do OPL não é copiado para este repositório. Ele vive num fork limpo, e as mudanças ficam
em [`patches/`](patches/), aplicadas por [`tools/rh-apply.sh`](tools/rh-apply.sh). A consequência
prática: `./tools/rh-apply.sh --revert` devolve o OPL original a qualquer momento, e `git diff` no
fork mostra exatamente a divergência — nem mais, nem menos.

**Base:** `ps2homebrew/Open-PS2-Loader`, commit `3e3f34e` (*Update issue template*, #1704).

---

## Índice

| # | Patch | Arquivo | Fase | Upstream |
|---|---|---|---|---|
| 0001 | Ordenação O(n log n) | `src/menusys.c` | 1 | Candidato |
| 0002 | Tela "Estante" | `gui.c/h`, `menusys.c/h`, `Makefile` | 5 (antecipada) | Não |

Arquivos novos, copiados de `overlay/` sem patch:

| Arquivo | Papel |
|---|---|
| `src/rh_shelf.c` | A tela Estante |
| `include/rh_shelf.h` | Sua interface pública |

---

## 0001 — Ordenação O(n log n) em `submenuSort()`

**Arquivo:** `src/menusys.c` · **Patch:** [`patches/0001-fase1-ordenacao-qsort.patch`](patches/0001-fase1-ordenacao-qsort.patch)

### O que era

`submenuSort()` era um bubble sort sobre lista duplamente encadeada. O comentário no código já
denunciava a intenção abandonada: `// *submenu = mergeSort(*submenu);`.

O custo é O(n²) em **comparações de string** — não em comparações de inteiro. Cada uma é um
`strcasecmp()` sobre o título do jogo, e cada troca religa quatro ponteiros. Para 400 jogos em
ordem desfavorável, isso passa de 80 mil comparações e outras tantas religações.

Isso não é um problema teórico para o RetroHub: a ordenação por nome, ano, gênero e desenvolvedora
é uma funcionalidade central, e reordenar precisa ser instantâneo, não uma operação que congela a
tela.

### O que é agora

Materializa um vetor de ponteiros, chama `qsort()`, religa a lista numa passada linear.

```
  antes:  O(n²) comparações  +  O(n²) religações de ponteiro
  agora:  O(n log n) comparações  +  O(n) religações
```

**Custo de memória:** 8 bytes por jogo, vivos apenas durante a ordenação — 3,2 KB para 400 jogos,
8 KB para 1.000. Devolvidos com `free()` antes do retorno.

### Três decisões que valem explicação

**1. O fallback não foi removido.** Se o `malloc` falhar, cai no bubble sort original, preservado
como `submenuSortBubble()`. Ele ordena no lugar e não aloca nada. Em 32 MB de RAM, falha de
alocação é um cenário real, e ordenar devagar é melhor que não ordenar — ou que travar.

**2. O desempate é explícito.** `qsort` não é estável; o bubble sort era. Sem cuidado, dois jogos
com títulos que `strcasecmp` considera iguais (`God of War` e `GOD OF WAR`) poderiam trocar de
lugar de forma imprevisível. O comparador desempata pela posição original, o que torna a saída
**idêntica** à da implementação anterior em todos os casos — não apenas equivalente.

**3. Os nós não são copiados, só religados.** Cada `submenu_list_t` carrega `cache_id` e
`cache_uid`, os índices do cache de texturas daquele item. Mover conteúdo entre nós invalidaria
esses caches silenciosamente e faria capas aparecerem no jogo errado. Reordenar ponteiros mantém
cada cache com o seu dono — que é, aliás, o mesmo motivo pelo qual o `swap()` original mexia em
elos e não em conteúdo.

### Verificação

[`tools/tests/sort_equivalence.c`](tools/tests/sort_equivalence.c) compila as duas implementações
lado a lado no PC e compara a saída, checando também a integridade dos elos nos dois sentidos e a
ausência de ciclos.

```
$ ./tools/run-tests.sh
um item / dois fora de ordem / dois em ordem / empate por caixa /
todos iguais / ordem inversa / caixa alternada / títulos vazios /
fallback com malloc falhando / 5 casos aleatórios (n=64…442)

14 casos, saída idêntica ao original, limpo sob AddressSanitizer e UBSan
custo no PC com 400 jogos: bubble 3,54 ms | qsort 0,44 ms
```

O ganho medido no PC (8×) **não é a previsão para o console.** O PS2 é muito mais lento e tem
cache pequeno; a diferença tende a ser maior, e é o hardware que decide. A medição real entra no
critério de aceite da Fase 1 — 1.000 jogos em ≤ 100 ms.

O que este teste **não** prova: que compila para MIPS, que o `qsort` do newlib do PS2SDK se comporta
como o da glibc, e que o comportamento no console é o esperado. Isso só o hardware responde.

### Upstream

**Candidato a PR.** É uma melhoria genérica, sem relação com a interface do RetroHub, e o próprio
comentário no código original indica que a substituição já era desejada.

---

## 0002 — Tela "Estante"

**Arquivos:** `include/gui.h`, `src/gui.c`, `include/menusys.h`, `src/menusys.c`, `Makefile`
**Novos:** `src/rh_shelf.c`, `include/rh_shelf.h`
**Patch:** [`patches/0002-fase5-tela-estante.patch`](patches/0002-fase5-tela-estante.patch)

### Por que antes da hora

O plano em [`04-plano-de-desenvolvimento.md`](docs/04-plano-de-desenvolvimento.md) punha a estante
na Fase 5, depois de índice e metadados. A justificativa era legítima: com centenas de jogos, uma
grade que lê disco a cada capa engasga, e construir a UI antes da camada de dados significa
reconstruí-la depois.

**Só que a biblioteca de teste tem um jogo.** O gargalo que a Fase 2 resolve não existe nessa
escala e não vai existir tão cedo. Adiar a única parte visível do projeto por causa de uma
otimização que ainda não morde é otimizar contra o problema errado — e custa a única coisa que
sustenta um projeto longo: ver ele funcionando.

A antecipação é barata porque a estante não depende da camada de dados. Ela lê a lista pela
`item_list_t`, que já existe. Quando o índice entrar, muda de onde vêm os dados — uma função — e
não como eles são desenhados.

### Como ela evita ser um risco

**Ela não substitui nada.** É uma sexta entrada em `screenHandlers[]`, ao lado das cinco do OPL. A
lista original continua sendo a tela inicial. Se a estante estiver quebrada, `○` volta para a lista
e o console segue utilizável — a tecla de saída é a primeira coisa tratada no handler de entrada.

**Ela compartilha o `current` do menu.** Andar na estante move o mesmo cursor que a lista usa, então
as duas telas nunca discordam sobre qual jogo está selecionado. Ao sair, `pagestart` é ancorado em
`current`, senão a lista voltaria mostrando uma página onde o cursor não está.

**Entrada por `R3`.** É a única tecla livre no handler principal do OPL, e exige apertar o
analógico — acionamento acidental é improvável.

### O custo, em números

| Recurso | Uso |
|---|---|
| Texturas | **1** — só a capa do jogo em foco |
| Cache | 1 entrada (`cacheInitCache(-1, "ART", 1, "_COV", 1)`) |
| Primitivas por frame | ~10 fixas + 4 a 6 por lombada visível |
| Arquivos novos em disco | nenhum |

As lombadas são retângulos, não imagens: quatro `rmDrawRect` cada, zero bytes de textura. É por isso
que a fila inteira é praticamente de graça e só o selecionado paga I/O — exatamente o comportamento
que a navegação por USB 1.1 exige.

A cor de cada lombada sai de um hash FNV-1a do código do disco, na faixa 60–170. Estável entre
execuções, distinta entre jogos, e não guarda nada em lugar nenhum.

### O que ainda não faz

- **Nome na lombada.** `fntRenderString` não gira texto, e a lombada é vertical. A solução é a
  imagem de lombada que o recortador já gera (`ART/<startup>_SPI.png`), desenhada quando existir,
  com o retângulo colorido como reserva. Fica para o próximo passo.
- Metadados (ano, gênero, jogadores) — dependem da Fase 2.
- Capa desfocada como fundo (`CoverBackdrop`) — depende de medir o orçamento de primitivas.

### Verificação

Compila limpo sob `gcc -Wall -Wextra` contra cabeçalhos mínimos que reproduzem as assinaturas reais
do OPL. Isso pega erro de digitação, argumento trocado e tipo errado — **não** prova que compila
para MIPS nem que desenha certo. Só o console responde isso.

### Upstream

**Não.** É a identidade do RetroHub, não uma correção genérica.
