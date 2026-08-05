# Console de Teste — Pré-requisito da Fase 0

> Como deixar um PS2 pronto para receber builds do RetroHub, e por que isso precisa existir
> antes de qualquer linha de código.
>
> **Aviso:** compatibilidade de exploits varia por modelo (SCPH-xxxxx), região e firmware.
> Este documento descreve o caminho conceitual; os detalhes do seu console específico precisam
> ser conferidos.

---

## 1. O Memory Card é a chave — e não dá para eliminá-la

O **FreeMcBoot no Memory Card é o exploit**. É ele que faz o console aceitar executar código não
assinado. Sem ele o PS2 não roda nada, venha de onde vier — pendrive, HD ou rede.

Num **slim sem leitor óptico funcionando**, não existe caminho por software para dispensá-lo:

| Alternativa | Por que não serve aqui |
|---|---|
| FreeHDBoot | Exige HD interno; o slim não tem baia de expansão |
| FreeDVDBoot | Exige leitor óptico funcionando |
| Modchip | Elimina o cartão, mas é solução de hardware |

**Isso não é burocracia.** O cartão fica plugado e nunca mais é tocado — diferente de ter que pôr
um disco a cada vez.

---

## 2. Não é preciso alterar o Memory Card

**Se o console já boota no FMCB, o cartão pode ficar exatamente como está.**

### O leitor óptico não tem relação com gravar no cartão

Essa é a confusão mais comum. O leitor serviria apenas para **instalar** o FMCB pela primeira vez.
Uma vez instalado, ele boota sozinho, e qualquer homebrew lançado a partir dali — inclusive do
pendrive — lê e escreve no Memory Card normalmente. **Um leitor quebrado é irrelevante daí em
diante.**

### O caminho que funciona sem tocar em nada

```
1. Copiar RETROHUB.ELF para a raiz do pendrive          (no PC)
2. Ligar o console  →  menu do FMCB
3. Abrir o uLaunchELF  (presente na maioria das instalações de FMCB)
4. Navegar até mass:  →  selecionar RETROHUB.ELF  →  executar
```

Zero alteração no cartão. O FMCB e o OPL originais continuam intactos, o que garante que sempre
há um sistema funcionando para voltar.

Este já é o ciclo de desenvolvimento completo — um passo a mais que o ideal, mas suficiente.

### Lançamento direto, sem passar pelo uLaunchELF (opcional)

Para pular uma etapa, configura-se o FMCB — e **isso também se faz pelo pendrive**: basta colocar
o FMCB Configurator nele, executá-lo pelo uLaunchELF, e ele grava a configuração no cartão. Nenhum
disco envolvido.

**Não é obrigatório, e convém deixar para depois.** Enquanto o cartão permanece intocado, existe
garantia de retorno a um sistema conhecido.

Quando for feito, vale usar **caminhos alternativos** no mesmo item de menu, tentados em ordem:

| Ordem | Caminho | Papel |
|---|---|---|
| Path1 | `mass:/RETROHUB.ELF` | a build em teste |
| Path2 | `mass:/OPNPS2LD.ELF` | OPL estável no pendrive |
| Path3 | `mc0:/APPS/OPNPS2LD.ELF` | OPL no cartão, último recurso |

Se o pendrive não estiver presente, ou se a build em teste travar, cai sozinho no próximo.

**Se o ELF não for encontrado:** o FMCB pode estar lançando antes de o pendrive ser enumerado.
Há um ajuste de espera por USB nas configurações.

### O que pode ser feito pela rede

| Recurso | Serve? |
|---|---|
| **UDPTTY** (`./tools/build.sh debug`) | **Sim.** O OPL envia as mensagens de `LOG()` pela ethernet para o PC. É a diferença entre depurar com informação e depurar com tela preta |
| **ps2link** (enviar o ELF pela rede) | **Provavelmente não.** O OPL executa `sysReset()` na inicialização, que reinicia o IOP e derruba a conexão do ps2link |
| Jogos por SMB | Sim, mas é outro assunto |

Ou seja: **o pendrive transporta o ELF; a rede mostra o que acontece dentro dele.** Vale testar o
ps2link, mas não convém contar com ele.

---

## 3. O que move para o pendrive: tudo o mais

O Memory Card guarda **apenas o FMCB**, poucos KB. O sistema inteiro vive no pendrive ou HD:

```
   Memory Card                    Pendrive / HD
   ───────────────                ──────────────────────────────
   FMCB                           RETROHUB.ELF     ← nossa build
   (chave de ignição,             OPNPS2LD.ELF     ← plano B
    poucos KB)                    CD/  DVD/  ART/  CFG/
                                  THM/ LNG/  VMC/  CHT/  APPS/
                                  RH/
```

Com isso, atualizar o launcher passa a ser **copiar um arquivo no PC** — em vez de abrir o
uLaunchELF e copiar para o cartão a cada iteração. É exatamente o ciclo que o desenvolvimento
precisa.

### Configurar o FMCB para lançar do pendrive

1. Copie o ELF para a **raiz do pendrive**
2. Boote no FMCB e abra o **FMCB Configurator**
3. Em **OSDSYS Menu Items**, aponte um item para `mass:/RETROHUB.ELF`
4. Salve a configuração no cartão

**Use os caminhos alternativos.** O FMCB aceita mais de um caminho por item, tentados em ordem:

| Ordem | Caminho | Papel |
|---|---|---|
| Path1 | `mass:/RETROHUB.ELF` | a build em teste |
| Path2 | `mass:/OPNPS2LD.ELF` | OPL estável no pendrive |
| Path3 | `mc0:/APPS/OPNPS2LD.ELF` | OPL no cartão, último recurso |

Se o pendrive não estiver plugado, ou se a build em teste travar, ele cai sozinho no próximo.
**Nunca se fica sem sistema** — o que importa quando se está testando código novo toda semana.

**Se o ELF não for encontrado:** o FMCB pode estar lançando antes de o pendrive ser enumerado.
Há um ajuste de espera por USB nas configurações.

### E se o console ainda não tiver FMCB

Nesse caso é preciso um ponto de entrada primeiro: **FreeDVDBoot** gravado em **DVD-R** (não CD-R —
o exploit é do player de DVD, e a compatibilidade depende de modelo e firmware), um **Memory Card
já vendido com FMCB**, ou um **modchip**. Depois disso, o fluxo acima se aplica igual.

Detalhe que confunde: **copiar o `OPNPS2LD.ELF` para o cartão não faz o console bootá-lo.** Isso
apenas guarda um arquivo. O que dá o poder de boot é o FMCB instalado, que tem instalador próprio.

---

## 4. Formato do pendrive

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

## 5. O ciclo de desenvolvimento

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

## 6. Velocidade por dispositivo

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

## 7. O que ainda precisa ser confirmado

Coisas que dependem do console específico e que este documento não pode decidir:

- **Qual ponto de entrada funciona** no modelo e firmware em questão
- **Se o adaptador HDMI força overscan**, o que decidiria entre margem de 16 px e 24 px
  (ver [`03-arquitetura`](03-arquitetura-retrohub-ps2.md#geometria-padrão-do-tema))
- **Se o console suporta 480p** pela saída em uso, o que libera linhas de 1 px e texto menor
