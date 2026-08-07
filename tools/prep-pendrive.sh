#!/usr/bin/env bash
# RetroHub PS2 — prepara um pendrive novo para o PS2.
#
#   ./tools/prep-pendrive.sh                 lista os candidatos e para
#   ./tools/prep-pendrive.sh /dev/sdb        formata em exFAT (padrao)
#   ./tools/prep-pendrive.sh --fat32 /dev/sdb
#
# Serve para pendrive e para HD externo em gaveta USB - para o PS2 os dois sao
# a mesma coisa, um dispositivo de armazenamento em massa.
#
# APAGA TUDO no dispositivo escolhido. O script pede confirmacao digitada e se
# recusa a tocar em disco que nao seja USB ou que contenha o sistema.
#
# Tabela de particao MBR sempre: o PS2 nao reconhece GPT.
#
# Sobre o sistema de arquivos, os dois tem defeito e a escolha depende da
# biblioteca:
#
#   exFAT   PADRAO. Arquivo de qualquer tamanho. So o OPL le - o uLaunchELF e o
#           FMCB usam um driver de USB de vinte anos atras, FAT16/FAT32 apenas.
#           Consequencia: nada da boot direto pelo pendrive, e a volta do jogo
#           pelo IGR falha. Contorna-se lancando pela secao Applications do OPL,
#           que e o que o build.sh ja prepara.
#   FAT32   Todos leem, da boot direto, o IGR volta. Limite de 4 GB por arquivo -
#           e jogo de DVD9 passa disso com folga (God of War tem 7,9 GB).
#
# Regra pratica: se algum jogo seu passa de 4 GB, exFAT. Senao, FAT32.

set -euo pipefail

FS="exfat"
DEV=""

while [ $# -gt 0 ]; do
    case "$1" in
        --fat32) FS="fat32"; shift ;;
        --exfat) FS="exfat"; shift ;;
        -h|--help) sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "erro: argumento desconhecido '$1'" >&2; exit 1 ;;
        *) DEV="$1"; shift ;;
    esac
done

# ------------------------------------------------------------------ ajuda ---
if [ -z "$DEV" ]; then
    cat <<'EOF'
Uso:  ./tools/prep-pendrive.sh /dev/sdX

Dispositivos USB encontrados:
EOF
    # Gaveta de HD USB quase sempre reporta removable=0, porque o disco de dentro
    # nao e midia removivel. Filtrar por transporte USB pega pendrive e HD
    # externo, e continua deixando de fora os discos internos.
    found=0
    while IFS= read -r line; do
        set -- $line
        printf '  /dev/%-6s %-8s %s\n' "$1" "$2" "${3:-}"
        found=1
    done < <(lsblk -dno NAME,SIZE,TRAN,MODEL 2>/dev/null | awk '$3=="usb"{ $3=""; print }')
    [ "$found" = "0" ] && echo "  (nenhum — plugue o dispositivo USB)"
    echo
    echo "Confira o tamanho antes de escolher. Em caso de duvida, desconecte,"
    echo "rode de novo, conecte e rode outra vez: o que aparecer a mais e ele."
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

# Disco interno = quase certamente o do sistema. Recusar e o comportamento certo:
# um engano aqui destroi a maquina, e nao ha desfazer.
#
# O criterio e o transporte, nao a flag 'removable': gaveta de HD USB reporta
# removable=0 porque o disco de dentro nao e midia removivel, e o script recusaria
# um HD externo legitimo.
TRAN=$(lsblk -dno TRAN "$DEV" 2>/dev/null | tr -d ' ')
REMOVABLE=$(cat "/sys/block/$BASE/removable" 2>/dev/null || echo 0)
if [ "$TRAN" != "usb" ] && [ "$REMOVABLE" != "1" ]; then
    echo "erro: '$DEV' não está conectado por USB nem é removível." >&2
    echo "      Recusando por segurança — discos internos não passam por aqui." >&2
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

echo "==> criando tabela MBR e uma partição ($FS)"
# -s: modo script. Sem ele o parted abre um prompt Retry/Cancel que trava tudo.
# O PS2 exige MBR; GPT nao e reconhecido.
#
# O tipo declarado aqui e so um byte na tabela de particao. O que vale e o
# sistema de arquivos gravado depois; 'fat32' serve para os dois casos porque
# e o codigo que o PS2 procura.
parted -s "$DEV" mklabel msdos
parted -s "$DEV" mkpart primary fat32 1MiB 100%
parted -s "$DEV" set 1 lba on
partprobe "$DEV" 2>/dev/null || true
sleep 2

PART="${DEV}1"
[ -b "$PART" ] || PART="${DEV}p1"
[ -b "$PART" ] || { echo "erro: partição não apareceu." >&2; exit 1; }

echo "==> formatando $PART em $FS"
# Clusters grandes em ambos os casos. Cada cluster a mais e um fragmento em
# potencial, e o OPL nao abre jogo com mais de 64 fragmentos
# (BDM_MAX_FRAGS, cdvd_config.h:65). Num ISO de 8 GB isso importa muito.
if [ "$FS" = "exfat" ]; then
    if ! command -v mkfs.exfat >/dev/null 2>&1; then
        echo >&2
        echo "erro: mkfs.exfat não encontrado. Instale:" >&2
        echo "        sudo apt install exfatprogs" >&2
        echo "      ou formate em FAT32:  --fat32   (limite de 4 GB por arquivo)" >&2
        exit 1
    fi
    # -c 1M: clusters de 1 MB. Num disco de centenas de GB o desperdicio por
    # arquivo e irrelevante perto do ganho em continuidade.
    mkfs.exfat -c 1M -L PS2GAMES "$PART" >/dev/null
else
    # -s 64 => clusters de 32 KB, o maximo que o FAT32 aceita.
    mkfs.vfat -F 32 -s 64 -n PS2GAMES "$PART" >/dev/null
fi

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

cat <<EOF

Próximos passos:
  1. conecte de novo e copie os jogos, os MAIORES PRIMEIRO:
       ISOs de DVD  ->  DVD/
       ISOs de CD   ->  CD/
       capas        ->  ART/<CODIGO>_COV.png   ex.: SLUS_211.34_COV.png
  2. copie de uma vez só, sem apagar e regravar. Arquivo acima de 64
     fragmentos não abre, e formatar limpo é a única desfragmentação segura.
     Use ./tools/inspect-iso.py para conferir antes de ligar o console.
  3. no PS2: OPL -> Applications -> RetroHub
EOF

if [ "$FS" = "fat32" ]; then
    cat <<'EOF'

Atenção: em FAT32 nenhum arquivo passa de 4 GB. Jogo de DVD9 precisa ser
convertido para o formato UL/USBExtreme antes de copiar.
EOF
fi
