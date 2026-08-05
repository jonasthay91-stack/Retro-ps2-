# RetroHub PS2

Um launcher moderno para PlayStation 2, construído sobre o motor do
[Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader).

Mantém integralmente a compatibilidade com **USB, HDD, SMB, MX4SIO, iLink e Memory Card** — o
carregamento dos jogos continua sendo feito pelo motor do OPL. O que muda é a interface e a
experiência de uso.

---

## Estado atual

> **Fase 0 concluída em hardware real. Fase 1 em andamento.**

A análise produziu a documentação técnica completa da arquitetura do OPL, a análise de riscos e a
proposta de arquitetura do RetroHub — sem alterar um arquivo sequer.

A **Fase 0** fechou com a cadeia inteira validada num PS2 slim: compilar no PC → copiar o
`RETROHUB.ELF` para o pendrive → lançar pelo FMCB → listar o jogo → jogar. O binário era o OPL sem
modificação, exatamente como o plano exigia: primeiro provar a toolchain, depois mexer no código.

A **Fase 1** começou. São mudanças invisíveis na tela e decisivas embaixo dela — o que impede a
interface de engasgar quando a biblioteca crescer. Ver [`DIVERGENCIAS.md`](DIVERGENCIAS.md).

### Como o código é entregue

O código do OPL **não é copiado para este repositório**. Ele fica num fork limpo, e as mudanças
vivem em [`patches/`](patches/):

```bash
./tools/rh-apply.sh ~/Open-PS2-Loader     # aplica (idempotente)
./tools/rh-apply.sh --status              # mostra o que está aplicado
./tools/rh-apply.sh --revert              # volta ao OPL original
./tools/run-tests.sh                      # testes de lógica, rodam no PC
```

Assim `git diff` no fork mostra exatamente a divergência, e há sempre um caminho de volta.

---

## Documentação

| Documento | Conteúdo |
|---|---|
| [**01 — Análise da arquitetura do OPL**](docs/01-analise-arquitetura-opl.md) | Estrutura do projeto, linguagem, renderização, menus, capas, memória, detecção de dispositivos, leitura de ISOs, carregamento de jogos, temas, compilação e o que pode ser modificado com segurança |
| [**02 — Riscos e compatibilidade**](docs/02-riscos-e-compatibilidade.md) | 12 riscos avaliados, 10 regras invioláveis, estratégia de teste |
| [**03 — Arquitetura do RetroHub PS2**](docs/03-arquitetura-retrohub-ps2.md) | Modelo de dados, telas, extensões de tema, identidade visual, orçamentos de memória e desempenho |
| [**04 — Plano de desenvolvimento**](docs/04-plano-de-desenvolvimento.md) | 8 fases com entregáveis, critérios de aceite e gatilhos de parada |
| [**05 — RetroHub Manager (PC)**](docs/05-retrohub-manager-pc.md) | Especificação do aplicativo de PC |
| [**06 — Decisão: formato das capas**](docs/06-decisao-capas.md) | Capa única 192×276 em PNG paletizado 8 bits — a escolha mais leve e mais simples |
| [**07 — Identidade visual**](docs/07-identidade-visual.md) | O que foi aproveitado e rejeitado das referências, e a assinatura visual: a capa vira o fundo |
| [**08 — Console de teste**](docs/08-console-de-teste.md) | Como preparar um PS2 para receber builds — pré-requisito da Fase 0 |
| [**Divergências**](DIVERGENCIAS.md) | Tudo o que o RetroHub muda no código do OPL, e por quê |

---

## O que a análise concluiu

O OPL é, na prática, dois programas em um:

- **Um motor de compatibilidade** — `ee_core` + `modules/iopcore` + `sbPrepare()` +
  `sysLaunchLoaderElf()`. É o resultado de mais de uma década de engenharia reversa, é
  extremamente sensível, e o RetroHub **não toca em nenhuma linha dele**.
- **Um front-end** — `gui`, `menusys`, `themes`, `renderman`, `fntsys`, `texcache`. Bem
  estruturado, desacoplado por uma vtable limpa (`item_list_t`) e por um motor de temas
  data-driven. **Totalmente substituível.**

A fronteira entre os dois é nítida. Isso torna o objetivo do projeto — nova experiência sobre o
mesmo motor — o caminho que a própria arquitetura do OPL já sugere.

