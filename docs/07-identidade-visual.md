# Identidade Visual — RetroHub PS2

> **Contexto:** o dono do projeto apresentou referências de front-ends existentes com a instrução
> *"achei legal, porém não copiar — fazer melhor"*.
> Este documento registra o que foi aproveitado, o que foi deliberadamente rejeitado, e a
> técnica que dá identidade própria ao RetroHub.

---

## 1. Leitura das referências

### Referência A — thumbnail promocional (PS3 multijogos)

Arte de divulgação, não interface: tipografia gritada com contorno, logos de plataformas
empilhados em diagonal, colagem de artes de jogos. **Não há vocabulário de interface a extrair
daqui.** Serve apenas como indicação de tom do público-alvo — que valoriza fartura e reconhecimento
imediato de plataformas.

### Referência B — tema estilo EmulationStation / Batocera

Esta sim é uma referência de interface. Estrutura observada:

```
┌───────────────────────────────────────────────┐
│              807 JOGOS                        │
│  ┌───────────────────┐   ┌──────────────────┐ │
│  │                   │   │ ARCADE CLASSICS  │ │
│  │   ARTE GRANDE     │   ├──────────────────┤ │
│  │   (dominante)     │ ◄ │ SUPER NINTENDO   │ │
│  │                   │   ├──────────────────┤ │
│  │        [badge]    │   │ GAME BOY ADVANCE │ │
│  └───────────────────┘   └──────────────────┘ │
│  A Select  B Back  X Favorites  Y Search      │
└───────────────────────────────────────────────┘
```

---

## 2. O que foi aproveitado

| Elemento | Por quê |
|---|---|
| **Arte grande dominando a tela** | Impacto imediato — e, contraintuitivamente, é **mais barato** que uma grade: uma textura grande custa menos que oito pequenas |
| **Lista vertical de badges com logo** | O símbolo é reconhecido antes de o texto ser lido |
| **Contagem de itens visível** | Orientação instantânea sobre o tamanho da biblioteca |
| **Barra de dicas com glifos de botão** | O OPL já faz isso (`HintText`); a referência confirma que funciona |
| **Badge de plataforma sobre a arte** | Identifica a origem sem ocupar uma linha de texto |

**A descoberta mais útil:** o lado direito da referência lista **sistemas, não jogos**. Aquela tela
é um seletor de plataforma — ou seja, é o equivalente da **tela inicial** do RetroHub, não da tela
de jogos. E o layout dela é melhor que os 7 tiles chapados propostos originalmente em
[`03-arquitetura`](03-arquitetura-retrohub-ps2.md). A tela inicial foi redesenhada por causa disso
(seção 4).

---

## 3. O que foi deliberadamente rejeitado

| Problema da referência | Decisão do RetroHub |
|---|---|
| **Moldura sci-fi cromada** ocupa ~15% da tela sem informar nada | Nenhum ornamento. Hierarquia por espaço, escala e tipografia |
| **Zero metadados** — só arte e nome do sistema | Metadados são o diferencial do projeto; ano, gênero e desenvolvedora sempre visíveis |
| **Sem noção de posição** em 807 itens | Indicador de posição e página sempre presentes |
| **Badges com tamanhos e estilos inconsistentes** (cada logo de uma fonte) | Sistema visual único: mesma altura, mesmo tratamento, mesma grade |
| **Estética datada** (HUD futurista de ~2015) | Linguagem contemporânea: superfícies planas, uma cor de destaque, espaço negativo |
| **Arte inconsistente** (key art solta, não capa) | Padrão único de capa 192×276 ([`06-decisao-capas`](06-decisao-capas.md)) |

O ornamento é o ponto central: ele existe nesses temas porque **preencher é mais fácil que
compor**. Trocar moldura por espaço é o que separa "tema de emulador" de "interface de console".

---

## 4. A assinatura visual: a capa vira o fundo

O elemento que dá identidade própria ao RetroHub e que nenhuma das referências usa.

**A capa do jogo em foco é desenhada esticada para a tela inteira, muito escurecida, como fundo.**

```
rmDrawPixmap(capa, 0, 0, ALIGN_NONE, 640, 480, SCALING_NONE, corEscura);
```

### Por que funciona no PS2

O upscale de 192×276 para 640×480 é de **3,3×** com `GS_FILTER_LINEAR` — que já é o padrão
(`texPrepare`, `textures.c:257`). Uma ampliação bilinear dessa magnitude **é** um desfoque. O
efeito que num PC exigiria um shader de blur sai como consequência natural do hardware.

O parâmetro `color` de `rmDrawPixmap` multiplica a textura (`renderman.c:337`), então o
escurecimento vem na mesma chamada.

| Custo | Valor |
|---|---|
| Primitivas | **1** (opcionalmente 2, com um `rmDrawRect` de gradiente por cima) |
| Memória | **0 bytes** — é a mesma textura que já está no cache de capas |
| Trabalho de arte | **nenhum** — não requer nada do RetroHub Manager |

### O que isso entrega

- O fundo **responde ao conteúdo**: muda de cor e clima a cada jogo em foco
- Substitui o plasma Perlin, que custa CPU todo frame (`gui.c:1269`)
- Substitui a moldura ornamental por algo que respira
- Dá coesão cromática: a capa e o fundo sempre combinam, porque são a mesma imagem

### Cuidados

1. **Escurecer o suficiente** para o texto manter contraste — alvo de no mínimo 4,5:1 sobre a
   região mais clara do fundo. Na prática, multiplicador em torno de 25–30% do brilho original.
2. **Transição suave** ao trocar de jogo: interpolar o escurecimento por ~8 frames evita cintilação
   durante rolagem.
