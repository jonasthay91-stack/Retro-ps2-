#!/usr/bin/env bash
# RetroHub PS2 — build em um comando.
#
# Compila o OPL/RetroHub usando o container oficial do PS2 homebrew, sem
# precisar instalar PS2SDK na máquina. Funciona em Linux com Docker.
#
#   ./tools/build.sh                    compila (padrão)
#   ./tools/build.sh debug              compila com log por rede (UDPTTY)
#   ./tools/build.sh clean              limpa
#   ./tools/build.sh -o /run/media/eu/RETROHUB   compila e copia pro pendrive
#
# O binário sai como RETROHUB.ELF — nome diferente do OPNPS2LD.ELF de propósito,
# para que o OPL original continue no pendrive como plano B.

set -euo pipefail

IMAGE="ghcr.io/ps2homebrew/ps2homebrew:main"
OUT_NAME="RETROHUB.ELF"
TARGET="all"
DEST=""

usage() {
    sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -o|--output-dir) DEST="${2:-}"; shift 2 ;;
        -h|--help)       usage 0 ;;
        debug|clean|all|release|iopcore_debug|ingame_debug|eesio_debug)
                         TARGET="$1"; shift ;;
        *) echo "erro: argumento desconhecido '$1'" >&2; usage 1 ;;
    esac
done

cd "$(dirname "$0")/.."
ROOT="$PWD"

if [ ! -f Makefile ]; then
    cat >&2 <<'EOF'
erro: Makefile não encontrado.

Este script roda na raiz do fork do Open PS2 Loader. Se você ainda não clonou:

    git clone https://github.com/ps2homebrew/Open-PS2-Loader.git
    cd Open-PS2-Loader

e copie tools/build.sh para lá, ou aponte este repositório para o fork.
EOF
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "erro: docker não encontrado. Instale o docker e adicione seu usuário ao grupo 'docker'." >&2
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    cat >&2 <<'EOF'
erro: o daemon do docker não respondeu.

    sudo systemctl start docker
    sudo usermod -aG docker "$USER"    # depois faça logout/login

EOF
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

if [ -n "$DEST" ]; then
    if [ ! -d "$DEST" ]; then
        echo "erro: destino '$DEST' não existe ou não está montado." >&2
        exit 1
    fi
    cp -f "$OUT_NAME" "$DEST/$OUT_NAME"
    sync
    echo "==> copiado para $DEST/$OUT_NAME"
    echo "    desmonte o pendrive antes de tirar:  udisksctl unmount -b /dev/sdX1"
fi

cat <<EOF

Próximo passo:
  1. copie $OUT_NAME para a raiz do pendrive
  2. mantenha o OPNPS2LD.ELF original lá — é o seu plano B
  3. no PS2, lance $OUT_NAME pelo menu do FMCB ou pelo uLaunchELF
EOF
