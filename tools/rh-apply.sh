#!/usr/bin/env bash
# RetroHub PS2 — aplica as mudancas do RetroHub sobre um fork do Open PS2 Loader.
#
#   ./tools/rh-apply.sh                aplica no fork (procura sozinho)
#   ./tools/rh-apply.sh ~/Open-PS2-Loader
#   ./tools/rh-apply.sh --status       so mostra o que esta aplicado
#   ./tools/rh-apply.sh --revert       desfaz tudo, volta ao OPL original
#
# Este repositorio guarda a documentacao, as ferramentas e os patches. O codigo
# do OPL fica no fork, intocado no git dele — assim `git diff` no fork mostra
# exatamente o que o RetroHub mudou, e `--revert` devolve o OPL original.
#
# O script e idempotente: rodar duas vezes nao aplica nada duas vezes.

set -euo pipefail

cd "$(dirname "$0")/.."
RH_ROOT="$PWD"
PATCH_DIR="$RH_ROOT/patches"
OVERLAY_DIR="$RH_ROOT/overlay"

MODE="apply"
FORK=""

while [ $# -gt 0 ]; do
    case "$1" in
        --status) MODE="status"; shift ;;
        --revert) MODE="revert"; shift ;;
        -h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "erro: argumento desconhecido '$1'" >&2; exit 1 ;;
        *) FORK="$1"; shift ;;
    esac
done

# ---- localizar o fork do OPL ------------------------------------------------
is_opl() { [ -f "$1/Makefile" ] && [ -f "$1/src/menusys.c" ] && [ -d "$1/ee_core" ]; }

if [ -z "$FORK" ]; then
    for cand in "${RETROHUB_OPL:-}" "$RH_ROOT" "$PWD" "$HOME/Open-PS2-Loader" \
                "$HOME/OPL" "$RH_ROOT/../Open-PS2-Loader"; do
        [ -n "$cand" ] && is_opl "$cand" && { FORK="$cand"; break; }
    done
fi

if [ -z "$FORK" ]; then
    cat >&2 <<'EOF'
erro: nao encontrei o fork do Open PS2 Loader.

Passe o caminho:

    ./tools/rh-apply.sh ~/Open-PS2-Loader

ou defina a variavel de ambiente RETROHUB_OPL. Se ainda nao clonou:

    git clone https://github.com/ps2homebrew/Open-PS2-Loader.git ~/Open-PS2-Loader
EOF
    exit 1
fi

FORK="$(cd "$FORK" && pwd)"
is_opl "$FORK" || { echo "erro: '$FORK' nao parece ser o fork do OPL." >&2; exit 1; }
[ -d "$FORK/.git" ] || { echo "erro: '$FORK' nao e um repositorio git; sem git nao da para aplicar nem reverter." >&2; exit 1; }

echo "==> fork: $FORK"

shopt -s nullglob
PATCHES=("$PATCH_DIR"/*.patch)
shopt -u nullglob

# Arquivos novos (rh_*.c, rh_*.h) sao copiados inteiros, nao aplicados como
# patch: nao existe nada no OPL com que eles conflitem, e um arquivo legivel
# vale mais que um diff gigante contra /dev/null. Caminhos relativos a overlay/.
overlay_files() {
    [ -d "$OVERLAY_DIR" ] || return 0
    ( cd "$OVERLAY_DIR" && find . -type f | sed 's|^\./||' | sort )
}

if [ ${#PATCHES[@]} -eq 0 ] && [ -z "$(overlay_files)" ]; then
    echo "==> nada em patches/ nem em overlay/ — nada a fazer"
    exit 0
fi

# Estado de um patch: applied / pending / conflict
state_of() {
    if git -C "$FORK" apply --check --reverse "$1" >/dev/null 2>&1; then
        echo applied
    elif git -C "$FORK" apply --check "$1" >/dev/null 2>&1; then
        echo pending
    else
        echo conflict
    fi
}

case "$MODE" in
status)
    for p in "${PATCHES[@]}"; do
        s=$(state_of "$p")
        case "$s" in
            applied)  mark="[x] aplicado" ;;
            pending)  mark="[ ] pendente" ;;
            *)        mark="[!] CONFLITO" ;;
        esac
        printf '  %-14s %s\n' "$mark" "$(basename "$p")"
    done
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        if [ -f "$FORK/$f" ] && cmp -s "$OVERLAY_DIR/$f" "$FORK/$f"; then
            printf '  %-14s %s\n' "[x] copiado" "$f"
        elif [ -f "$FORK/$f" ]; then
            printf '  %-14s %s\n' "[~] DIFERE" "$f"
        else
            printf '  %-14s %s\n' "[ ] ausente" "$f"
        fi
    done <<< "$(overlay_files)"
    ;;

apply)
    APPLIED=0
    for p in "${PATCHES[@]}"; do
        name=$(basename "$p")
        case "$(state_of "$p")" in
            applied) echo "  ja aplicado: $name" ;;
            pending)
                git -C "$FORK" apply "$p"
                echo "  aplicado:    $name"
                APPLIED=$((APPLIED + 1))
                ;;
            conflict)
                echo >&2
                echo "erro: '$name' nao aplica nem esta aplicado." >&2
                echo "      O fork provavelmente esta em um commit diferente do esperado," >&2
                echo "      ou tem alteracoes locais nos mesmos trechos." >&2
                echo "      Confira com: git -C '$FORK' status" >&2
                exit 1
                ;;
        esac
    done
    echo "==> $APPLIED patch(es) novo(s) aplicado(s)"

    COPIED=0
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        if [ -f "$FORK/$f" ] && cmp -s "$OVERLAY_DIR/$f" "$FORK/$f"; then
            echo "  ja copiado: $f"
        else
            mkdir -p "$FORK/$(dirname "$f")"
            cp -f "$OVERLAY_DIR/$f" "$FORK/$f"
            echo "  copiado:    $f"
            COPIED=$((COPIED + 1))
        fi
    done <<< "$(overlay_files)"
    [ "$COPIED" -gt 0 ] && echo "==> $COPIED arquivo(s) do overlay copiado(s)"

    if [ -x "$RH_ROOT/tools/check-engine-frozen.sh" ]; then
        echo "==> verificando o congelamento do motor"
        ( cd "$FORK" && "$RH_ROOT/tools/check-engine-frozen.sh" )
    fi

    cat <<EOF

Proximo passo:
  cd "$FORK" && "$RH_ROOT/tools/build.sh" -p
EOF
    ;;

revert)
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        # So remove o que e identico ao nosso: se o arquivo foi editado no fork,
        # apagar destruiria trabalho de quem editou.
        if [ -f "$FORK/$f" ] && cmp -s "$OVERLAY_DIR/$f" "$FORK/$f"; then
            rm -f "$FORK/$f"
            echo "  removido:    $f"
        elif [ -f "$FORK/$f" ]; then
            echo "  MANTIDO (foi editado no fork): $f"
        fi
    done <<< "$(overlay_files)"

    # De tras para frente, para que patches encadeados saiam na ordem certa.
    for ((i = ${#PATCHES[@]} - 1; i >= 0; i--)); do
        p="${PATCHES[$i]}"; name=$(basename "$p")
        if git -C "$FORK" apply --check --reverse "$p" >/dev/null 2>&1; then
            git -C "$FORK" apply --reverse "$p"
            echo "  revertido:   $name"
        else
            echo "  nao aplicado: $name"
        fi
    done
    echo "==> fork de volta ao OPL original"
    ;;
esac
