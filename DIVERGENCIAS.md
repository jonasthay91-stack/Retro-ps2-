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
