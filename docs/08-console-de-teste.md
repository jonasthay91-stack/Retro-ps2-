# Console de Teste — Pré-requisito da Fase 0

> Como deixar um PS2 pronto para receber builds do RetroHub, e por que isso precisa existir
> antes de qualquer linha de código.
>
> **Aviso:** compatibilidade de exploits varia por modelo (SCPH-xxxxx), região e firmware.
> Este documento descreve o caminho conceitual; os detalhes do seu console específico precisam
> ser conferidos.

---

## 1. O mal-entendido mais comum

> *"Gravo o OPL num CD e rodo."*

**Não funciona num console de fábrica.** O PS2 verifica a autenticação do disco e recusa mídia
gravada com código não assinado. Gravar `OPNPS2LD.ELF` num CD-R e esperar que rode é o erro
número um de quem começa.

É preciso um **ponto de entrada** (exploit) antes. Depois disso, nunca mais se grava disco.

---

## 2. As três camadas

```
┌─────────────────────────────────────────────────────┐
│  1. PONTO DE ENTRADA        (uma vez, o passo chato) │
│     FreeDVDBoot · MC pronto com FMCB · modchip       │
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

### Camada 1 — ponto de entrada

| Caminho | Mídia | Observação |
|---|---|---|
| **FreeDVDBoot** | **DVD-R** | Explora o player de DVD embutido. **Não é CD** — o CD-R não serve para este caminho. Compatibilidade depende de modelo e firmware |
| **Memory Card já com FMCB** | — | O caminho mais simples: chega pronto, é só plugar |
| **Modchip** | — | Solução de hardware |

### Camada 2 — FreeMcBoot

Instalado no Memory Card, faz o console executar homebrew automaticamente ao ligar. É o que a
maioria das pessoas quer dizer com *"deixar o OPL instalado no cartão"* — só que o que fica
instalado é o FMCB, e é ele que chama o OPL.

O FMCB pode ser configurado para lançar um ELF **direto do pendrive**
(`mass:/OPNPS2LD.ELF`). É a configuração recomendada para desenvolvimento.

### Camada 3 — pendrive

Tudo o mais mora aqui. O Memory Card não precisa guardar nada além do FMCB.

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

Esta é a razão pela qual o console de teste é pré-requisito da Fase 0 — sem ele não há verificação
possível, e com ele a iteração fica rápida:

```
build  →  RETROHUB.ELF  →  copiar para o pendrive  →  ligar o PS2  →  testar
```

**Nenhuma gravação de disco em nenhum momento.**

### Recomendações para o console de teste

1. **Manter o OPL original no pendrive** com outro nome (`OPNPS2LD.ELF` intacto, RetroHub como
   `RETROHUB.ELF`). Permite comparar contra o baseline e voltar atrás na hora.
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
