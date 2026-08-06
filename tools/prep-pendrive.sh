#!/usr/bin/env bash
# RetroHub PS2 — prepara um pendrive novo para o PS2.
#
#   ./tools/prep-pendrive.sh                lista os candidatos e para
#   ./tools/prep-pendrive.sh /dev/sdb       formata e monta a estrutura
#
# APAGA TUDO no dispositivo escolhido. O script pede confirmacao digitada e se
# recusa a tocar em disco nao removivel ou que contenha o sistema.
#
# Formata em FAT32 com tabela MBR. Nao e escolha estetica:
#
#   exFAT  o OPL le, mas o uLaunchELF e o FMCB nao - eles usam um driver de USB
#          de vinte anos atras, FAT16/FAT32 apenas. Consequencia: o pendrive
#          guarda jogos mas nada consegue dar boot nele, e a volta do jogo pelo
#          IGR tambem falha, porque o ee_core carrega esses mesmos drivers.
#   FAT32  todos leem. O preco e o limite de 4 GB por arquivo, que atinge poucos
#          jogos e tem contorno (formato UL/USBExtreme).

set -euo pipefail

DEV="${1:-}"

# ------------------------------------------------------------------ ajuda ---
if [ -z "$DEV" ]; then
    cat <<'EOF'
Uso:  ./tools/prep-pendrive.sh /dev/sdX