3. **Fallback**: sem capa carregada, usa a cor de fundo sólida do tema. Nunca fica preto puro.
4. **Desligável** no menu de configurações visuais, para quem prefere fundo estático.

---

## 5. Tela inicial redesenhada

Substitui a grade de 7 tiles de [`03-arquitetura`](03-arquitetura-retrohub-ps2.md#32-tela-inicial-hub).

```
   16 ┌───────────────────────────────────────────────────────┐
      │  RetroHub                    428 jogos       14:32    │ 36
   52 ├──────────────────────────────────┬────────────────────┤
      │                                  │ ▸ JOGOS PS2    428 │
      │      ┌────────────────┐          │   JOGOS PS1     61 │
      │      │                │          │   EMULADORES     7 │
      │      │   HERO 267x384 │          │   HOMEBREW      23 │
      │      │                │          │   FAVORITOS     19 │
      │      │                │          │   RECENTES       8 │
      │      │       [PS2]    │          │   CONFIGURAÇÕES    │
      │      └────────────────┘          │                    │
  436 ├──────────────────────────────────┴────────────────────┤
      │  X Abrir      /\ Opções          USB o  HDD o  SMB -  │ 28
  464 └───────────────────────────────────────────────────────┘
        |<----------- 372 ------------>|<------- 220 ------->|

   Fundo: capa do último jogo jogado da categoria em foco,
          esticada a 640x480 e escurecida. 1 primitiva.
```

| Parâmetro | Valor |
|---|---|
| Conteúdo | 608 × 384 (margem 16 px, alvo HDMI) |
| Lista | 7 itens de **48 px** + gaps de 8 px = **384 px exatos** |
| Largura da lista | 220 px |
| Área do hero | 372 × 384 |
| Hero | capa 192×276 desenhada a **267×384** (upscale 1,39×) |

**O hero é a capa do último jogo jogado da categoria em foco.** Isso dá continuidade emocional — a
tela inicial mostra onde você parou — e não exige nenhuma arte adicional do usuário nem do Manager.

Categoria vazia mostra um placeholder do tema e o item fica esmaecido, mas **não desaparece**: a
lista tem sempre as mesmas 7 linhas, na mesma ordem, na mesma posição.

**Custo:** 1 fundo + 1 hero + 1 badge + 7 linhas de lista + 7 contadores + cabeçalho + rodapé +
indicadores de dispositivo ≈ **22 primitivas**.

---

## 6. Modo Cinema — a referência levada a sério

A referência B, aplicada a jogos em vez de sistemas, é um modo de visualização legítimo: **uma capa
por vez, muito grande**. Ele passa a ser um dos três modos, não um consolo.

| Modo | Capas visíveis | Quando |
|---|---|---|
| **Grade** (padrão) | 6 (4:3) / 8 (16:9) | Navegar bibliotecas grandes |
| **Cinema** | 1, a 267×384 | Apreciar a coleção; bibliotecas pequenas; quem prefere impacto |
| **Clássico** | conforme o tema | Compatibilidade com temas de OPL |

O modo Cinema é o **mais leve dos três** — uma textura visível, ~14 primitivas. É também o modo
recomendado para dispositivos lentos, já que carrega uma capa por vez.

Diferenças em relação à referência: sem moldura ornamental, com metadados completos ao lado, e com
indicador de posição na lista.

---

## 7. Sistema de badges

A referência usa logos de origens diferentes, com alturas e estilos inconsistentes. O RetroHub
padroniza:

| Regra | Valor |
|---|---|
| Altura fixa | 24 px (lista) · 32 px (sobre o hero) |
| Largura | variável, até 96 px |
| Formato | PNG paletizado 8 bits |
| Tratamento | monocromático na cor de destaque quando em foco; cor secundária quando não |
| Fallback | texto no lugar do logo, mesma caixa, mesma altura |

Badges usados: dispositivo (USB, HDD, MX4SIO, iLink, SMB), categoria, região, formato
(ISO/ZSO/UL/HDL). O OPL já tem as texturas de dispositivo e formato em `gfx/`
(`textures.h`: `USB_ICON`, `HDD_ICON`, `ISO_FORMAT`, `ZSO_FORMAT`…) — são reaproveitadas e
reestilizadas, não recriadas.

---

## 8. Princípios consolidados

1. **Nenhum ornamento.** Se um elemento não informa nem organiza, sai. Moldura, chanfro, brilho e
   textura de fundo estão fora.
2. **A arte é a interface.** A capa é o maior elemento da tela e também o fundo dela. O resto é
   tipografia e espaço.
3. **Uma cor de destaque por tema.** Aplicada a foco e ações. Todo o resto é neutro.
4. **Posição sempre visível.** O usuário nunca deve se perguntar onde está numa biblioteca de mil
   jogos.
5. **Layout estável.** Nada se move quando o conteúdo carrega. Slots e listas têm tamanho fixo.
6. **Movimento sóbrio.** Transições de 12–16 frames com easing. Sem paralaxe, sem partículas.
7. **Barato por construção.** Cada efeito visual precisa se pagar em primitivas e bytes. O fundo
   desfocado é o modelo: máximo impacto, 1 primitiva, 0 bytes.

---

## 9. Impacto no plano

| Fase | Ajuste |
|---|---|
| **3** — Extensão de temas | Novo elemento `CoverBackdrop` (a capa como fundo). `HubTile` é substituído por `HubList` |
| **4** — Tela inicial | Layout hero + lista, não grade de tiles |
| **5** — Tela de jogos | Modo Cinema entra como modo de primeira classe, não fallback |
| **7** — Identidade | Sistema de badges padronizado; os 4 temas exercitam o backdrop |

Nenhum desses ajustes altera orçamento de memória: o backdrop reusa textura já carregada, e o hero
da tela inicial é uma única capa que o cache já cobre.
