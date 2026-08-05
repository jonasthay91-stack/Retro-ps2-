#!/usr/bin/env bash
# RetroHub PS2 — build em um comando.
#
# Compila o OPL/RetroHub usando o container oficial do PS2 homebrew, sem
# precisar instalar PS2SDK na máquina. Funciona em Linux com Docker.
#
#   ./tools/build.sh                    compila (padrão)
#   ./tools/build.sh debug              compila com log por rede (UDPTTY)
#   ./tools/build.sh clean              limpa
#   ./tools/build.sh -p                 compila e copia pro pendrive (acha sozinho)
#   ./tools/build.sh -o /caminho        compila e copia para um caminho especifico
#   ./tools/build.sh -C ~/Open-PS2-Loader   escolhe o fork explicitamente
#
# O binário sai como RETROHUB.ELF — nome diferente do OPNPS2LD.ELF de propósito,
# para que o OPL original continue no pendrive como plano B.

set -euo pipefail

IMAGE="ghcr.io/ps2homebrew/ps2homebrew:main"
OUT_NAME="RETROHUB.ELF"
TARGET="all"
DEST=""

FORK=""

usage() {
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -o|--output-dir) DEST="${2:-}"; shift 2 ;;
        -p|--pendrive)   DEST="auto"; shift ;;
        -C|--fork)       FORK="${2:-}"; shift 2 ;;
        -h|--help)       usage 0 ;;
        debug|clean|all|release|iopcore_debug|ingame_debug|eesio_debug)
                         TARGET="$1"; shift ;;
        *) echo "erro: argumento desconhecido '$1'" >&2; usage 1 ;;
    esac
done

# O script tanto pode viver dentro do fork do OPL quanto no repositório do
# RetroHub, chamado de fora. Procura a raiz do fork em vez de assumir.
is_opl() { [ -f "$1/Makefile" ] && [ -f "$1/src/menusys.c" ] && [ -d "$1/ee_core" ]; }

SELF_PARENT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "$FORK" ]; then
    for cand in "$PWD" "$SELF_PARENT" "${RETROHUB_OPL:-}" \
                "$HOME/Open-PS2-Loader" "$HOME/OPL" "$SELF_PARENT/../Open-PS2-Loader"; do
        [ -n "$cand" ] && is_opl "$cand" && { FORK="$cand"; break; }
    done
fi

if [ -z "$FORK" ] || ! is_opl "$FORK"; then
    cat >&2 <<'EOF'
erro: não encontrei o fork do Open PS2 Loader.

Este script compila o fork do OPL. Aponte para ele:

    ./tools/build.sh -C ~/Open-PS2-Loader

ou defina a variável de ambiente RETROHUB_OPL. Se ainda não clonou:

    git clone https://github.com/ps2homebrew/Open-PS2-Loader.git ~/Open-PS2-Loader
EOF
    exit 1
fi

cd "$FORK"
ROOT="$PWD"
echo "==> fork: $ROOT"

if ! command -v docker >/dev/null 2>&1; then
    echo "erro: docker não encontrado. Instale o docker e adicione seu usuário ao grupo 'docker'." >&2
    exit 1
fi

# "não consegui falar com o docker" tem duas causas bem diferentes, e a
# solução de uma não resolve a outra. Vale ler a mensagem em vez de adivinhar.
if ! DOCKER_ERR=$(docker info 2>&1 >/dev/null); then
    if printf '%s' "$DOCKER_ERR" | grep -qi 'permission denied'; then
        cat >&2 <<'EOF'
erro: o daemon do docker está rodando, mas seu usuário não tem permissão
      para falar com ele — falta estar no grupo 'docker'.

    sudo usermod -aG docker "$USER"
    newgrp docker                      # vale já nesta janela, sem logout

EOF
    else
        cat >&2 <<'EOF'
erro: o daemon do docker não respondeu.

    sudo systemctl enable --now docker  # sobe agora e nos próximos boots

EOF
        printf '%s\n\n' "$DOCKER_ERR" | sed 's/^/  /' >&2
    fi
    exit 1
fi

# O container precisa do histórico git para calcular a versão (git rev-list --count).
if [ -d .git ] && [ -n "$(git rev-parse --is-shallow-repository 2>/dev/null | grep true || true)" ]; then
    echo "==> clone raso detectado, buscando histórico completo (necessário para a versão)"
    git fetch --prune --unshallow || true
fi

echo "==> alvo: make $TARGET"
# NB: 'bash -c', nunca 'bash -lc'. Um shell de login recarrega o PATH a partir
# de /etc/profile e descarta o PATH que o Docker definiu — que é justamente
# onde vive o cross-compiler. O PATH é reforçado abaixo por segurança.
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$ROOT":/src -w /src \
    -e HOME=/tmp \
    -e MAKE_TARGET="$TARGET" \
    "$IMAGE" \
    bash -c '
        P="${PS2DEV:-/usr/local/ps2dev}"
        export PS2DEV="$P"
        export PS2SDK="${PS2SDK:-$P/ps2sdk}"
        export GSKIT="${GSKIT:-$P/gsKit}"
        export PATH="$PATH:$P/bin:$P/ee/bin:$P/iop/bin:$P/dvp/bin:$PS2SDK/bin"

        if ! command -v mips64r5900el-ps2-elf-gcc >/dev/null 2>&1; then
            echo "erro: cross-compiler nao encontrado dentro do container." >&2
            echo "  PS2DEV=$P" >&2
            echo "  PATH=$PATH" >&2
            echo "  conteudo de $P:" >&2
            ls -1 "$P" 2>&1 | sed "s/^/    /" >&2
            exit 127
        fi

        git config --global --add safe.directory /src 2>/dev/null || true
        exec make "$MAKE_TARGET"
    '