Dispositivos removiveis encontrados:
EOF
    found=0
    for d in /sys/block/*; do
        name=$(basename "$d")
        [ -f "$d/removable" ] || continue
        [ "$(cat "$d/removable")" = "1" ] || continue
        size=$(lsblk -dno SIZE "/dev/$name" 2>/dev/null || echo "?")
        model=$(lsblk -dno MODEL "/dev/$name" 2>/dev/null || echo "")
        printf '  /dev/%-6s %-8s %s\n' "$name" "$size" "$model"
        found=1
    done
    [ "$found" = "0" ] && echo "  (nenhum — plugue o pendrive)"
    echo
    echo "Confira o tamanho antes de escolher. Em caso de duvida, tire o pendrive,"
    echo "rode de novo, plugue e rode outra vez: o que aparecer a mais e ele."
    exit 0
fi

# ------------------------------------------------------------ verificacoes ---
[ "$(id -u)" = "0" ] || { echo "erro: precisa de sudo — é formatação de disco." >&2; exit 1; }
[ -b "$DEV" ] || { echo "erro: '$DEV' não é um dispositivo de bloco." >&2; exit 1; }

BASE=$(basename "$DEV")
case "$BASE" in
    *[0-9]) # /dev/sdb1 e uma particao, nao o disco
        if [ -f "/sys/class/block/$BASE/partition" ]; then
            echo "erro: '$DEV' é uma partição. Passe o disco inteiro, ex.: /dev/${BASE%%[0-9]*}" >&2
            exit 1
        fi
        ;;
esac

# Nao removivel = provavelmente o disco do sistema. Recusar e o comportamento
# certo: um engano aqui destroi a maquina, e nao ha desfazer.
REMOVABLE=$(cat "/sys/block/$BASE/removable" 2>/dev/null || echo 0)
if [ "$REMOVABLE" != "1" ]; then
    echo "erro: '$DEV' não é removível. Recusando por segurança." >&2
    echo "      Pendrives aparecem como removíveis; discos internos, não." >&2
    exit 1
fi

# Cinto e suspensorio: se qualquer particao dele estiver montada em / ou /home,
# aborta mesmo que o kernel diga que e removivel.
while IFS= read -r mp; do
    case "$mp" in
        /|/home|/boot|/usr|/var)
            echo "erro: '$DEV' tem partição montada em '$mp'. Abortando." >&2
            exit 1
            ;;
    esac
done < <(lsblk -nro MOUNTPOINT "$DEV" 2>/dev/null | grep -v '^$' || true)

SIZE=$(lsblk -dno SIZE "$DEV")
MODEL=$(lsblk -dno MODEL "$DEV" 2>/dev/null || echo "")

echo
echo "  Dispositivo: $DEV"
echo "  Tamanho:     $SIZE"
echo "  Modelo:      ${MODEL:-desconhecido}"
echo
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DEV" 2>/dev/null || true
echo
echo "  TUDO ACIMA SERÁ APAGADO."
echo
printf "  Para confirmar, digite o caminho do dispositivo (%s): " "$DEV"
read -r CONFIRM
[ "$CONFIRM" = "$DEV" ] || { echo "  cancelado."; exit 1; }

# ------------------------------------------------------------- formatacao ---
echo
echo "==> desmontando o que estiver montado"
while IFS= read -r part; do
    [ -n "$part" ] && umount "$part" 2>/dev/null || true
done < <(lsblk -nro PATH "$DEV" | tail -n +2)

echo "==> limpando assinaturas antigas"
# Sem isso, restos de tabela GPT confundem o PS2 e algumas ferramentas.
wipefs -a "$DEV" >/dev/null
sgdisk --zap-all "$DEV" >/dev/null 2>&1 || dd if=/dev/zero of="$DEV" bs=1M count=4 status=none

echo "==> criando tabela MBR e uma partição FAT32"
# -s: modo script. Sem ele o parted abre um prompt Retry/Cancel que trava tudo.
# O PS2 exige MBR; GPT nao e reconhecido.
parted -s "$DEV" mklabel msdos
parted -s "$DEV" mkpart primary fat32 1MiB 100%
parted -s "$DEV" set 1 lba on
partprobe "$DEV" 2>/dev/null || true
sleep 2

PART="${DEV}1"
[ -b "$PART" ] || PART="${DEV}p1"
[ -b "$PART" ] || { echo "erro: partição não apareceu." >&2; exit 1; }

echo "==> formatando $PART em FAT32"
# -s 64 => clusters de 32 KB. Clusters grandes reduzem o numero de fragmentos
# por arquivo, e o OPL nao abre jogo com mais de 64 fragmentos
# (BDM_MAX_FRAGS, cdvd_config.h:65).
mkfs.vfat -F 32 -s 64 -n PS2GAMES "$PART" >/dev/null

echo "==> montando"
MP=$(mktemp -d)
mount "$PART" "$MP"

echo "==> criando a estrutura de pastas do OPL"
for d in APPS ART CD CFG CHT DVD LNG RH THM VMC; do
    mkdir -p "$MP/$d"
done

# Copia a build, se existir por perto.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF=""
for cand in "$ROOT/RETROHUB.ELF" "$ROOT/../Open-PS2-Loader/RETROHUB.ELF" \
            "$HOME/Open-PS2-Loader/RETROHUB.ELF"; do
    [ -f "$cand" ] && { ELF="$cand"; break; }
done

if [ -n "$ELF" ]; then
    cp -f "$ELF" "$MP/RETROHUB.ELF"
    mkdir -p "$MP/APPS/RetroHub"
    cp -f "$ELF" "$MP/APPS/RetroHub/RETROHUB.ELF"
    printf 'title=RetroHub\nboot=RETROHUB.ELF\n' > "$MP/APPS/RetroHub/title.cfg"
    echo "==> RETROHUB.ELF instalado (raiz e APPS/RetroHub)"
else
    echo "==> RETROHUB.ELF não encontrado; rode ./tools/build.sh -p depois"
fi

sync
OWNER=${SUDO_USER:-}
[ -n "$OWNER" ] && chown -R "$OWNER" "$MP" 2>/dev/null || true

echo
echo "==> pronto. Estrutura criada:"
find "$MP" -maxdepth 2 | sed "s|$MP|<pendrive>|" | sort
umount "$MP"; rmdir "$MP"

cat <<'EOF'

Próximos passos:
  1. plugue o pendrive de novo e copie os jogos:
       ISOs de DVD  ->  DVD/
       ISOs de CD   ->  CD/
       capas        ->  ART/<CODIGO>_COV.png   ex.: SLUS_211.34_COV.png
  2. copie de uma vez só, sem apagar e regravar — arquivo acima de 64
     fragmentos não abre, e formatar limpo é a única desfragmentação segura
  3. no PS2: OPL -> Applications -> RetroHub

Jogo acima de 4 GB não cabe num arquivo FAT32: converta para o formato
UL/USBExtreme antes de copiar. A maioria dos títulos fica bem abaixo disso.
EOF
