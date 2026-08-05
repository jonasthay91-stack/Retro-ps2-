#!/usr/bin/env bash
# Guarda do motor — regra RI-1 de docs/02-riscos-e-compatibilidade.md
#
# O que faz um jogo rodar não é a interface: é o ee_core, o cdvdman e a
# sequência de patch em system.c. Uma alteração ali pode transformar
# "98% dos jogos funcionam" em "nada roda", e o sintoma é tela preta sem log.
#
# Este script falha se um commit tocar esses caminhos. Rode local antes de
# enviar, ou deixe o CI rodar.
#
#   ./tools/check-engine-frozen.sh                 compara com origin/master
#   ./tools/check-engine-frozen.sh <base> <head>   compara duas refs
#
# Para uma mudança deliberada no motor, use a label `engine-change` no PR
# (o CI a reconhece) — nunca contorne a guarda em silêncio.

set -euo pipefail

FROZEN=(
    "ee_core/"
    "modules/"
    "src/system.c"
    "src/ioprp.c"
    "src/xparam.c"
)

# Funções cujo layout binário é compartilhado com IOP/ee_core.
FROZEN_SYMBOLS=(
    "sbPrepare"
    "sysLaunchLoaderElf"
    "sendIrxKernelRAM"
    "initKernel"
)

BASE="${1:-}"
HEAD="${2:-HEAD}"

if [ -z "$BASE" ]; then
    for ref in origin/master origin/main master main; do
        if git rev-parse --verify --quiet "$ref" >/dev/null; then BASE="$ref"; break; fi
    done
fi

if [ -z "$BASE" ]; then
    echo "aviso: nenhuma base de comparação encontrada; nada a verificar." >&2
    exit 0
fi

CHANGED=$(git diff --name-only "$BASE...$HEAD" 2>/dev/null || git diff --name-only "$BASE" "$HEAD")

VIOLATIONS=""
while IFS= read -r file; do
    [ -z "$file" ] && continue
    for path in "${FROZEN[@]}"; do
        case "$file" in
            "$path"*) VIOLATIONS+="  $file    (congelado: $path)"$'\n' ;;
        esac
    done
done <<< "$CHANGED"

# supportbase.c é zona mista: só as funções do motor são congeladas.
if grep -qx "src/supportbase.c" <<< "$CHANGED"; then
    DIFF=$(git diff "$BASE...$HEAD" -- src/supportbase.c 2>/dev/null || true)
    for sym in "${FROZEN_SYMBOLS[@]}"; do
        if grep -qE "^[-+].*\b$sym\b" <<< "$DIFF"; then
            VIOLATIONS+="  src/supportbase.c    (função congelada: $sym)"$'\n'
        fi
    done
fi

if [ -n "$VIOLATIONS" ]; then
    cat >&2 <<EOF

╭──────────────────────────────────────────────────────────────────╮
│  GUARDA DO MOTOR — alteração bloqueada                           │
╰──────────────────────────────────────────────────────────────────╯

Arquivos do motor de compatibilidade foram modificados:

$VIOLATIONS
O RetroHub muda a interface, não o motor. Estes caminhos são somente
leitura (regra RI-1). Alterá-los arrisca a compatibilidade de todos os
jogos, e a falha aparece como tela preta sem log — praticamente
impossível de depurar depois.

O que fazer:
  • Se foi sem querer:  git checkout $BASE -- <arquivo>
  • Se a mudança pertence à UI, coloque-a num arquivo src/rh_*.c
  • Se é mesmo uma mudança de motor, deliberada e testada em hardware
    real com os 20 jogos de regressão, marque o PR com a label
    'engine-change'

Referência: docs/02-riscos-e-compatibilidade.md (R-01, RI-1, RI-2)

EOF
    exit 1
fi

echo "guarda do motor: OK — nenhum caminho congelado foi tocado ($BASE...$HEAD)"