[ "$TARGET" = "clean" ] && { echo "==> limpo"; exit 0; }

# O Makefile gera OPNPS2LD.ELF (empacotado) ou opl.elf (com NOT_PACKED=1).
BUILT=""
for cand in OPNPS2LD.ELF opl.elf; do
    [ -f "$cand" ] && { BUILT="$cand"; break; }
done

if [ -z "$BUILT" ]; then
    echo "erro: build terminou mas nenhum ELF foi encontrado." >&2
    exit 1
fi

cp -f "$BUILT" "$OUT_NAME"
SIZE=$(du -h "$OUT_NAME" | cut -f1)
echo "==> $OUT_NAME  ($SIZE)"

if [ "$DEST" = "auto" ]; then
    # Procura mídia removível montada. Nem tudo que está montado serve: mídia de
    # instalação de sistema costuma ser somente-leitura, e pode haver mais de um
    # dispositivo. Filtra por gravabilidade e prefere quem já tem cara de PS2.
    # findmnt devolve pontos de montagem reais. Um 'find' pegaria subpastas
    # dentro deles (sources/, boot/...) e as trataria como dispositivos.
    if command -v findmnt >/dev/null 2>&1; then
        CANDIDATES=$(findmnt -rno TARGET 2>/dev/null | grep -E '^/(run/)?media/' | sort -u)
    else
        CANDIDATES=$(find /run/media /media -maxdepth 3 -mindepth 1 -type d 2>/dev/null | sort -u)
    fi
    WRITABLE=""; BEST=""
    while IFS= read -r d; do
        [ -z "$d" ] && continue
        touch "$d/.rh_write_test" 2>/dev/null || continue   # pula somente-leitura
        rm -f "$d/.rh_write_test"
        WRITABLE="${WRITABLE}${d}"$'\n'
        if [ -f "$d/OPNPS2LD.ELF" ] || [ -f "$d/$OUT_NAME" ] \
           || [ -d "$d/CD" ] || [ -d "$d/DVD" ] || [ -d "$d/ART" ]; then
            BEST="$d"
        fi
    done <<EOF
$CANDIDATES
EOF

    WCOUNT=$(printf '%s' "$WRITABLE" | grep -c . || true)
    if [ -n "$BEST" ]; then
        DEST="$BEST"
        echo "==> pendrive do PS2 encontrado: $DEST"
    elif [ "$WCOUNT" = "1" ]; then
        DEST=$(printf '%s' "$WRITABLE" | head -1)
        echo "==> único dispositivo gravável encontrado: $DEST"
        echo "    (sem pastas do OPL ainda — confira se é mesmo o pendrive do PS2)"
    else
        {
            echo
            if [ "$WCOUNT" = "0" ]; then
                echo "aviso: nenhum dispositivo gravável montado."
                echo "       Plugue o pendrive do PS2 e abra-o no gerenciador de arquivos."
                RO=$(printf '%s' "$CANDIDATES" | grep -c . || true)
                [ "$RO" != "0" ] && {
                    echo
                    echo "       Montados, porém somente-leitura (ignorados):"
                    printf '%s' "$CANDIDATES" | sed 's/^/         /'
                }
            else
                echo "aviso: mais de um dispositivo gravável, e nenhum com pastas do OPL."
                echo "       Escolha explicitamente com -o:"
                echo
                printf '%s' "$WRITABLE" | sed "s|^|         ./tools/build.sh -o |"
            fi
            echo
        } >&2
        DEST=""
    fi
fi

if [ -n "$DEST" ]; then
    if [ ! -d "$DEST" ]; then
        echo "erro: destino '$DEST' não existe ou não está montado." >&2
        echo "      dica: use -p para localizar o pendrive automaticamente." >&2
        exit 1
    fi
    cp -f "$OUT_NAME" "$DEST/$OUT_NAME"
    sync
    echo "==> copiado para $DEST/$OUT_NAME"
fi

# O que falta fazer depende de o ELF ja ter sido copiado ou nao. Repetir o
# passo que o script acabou de executar so confunde quem esta lendo.
if [ -n "$DEST" ]; then
    cat <<EOF

Próximo passo:
  1. desmonte antes de tirar:
       udisksctl unmount -b \$(findmnt -no SOURCE '$DEST')
  2. no PS2: FMCB → uLaunchELF → mass: → $OUT_NAME
  3. se a build travar, o OPNPS2LD.ELF do lado continua sendo o plano B
EOF
else
    cat <<EOF

Próximo passo:
  1. copie $OUT_NAME para a raiz do pendrive   (ou use: ./tools/build.sh -p)
  2. mantenha o OPNPS2LD.ELF original lá — é o seu plano B
  3. no PS2: FMCB → uLaunchELF → mass: → $OUT_NAME
EOF
fi
