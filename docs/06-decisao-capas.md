# Decisão de Arquitetura — Formato e Tamanho das Capas

> **Status:** decidido
> **Contexto:** substitui a proposta preliminar da seção 8 de
> [`03-arquitetura-retrohub-ps2.md`](03-arquitetura-retrohub-ps2.md), que previa dois tamanhos de
> capa (256×366 + 120×172).
> **Critério do dono do projeto:** *"usar o que ficar mais leve e fácil, para ser um sistema liso"*.

---

## Decisão

> **Uma capa por jogo: `ART/<startup>_COV.png`, 192×276, PNG paletizado de 8 bits (≤ 256 cores).**
> A grade desenha essa mesma textura reduzida por hardware; o painel de detalhes a desenha no
> tamanho nativo. **Não existe segundo arquivo de miniatura.**

| Parâmetro | Valor |
|---|---|
| Arquivo | `ART/<startup>_COV.png` |
| Resolução | **192 × 276** |
| Formato | PNG **paletizado 8 bits**, ≤ 256 cores |
| PSM no console | `GS_PSM_T8` + CLUT `GS_PSM_CT32` |
| RAM por capa | 52.992 B + 1.024 B (CLUT) = **~53 KB** |
| Cache | **16 entradas ≈ 845 KB** |
| Filtro | `GS_FILTER_LINEAR` (padrão do OPL) |
| Escala na grade | ~128×184 (**0,67×**) |
| Código novo necessário | **nenhum** |

---

## Por que esta é a opção mais leve

### O que o código já faz

`textures.c` decodifica PNG em quatro caminhos, escolhidos pelo tipo de cor do arquivo
(`textures.c:492-527`):

| PNG de entrada | PSM resultante | RAM por pixel | CLUT | Custo de decodificação |
|---|---|---|---|---|
| Paletizado 4 bits | `GS_PSM_T4` | 0,5 B | 64 B | desempacotamento de nibbles |
| **Paletizado 8 bits** | **`GS_PSM_T8`** | **1 B** | **1.024 B** | **`memcpy` por linha** |
| RGB | `GS_PSM_CT24` | 3–4 B | — | conversão por pixel |
| RGBA | `GS_PSM_CT32` | 4 B | — | conversão por pixel |

O caminho de 8 bits é literalmente o mais barato que existe no arquivo — `texReadPixels8`
(`textures.c:341-342`) monta a paleta uma vez e depois faz um `memcpy` por linha. Nenhum
processamento por pixel.

Números exatos, direto do código (`textures.c:518-522`):
```c
texture->PSM  = GS_PSM_T8;
texture->Clut = memalign(128, gsKit_texture_size_ee(16, 16, GS_PSM_CT32));  // 1.024 bytes
// texture->Mem = gsKit_texture_size_ee(w, h, GS_PSM_T8)                    // w × h bytes
```

**Ganho: 3 a 4× menos RAM e menos VRAM** que uma capa RGB do mesmo tamanho, sem escrever uma linha
de código.

### Por que 192×276 e não maior

Numa tela virtual de 640×480, uma capa de 192×276 ocupa 30% da largura e 57% da altura. É uma capa
grande de verdade — o requisito está atendido. Dobrar para 384×552 quadruplicaria a memória
(212 KB por capa, 3,4 MB de cache) para ganhar detalhe que uma TV de definição padrão não resolve.

A proporção 192:276 é 1:1,4375 — a proporção real da caixa de PS2 (138 × 197 mm).

192 é múltiplo de 64, que é a unidade de largura de buffer de textura do GS (`TBW`). Isso evita
padding interno.

---

## Por que esta é a opção mais fácil

### Um arquivo elimina uma máquina de estados inteira

A proposta anterior tinha dois arquivos por jogo: `_COV.png` (grande) e `_THM.png` (miniatura).
Isso obrigaria o console a manter:

- dois caches de textura independentes;
- dois estados de carregamento por jogo (miniatura carregada? capa carregada? nenhuma?);
- uma transição visual entre a miniatura ampliada e a capa nativa quando ela chegasse;
- lógica de fallback quando um dos dois arquivos faltasse.

Com um arquivo só, **nada disso existe**. A capa está carregada ou não está. Se não está, desenha
o placeholder. É a diferença entre um sistema com quatro estados e um com dois — e sistemas com
menos estados são os que ficam lisos.

O redimensionamento para a grade é feito pelo **GS, em hardware, de graça**: `rmDrawPixmap` já
recebe largura e altura de destino (`renderman.c:337`) e o filtro linear já é o padrão
(`texPrepare`, `textures.c:257`).

### Uma conversão só no RetroHub Manager

Um arquivo por jogo significa: uma conversão, uma quantização, uma verificação. Metade do trabalho
do Manager e metade dos arquivos no pendrive do usuário.

### Compatível com os art packs existentes

Existem muitos pacotes de capas de OPL circulando, quase todos em PNG RGB. Eles **continuam
funcionando** — só pesam 3–4× mais. O RetroHub Manager normaliza para 8 bits, e quem não usar o
Manager ainda tem um sistema que funciona. Degradação graciosa, conforme a regra RI-8.

---

## Alternativas consideradas e descartadas

### PNG paletizado de 4 bits (16 cores) — descartado
Metade da memória (26 KB por capa), mas 16 cores destroem uma arte de capa. Economizar 27 KB sobre
um número que já é pequeno não justifica o custo visual.

### Formato bruto próprio (`.rht`) — descartado
A ideia: pular o libpng e fazer `memcpy` direto do arquivo para a textura. Parece mais rápido, mas
não é mais leve:

