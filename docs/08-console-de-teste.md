# Console de Teste — Pré-requisito da Fase 0

> Como deixar um PS2 pronto para receber builds do RetroHub, e por que isso precisa existir
> antes de qualquer linha de código.
>
> **Aviso:** compatibilidade de exploits varia por modelo (SCPH-xxxxx), região e firmware.
> Este documento descreve o caminho conceitual; os detalhes do seu console específico precisam
> ser conferidos.

---

## 1. Se o console já roda OPL pelo pendrive, pule para a seção 3

**O caso mais comum, e o mais fácil.** Se o console já lança o `OPNPS2LD.ELF` de um pendrive,
então o ponto de entrada **já está resolvido** — há FreeMcBoot (ou equivalente) instalado no
Memory Card, e nada mais precisa ser feito nessa frente.

Nesse cenário:

- **Nenhum disco é necessário.** Nem para instalar, nem para rodar, nem para testar.
- **Leitor óptico quebrado ou ausente não é problema.** O OPL lê tudo do pendrive, e os jogos
  também — o drive nunca é usado.
- **Instalar uma build nova = copiar um arquivo.**

Vá direto para a seção 3.

---

## 2. Se o console ainda não roda homebrew

### O mal-entendido mais comum

> *"Gravo o OPL num CD e rodo."*

**Não funciona num console de fábrica.** O PS2 verifica a autenticação do disco e recusa mídia
gravada com código não assinado.

Também não basta copiar o `OPNPS2LD.ELF` para o Memory Card: isso apenas guarda um arquivo. O que
torna o console capaz de bootar homebrew é o **FreeMcBoot instalado**, que tem um instalador
próprio.

### As camadas

```
┌─────────────────────────────────────────────────────┐
│  1. PONTO DE ENTRADA        (uma vez, o passo chato) │
│     FreeDVDBoot (DVD-R) · MC pronto com FMCB ·       │
│     modchip                                          │
└───────────────────────┬─────────────────────────────┘
                        ▼
┌─────────────────────────────────────────────────────┐
│  2. FreeMcBoot no Memory Card         (uma vez)      │
│     Faz o console bootar homebrew ao ligar           │
│     Ocupa poucos KB — é só a chave de ignição        │
└───────────────────────┬─────────────────────────────┘
                        ▼
┌─────────────────────────────────────────────────────┐
│  3. Pendrive                    (o sistema inteiro)  │
│     RETROHUB.ELF · jogos · ART/ · THM/ · CFG/ · VMC/ │
└─────────────────────────────────────────────────────┘
```

| Caminho de entrada | Mídia | Observação |
|---|---|---|
| **FreeDVDBoot** | **DVD-R** | Explora o player de DVD embutido. **Não é CD-R.** Compatibilidade depende de modelo e firmware |
| **Memory Card já com FMCB** | — | O mais simples: chega pronto, é só plugar |
| **Modchip** | — | Solução de hardware |

Depois disso o Memory Card guarda apenas o FMCB, que pode lançar um ELF **direto do pendrive**
(`mass:/RETROHUB.ELF`). É a configuração recomendada.

---

## 3. Formato do pendrive

Confirmado no README do OPL (seção *USB/MX4SIO/iLink*):

| Sistema de arquivos | Situação |
|---|---|
| **exFAT** | Suportado desde a v1.2.0 beta (rev1880). **Recomendado** — ISO de qualquer tamanho fica inteira |
| **FAT32** | Funciona, mas jogo acima de 4 GB **precisa** ser convertido para o formato USBExtreme (UL) |

Ambos exigem **tabela de partição MBR**, não GPT.

### Fragmentação

O limite é de **64 fragmentos por arquivo** (`BDM_MAX_FRAGS`, `cdvd_config.h:65`). Acima disso o
jogo não abre.

O README do OPL é explícito: **não usar programas de desfragmentação.** O procedimento correto é
copiar tudo para o PC, formatar o pendrive e copiar de volta.

### Estrutura de pastas

