#!/usr/bin/env bash
# RetroHub PS2 — testes de logica que rodam no PC, sem PS2SDK.
#
#   ./tools/run-tests.sh
#
# Nem tudo precisa do console para ser verificado. Algoritmos puros — ordenacao,
# parsing, indice — podem ser compilados com o gcc do PC e comparados contra a
# implementacao original do OPL. Isso pega erro de logica antes de gastar um
# ciclo de "compila, copia no pendrive, liga o console".
#
# O que NAO da para testar assim, e por isso continua exigindo hardware:
# renderizacao, memoria real, temporizacao, dispositivos e carregamento de jogo.

set -euo pipefail

cd "$(dirname "$0")/.."
TESTS="tools/tests"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

command -v gcc >/dev/null 2>&1 || { echo "erro: gcc nao encontrado." >&2; exit 1; }

# Sanitizers ajudam muito aqui: um erro de religamento da lista vira
# use-after-free visivel, em vez de um travamento silencioso no console.
CFLAGS="-O2 -Wall -Wextra -Werror -std=gnu11"
if echo 'int main(void){return 0;}' | gcc -fsanitize=address,undefined -x c - -o "$WORK/probe" 2>/dev/null; then
    CFLAGS="$CFLAGS -fsanitize=address,undefined"
    echo "==> sanitizers ativados"
else
    echo "==> sanitizers indisponiveis, seguindo sem eles"
fi

FAILS=0
for src in "$TESTS"/*.c; do
    [ -e "$src" ] || { echo "nenhum teste em $TESTS/"; exit 0; }
    name=$(basename "$src" .c)
    echo
    echo "=== $name ==="
    # shellcheck disable=SC2086
    if ! gcc $CFLAGS -o "$WORK/$name" "$src"; then
        echo "  FALHOU: nao compilou"
        FAILS=$((FAILS + 1))
        continue
    fi
    "$WORK/$name" || FAILS=$((FAILS + 1))
done

echo
if [ "$FAILS" -ne 0 ]; then
    echo "$FAILS teste(s) falharam"
    exit 1
fi
echo "todos os testes passaram"