Quatro descobertas que economizam trabalho significativo:

1. **Os metadados já existem.** O tema padrão do OPL já lê `Title`, `Genre`, `Release`,
   `Developer` e `Description` de `CFG/<startup>.cfg`. Falta apenas acrescentar jogadores,
   região, favorito e última partida.
2. **O cache de capas já é assíncrono, LRU e com proteção anti-thrash.** É a base pronta para uma
   grade de capas.
3. **O motor de temas é extensível por design.** Adicionar `CoverGrid` ou `GameCard` é aditivo e
   não quebra nenhum dos temas existentes da comunidade.
4. **A vtable `item_list_t` isola completamente a UI dos dispositivos.** Uma interface nova pode
   ser escrita sem tocar em `bdmsupport.c`, `hddsupport.c` ou `ethsupport.c`.

---

## Funcionalidades planejadas

**Tela inicial** — PS2 · PS1 · Emuladores · Homebrew · Favoritos · Recentes · Configurações

**Tela de jogos** — grade de capas grandes com painel lateral exibindo capa, nome, ano,
desenvolvedora, gênero, número de jogadores, região, código do disco, compatibilidade, última vez
jogado e favorito.

**Navegação** — busca por nome, ordenação por nome/ano/gênero/desenvolvedora, filtros por
categoria e gênero, favoritos e recentes.

**Personalização** — sistema de temas, configurações visuais, fundo personalizado, tela inicial
personalizável, música opcional e sons de navegação.

---

## Princípios de projeto

O PS2 tem **32 MB de RAM**, **4 MB de VRAM** e um processador de **294 MHz**. Toda decisão de
interface é tomada dentro desse orçamento — não como limitação a contornar, mas como a restrição
que dá forma ao produto.

| Recurso | Orçamento |
|---|---|
| Heap da interface | ≤ 8 MB (uso previsto: 4,0 MB) |
| Cache de 24 capas | 1,27 MB (cobre 3–4 páginas da grade) |
| Primitivas de desenho por frame | ≤ 80 |
| Taxa de quadros | 60 (NTSC) / 50 (PAL) estáveis |
| Tempo até a tela inicial | ≤ 3 s |
| Ordenação de 1.000 jogos | ≤ 100 ms |

**Regras invioláveis:**
- O motor (`ee_core/`, `modules/`, `system.c`, `sbPrepare`) é somente leitura.
- O layout de pastas e o formato de configuração do OPL são preservados — uma mídia preparada pelo
  RetroHub continua funcionando no OPL original.
- Temas legados do OPL renderizam idênticos.
- Nenhuma release sem teste em PS2 físico.

A lista completa está em [`02-riscos-e-compatibilidade.md`](docs/02-riscos-e-compatibilidade.md#2-regras-invioláveis-do-projeto).

---

## Próximo passo

**Fase 1** — fundações invisíveis: ordenação O(n log n), tabela de despacho no parser de temas,
instrumentação de heap e primitivas.

Na primeira vez, os dois repositórios precisam existir lado a lado:

```bash
git clone https://github.com/ps2homebrew/Open-PS2-Loader.git ~/Open-PS2-Loader
git clone https://github.com/jonasthay91-stack/Retro-ps2-.git ~/Retro-ps2-
```

Daí em diante:

```bash
cd ~/Retro-ps2-
git pull
./tools/run-tests.sh                     # verifica a lógica no PC
./tools/rh-apply.sh ~/Open-PS2-Loader    # aplica os patches no fork
./tools/build.sh -p                      # compila e copia pro pendrive
./tools/check-engine-frozen.sh           # confere se algum caminho do motor foi tocado
```

Os scripts acham o fork sozinhos; `-C ~/Open-PS2-Loader` força um caminho específico.

O ciclo de teste é: compilar no PC, copiar o ELF para o pendrive, ligar o console. O PS2 nunca é
conectado ao PC — ver [`08-console-de-teste.md`](docs/08-console-de-teste.md).

---

## Créditos

Este projeto se apoia inteiramente no trabalho da equipe do **Open PS2 Loader** e da comunidade
PS2 Homebrew. O motor de compatibilidade que faz os jogos rodarem é obra deles; o RetroHub apenas
constrói uma nova experiência de uso sobre ele.

O OPL é licenciado sob a **Academic Free License v3.0**.