- **Aumenta o I/O.** Bruto = 53 KB no disco; PNG-8 comprimido = 20–35 KB. Na USB 1.1 do PS2
  (~1 MB/s real), o I/O é o gargalo, não a CPU. O formato bruto leria **~60% mais bytes**.
- **A decodificação não bloqueia nada.** Ela roda na thread de I/O (`texcache.c:16`), nunca no
  loop de frame. Economizar CPU ali não deixa a interface mais fluida.
- **Exigiria código novo** no console e quebraria a compatibilidade com todo art pack existente.

Trocar menos I/O por mais CPU é o negócio certo neste hardware. O formato bruto faz o oposto.

### Dois tamanhos (proposta original) — descartado
Economizaria ~300 KB de RAM, mas custaria: dois caches, quatro estados de carregamento, uma
transição visual, o dobro de arquivos e o dobro de trabalho no Manager. **300 KB não valem essa
complexidade** dentro de um orçamento de 8 MB.

Fica registrado como otimização futura, caso surja um caso de uso real com 3.000+ jogos onde
845 KB de cache seja um problema medido — não presumido.

---

## Orçamento de memória revisado

| Item | Antes | **Agora** |
|---|---|---|
| Cache de capas | 1,5 MB (16 × 94 KB) | **845 KB** (16 × 53 KB) |
| Cache de miniaturas | 0,3 MB | **0** (eliminado) |
| Cache de ícones (20 × 64×64 T8) | 0,1 MB | 0,1 MB |
| Fundo estático 640×480 | 0,9 MB | 0,9 MB (T8: **0,3 MB**) |
| Atlas de fontes | 1,0 MB | 1,0 MB |
| Índice de biblioteca (2.000 jogos) | 1,0 MB | 1,0 MB |
| Nós de menu/submenu | 0,3 MB | 0,3 MB |
| **Subtotal** | 5,1 MB | **3,4 MB** |
| Folga operacional | 2,9 MB | **4,6 MB** |
| **Teto** | 8,0 MB | **8,0 MB** |

A folga quase dobrou. Isso é margem para picos transitórios de decodificação (ver abaixo) e para
crescimento futuro sem renegociar o orçamento.

---

## Detalhes de implementação que afetam a fluidez

### Pico transitório durante a decodificação

`texReadData` (`textures.c:377-410`) aloca **três** blocos simultâneos por capa:

```
texture->Mem            53.248 B   (fica)
allRows (cópia da imagem decodificada)  53.248 B   (temporário)
pFileBuffer (arquivo PNG inteiro)      ~30.000 B   (temporário)
                                    ─────────────
pico                                  ~136 KB
estado final                           ~53 KB
```

Como só uma capa é decodificada por vez (a thread de I/O é serial), o pico é de ~136 KB, não
multiplicado pelo cache. Está coberto pela folga.

### Sem limite de tamanho no buffer de arquivo — item de robustez

`pFileBuffer = malloc(fileSize)` (`textures.c:430`) aloca o arquivo inteiro **antes de qualquer
validação de dimensão**. Um PNG de 20 MB em `ART/` tentaria alocar 20 MB.

Mitigação (Fase 3): rejeitar arquivos acima de **256 KB** antes do `malloc`. É uma checagem de duas
linhas, e nenhuma capa 192×276 em PNG-8 legítima chega perto disso.

O RetroHub Manager também garante isso na origem.

### Decisões complementares para manter o sistema liso

1. **Fundo estático em vez do plasma Perlin.** O plasma (`gui.c:1269`) é ruído 3D recalculado em
   CPU **todo frame**. O tema padrão do RetroHub define um `Background` estático, o que desliga
   esse caminho por completo. Em T8 o fundo custa 0,3 MB em vez de 0,9 MB.

2. **Pré-carregamento direcional.** Ao mover o foco, enfileirar a capa seguinte na direção do
   movimento. Uma requisição por movimento, não uma varredura.

3. **Manter o anti-thrash existente.** `cacheGetTexture` já se recusa a carregar enquanto
   `guiInactiveFrames < list->delay` (`texcache.c:143`). Numa grade isso é ainda mais valioso do
   que numa lista: segurar o direcional não dispara 40 leituras de disco.

4. **Placeholder estável.** Jogo sem capa mostra um placeholder do tema — nunca um espaço vazio
   que "pula" quando a capa chega. O tamanho do slot é fixo; só o conteúdo muda.

---

## Critérios de aceite (atualiza a Fase 5 do plano)

- [ ] Capa individual: ≤ 53 KB em RAM (192×276 T8 + CLUT)
- [ ] Cache de 16 capas: ≤ 845 KB
- [ ] Heap total da UI: ≤ 8 MB
- [ ] 60 fps (NTSC) / 50 fps (PAL) estáveis com rolagem rápida contínua na grade
- [ ] Segurar o direcional não dispara I/O
- [ ] Jogo sem capa exibe placeholder sem alterar o layout
- [ ] Capa RGB legada (não paletizada) carrega e funciona, apenas ocupando mais memória
- [ ] PNG acima de 256 KB é rejeitado sem travar
- [ ] Capa corrompida é rejeitada sem travar

---

## O que muda no RetroHub Manager

Substitui a seção 2.4 de [`05-retrohub-manager-pc.md`](05-retrohub-manager-pc.md):

| Saída | Resolução | Formato |
|---|---|---|
| `<startup>_COV.png` | **192 × 276** | PNG paletizado 8 bits |
| `<startup>_BG.png` | 640 × 480 | PNG paletizado 8 bits (opcional) |
| `<startup>_ICO.png` | 64 × 64 | PNG paletizado 8 bits (opcional) |

Quantização para ≤ 256 cores com dithering. Rejeitar/reprocessar qualquer saída acima de 256 KB.

**`_THM.png` deixa de existir.**
