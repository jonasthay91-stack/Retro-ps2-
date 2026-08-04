# RetroHub Manager — Especificação (Projeto Futuro)

> Aplicativo de PC (Windows) que prepara e mantém a biblioteca usada pelo RetroHub PS2.
> Depende apenas do formato de índice e do esquema de metadados definidos na
> [Fase 2](04-plano-de-desenvolvimento.md#fase-2--índice-e-metadados).

---

## 1. Papel do Manager

O PS2 tem 32 MB de RAM e um processador de 294 MHz. Buscar capas na internet, ler bases de dados,
redimensionar imagens e calcular índices **não é trabalho para o console**.

O Manager existe para que o PS2 só precise ler.

```
┌──────────────── PC (RetroHub Manager) ─────────────────┐
│  Detecta ISOs → lê código do disco → busca metadados   │
│  → baixa capas → gera miniaturas → organiza pastas     │
│  → constrói library.idx → grava no pendrive/HD         │
└────────────────────────┬───────────────────────────────┘
                         │ USB / rede
┌────────────────────────▼───────────────────────────────┐
│  PS2 (RetroHub): lê o índice, exibe, lança             │
└────────────────────────────────────────────────────────┘
```

---

## 2. Funcionalidades

### 2.1 Detecção de ISOs

- Varredura recursiva de pastas escolhidas pelo usuário
- Formatos: `.iso`, `.zso`, `.bin`/`.cue` (com conversão), formato UL multi-parte
- Identificação do tipo de mídia (CD vs DVD) pelo tamanho e estrutura
- Detecção de DVD dual-layer (mesma lógica de `sbProbeISO9660`)

### 2.2 Leitura do código do disco

Réplica exata do que o OPL faz em `scanForISO()`:
1. Monta/lê a ISO como ISO9660
2. Abre `SYSTEM.CNF`
3. Extrai a linha `BOOT2 = cdrom0:\SLUS_200.02;1`
4. Normaliza para o formato `startup` de 11 caracteres (`SLUS_200.02`)

Prefixos reconhecidos: `SLUS`, `SLES`, `SCES`, `SCUS`, `SLPS`, `SLPM`, `SCPS`, `SCAJ`, `SLKA`,
`SCKA`, `TCES`, `PBPX`, entre outros.

Para `.zso`, descomprimir apenas os setores necessários.

### 2.3 Busca automática de metadados

Arquitetura de **provedores plugáveis**, nenhum obrigatório:

| Provedor | Fornece |
|---|---|
| Base local embarcada | Ano, gênero, desenvolvedora, jogadores, região (offline, sempre disponível) |
| Provedores online (configuráveis) | Capa, fundo, descrição, complementos |
| Entrada manual | Tudo — sempre disponível como recurso final |

Campos coletados: capa, fundo, descrição, gênero, ano, desenvolvedora, número de jogadores,
região, compatibilidade conhecida.

**Princípios:**
- O Manager funciona 100% offline com a base local + entrada manual
- Nenhum provedor online é obrigatório; a ausência de um provedor degrada, não quebra
- Cache local de tudo que for baixado, para não repetir requisições
- O usuário sempre pode sobrescrever qualquer campo obtido automaticamente

### 2.4 Processamento de imagens

O passo que mais economiza RAM no console:

| Saída | Tamanho | Formato | Uso |
|---|---|---|---|
| `<startup>_COV.png` | 256×366 | PNG paletizado 8 bits | Capa em foco |
| `<startup>_THM.png` | 120×172 | PNG paletizado 8 bits | Miniatura da grade |
| `<startup>_BG.png` | 640×480 | PNG paletizado 8 bits | Fundo por jogo (opcional) |
| `<startup>_ICO.png` | 64×64 | PNG paletizado 8 bits | Ícone |

Quantização para 256 cores com dithering. **Esta conversão é a diferença entre um cache de capas
de 1,5 MB e um de 6 MB no console.**

Validação: rejeitar imagens acima de `720×512` (limite de `textures.c:104`).

### 2.5 Organização de pastas

Cria e mantém a estrutura que o OPL/RetroHub espera:

```
<destino>/
  CD/       ISOs de CD
  DVD/      ISOs de DVD
  CFG/      <startup>.cfg  metadados
  ART/      <startup>_COV.png, _THM.png, _BG.png, _ICO.png
  THM/      temas
  LNG/      idiomas
  VMC/      memory cards virtuais
  CHT/      cheats
  APPS/     homebrew
  RH/       library.idx, favorites.cfg, recent.cfg
  ul.cfg
```

Funções:
- Mover/copiar ISOs para `CD/` ou `DVD/` conforme a mídia
- Renomear jogos (respeitando o limite de 160 caracteres de `ISO_GAME_NAME_MAX`)
- Converter para o formato UL quando o sistema de arquivos exigir (FAT32 e arquivos > 4 GB)
- Manter `ul.cfg` consistente ao adicionar/remover jogos

### 2.6 Geração do índice

Produz `RH/library.idx` no formato definido em
[`03-arquitetura`](03-arquitetura-retrohub-ps2.md#22-a-solução-índice-binário):
cabeçalho + entradas de 64 bytes + tabela de strings + `sourceHash`.

Isso elimina a varredura de ISOs no primeiro boot do console — o maior custo de inicialização.

### 2.7 Preparação de mídia

- Detectar pendrives e HDs conectados
- Verificar sistema de arquivos (FAT32/exFAT) e tamanho de cluster
- **Verificar fragmentação de cada ISO** e avisar quando ultrapassar 64 fragmentos
  (`BDM_MAX_FRAGS`) — a causa nº 1 de "o jogo não abre"
- Estimar espaço necessário antes de copiar
- Copiar com verificação de integridade
- Gerar VMCs (reusando a lógica de `pc/genvmc/`)

### 2.8 Manutenção

- Atualizar capas de jogos já presentes
- Reprocessar metadados
- Reconstruir o índice
- Detectar jogos órfãos (entrada sem arquivo) e arquivos órfãos (arquivo sem entrada)
- Exportar/importar a biblioteca (backup dos metadados)

---

## 3. Arquitetura proposta

### 3.1 Separação em três camadas

```
┌───────────────────────────────────────────────┐
│  UI (Windows)                                 │
└──────────────────────┬────────────────────────┘
┌──────────────────────▼────────────────────────┐
│  RetroHub.Core  — biblioteca multiplataforma  │
│  ISO · SYSTEM.CNF · metadados · imagens ·     │
│  índice · organização de pastas · UL          │
└──────────────────────┬────────────────────────┘
┌──────────────────────▼────────────────────────┐
│  RetroHub.Cli  — mesma lógica, sem interface  │
└───────────────────────────────────────────────┘
```

Separar a lógica da interface permite: CLI para automação, port futuro para Linux/macOS (o público
de PS2 usa muito Linux), e testes automatizados de verdade.

### 3.2 Escolha de tecnologia

| Opção | A favor | Contra |
|---|---|---|
| **C# / .NET + WinUI ou Avalonia** | Produtivo, bom suporte a imagens, Avalonia dá multiplataforma | Runtime |
| **Rust + Tauri** | Binário pequeno, rápido, seguro | Curva de aprendizado |
| **C++ / Qt** | Reusa `iso2opl`/`genvmc` diretamente | Mais lento de desenvolver |

**Recomendação: C# com Avalonia.** O gargalo do Manager é I/O e rede, não CPU. Produtividade
importa mais, e Avalonia entrega Windows + Linux com um código só. As ferramentas existentes em
`pc/` (`iso2opl`, `opl2iso`, `genvmc`) podem ser invocadas como processos ou reimplementadas
conforme a necessidade.

---

## 4. Fluxos principais

### 4.1 Primeira configuração

```
1. Escolher pasta com as ISOs
2. Escolher o destino (pendrive / HD / pasta local)
3. Varrer → lista de jogos detectados com o código do disco
4. Buscar metadados → prévia com capas
5. Revisar e ajustar manualmente o que estiver errado
6. Confirmar → copia, organiza, processa imagens, gera índice
7. Relatório: N jogos prontos, N avisos (fragmentação, capa ausente, metadado incompleto)
```

### 4.2 Adicionar um jogo

```
1. Arrastar a ISO para a janela
2. Detecção automática do código do disco
3. Metadados e capa buscados
4. Confirmar → copiado, índice atualizado incrementalmente
```

### 4.3 Corrigir metadados

```
1. Selecionar o jogo na biblioteca
2. Editar qualquer campo
3. Substituir a capa (arquivo local ou nova busca)
4. Salvar → CFG/<startup>.cfg e o índice são atualizados
```

---

## 5. Princípios de projeto

1. **Nunca destruir.** Nenhuma operação apaga ISOs do usuário sem confirmação explícita.
   Toda gravação é atômica.
2. **Sempre reversível.** O `.cfg` de cada jogo é texto legível e editável à mão. O índice é
   descartável e regenerável.
3. **Compatível com o OPL original.** Uma mídia preparada pelo Manager funciona no OPL padrão sem
   nenhuma modificação. As chaves `$RH_*` são simplesmente ignoradas por ele.
4. **Offline em primeiro lugar.** Sem internet, o Manager continua fazendo tudo exceto buscar
   metadados online.
5. **Honesto sobre o que não sabe.** Metadado não encontrado é exibido como "desconhecido", nunca
   inventado.

---

## 6. Fora de escopo (por ora)

- Conversão de vídeo ou áudio
- Aplicação de patches em jogos
- Gerenciamento de saves reais (Memory Card físico)
- Streaming de jogos para o console
- Edição de temas (merece uma ferramenta própria)

---

## 7. Dependência com o console

O Manager e o RetroHub PS2 compartilham exatamente **dois contratos**:

1. O formato binário de `RH/library.idx`
2. O esquema de chaves de `CFG/<startup>.cfg` (existentes do OPL + `$RH_*`)

Ambos são definidos na Fase 2 do plano do console. Enquanto esses dois contratos não estiverem
estáveis, o Manager não deve ser implementado — qualquer trabalho anterior seria refeito.

O campo `version` no cabeçalho do índice permite evolução: o console aceita índices de versão
menor ou igual à sua e reconstrói quando encontra algo mais novo do que entende.