```
<pendrive>/
  OPNPS2LD.ELF   (ou RETROHUB.ELF)
  CD/            ISOs de CD
  DVD/           ISOs de DVD
  ART/           <startup>_COV.png  — capas 192×276, PNG-8
  CFG/           <startup>.cfg      — metadados
  THM/           temas
  LNG/           idiomas
  VMC/           memory cards virtuais
  CHT/           cheats
  APPS/          homebrew
  RH/            library.idx, favorites.cfg, recent.cfg
```

---

## 4. O ciclo de desenvolvimento

**O PS2 nunca é conectado ao PC.** O pendrive faz a ponte:

```
 [ PC Linux ]                          [ PS2 ]
      │                                   │
  ./tools/build.sh                        │
      │  RETROHUB.ELF                     │
      ▼                                   │
  copia no pendrive ──── pendrive ───────►│  FMCB → RETROHUB.ELF
                                          │  testa
      ◄──────────────── pendrive ─────────┘
```

Cerca de dois minutos por rodada. **Nenhuma gravação de disco em nenhum momento.**

### Build em um comando

```bash
./tools/build.sh                                  # gera RETROHUB.ELF
./tools/build.sh -o /run/media/$USER/RETROHUB     # gera e já copia
./tools/build.sh debug                            # com log por rede (UDPTTY)
```

O script usa o container oficial `ghcr.io/ps2homebrew/ps2homebrew:main`, então não é preciso
instalar PS2SDK. Requer apenas Docker.

### Depuração por rede

`make debug` (ou `./tools/build.sh debug`) ativa o **UDPTTY**: o console envia as mensagens de
`LOG()` pela rede para o PC, em vez de escrevê-las no vazio. É a diferença entre depurar com
informação e depurar com tela preta.

Requer conexão de rede no console: os modelos slim têm ethernet integrado; os fat precisam do
Network Adapter na baia de expansão.

### Sequência recomendada na primeira vez

Isole as variáveis — não deixe que a primeira build com código novo seja também o primeiro teste
da toolchain:

1. Compile o **OPL original, sem modificação**, e confirme que o ELF gerado boota no console.
   Isso valida a toolchain sozinha.
2. Só então aplique mudanças. Se algo quebrar a partir daí, é o código — não o ambiente.

### Recomendações

1. **Manter o `OPNPS2LD.ELF` original no pendrive.** O RetroHub sai como `RETROHUB.ELF`, nome
   diferente de propósito: se a build nova travar, o OPL original continua ali como plano B.
2. **Configurar o FMCB com as duas entradas** no menu, para alternar sem mexer em arquivo.
3. **Um segundo pendrive** com a configuração conhecida-boa, para isolar se um problema é do
   build ou da mídia.
4. **Anotar o modelo exato** (SCPH-xxxxx) e se é fat ou slim — a suíte de regressão precisa cobrir
   os dois, e algumas diferenças (Deckard/`xparam.c`) só aparecem em slims tardios.

---

## 5. Velocidade por dispositivo

Relevante para a medição de carregamento de capa da Fase 0 e para a experiência de uso:

| Dispositivo | Velocidade | Observação |
|---|---|---|
| **USB** | Lento — o PS2 tem **USB 1.1** | O caso mais desfavorável, e por isso o alvo de otimização |
| **MX4SIO** (SD no slot de MC) | Mais rápido que USB | Requer adaptador |
| **HDD interno** (fat) | O mais rápido | Só nos modelos com baia de expansão |
| **SMB** (rede) | Varia | Depende da rede e do servidor |

Se a biblioteca for grande e a navegação com capas parecer lenta no USB, o gargalo é o USB 1.1 —
não o RetroHub. É exatamente essa a hipótese que a medição da Fase 0 vai confirmar ou derrubar,
e é o que decide se o `RH/covers.pak` entra ou não
(ver [`06-decisao-capas.md`](06-decisao-capas.md#plano-b-condicional-rhcoverspak)).

---

## 6. O que ainda precisa ser confirmado

Coisas que dependem do console específico e que este documento não pode decidir:

- **Qual ponto de entrada funciona** no modelo e firmware em questão
- **Se o adaptador HDMI força overscan**, o que decidiria entre margem de 16 px e 24 px
  (ver [`03-arquitetura`](03-arquitetura-retrohub-ps2.md#geometria-padrão-do-tema))
- **Se o console suporta 480p** pela saída em uso, o que libera linhas de 1 px e texto menor
