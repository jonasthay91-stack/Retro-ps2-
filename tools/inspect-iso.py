#!/usr/bin/env python3
"""RetroHub PS2 — diz o que tem dentro de um ISO e por que ele pode não abrir.

    ./tools/inspect-iso.py jogo.iso
    ./tools/inspect-iso.py /media/*/PS2GAMES/DVD/*.iso

Lê a estrutura ISO9660 de verdade: descritor primário, diretório raiz e o
SYSTEM.CNF de dentro do disco. É de lá que sai o código (SLUS_211.34) — o mesmo
que o OPL usa para casar capa e configuração, e o mesmo que o RetroHub Manager
vai usar para buscar metadados sozinho.

Também aponta os motivos comuns de um jogo não abrir: sistema errado, tamanho
acima do limite do FAT32, pasta trocada entre CD e DVD, e mídia fragmentada.
"""

import os
import re
import subprocess
import sys

SECTOR = 2048


def fragment_count(path):
    """Numero de extents, ou uma string explicando por que nao deu.

    Usa o filefrag do e2fsprogs. Em ext4 ele consulta por FIEMAP, que qualquer
    usuario pode usar. Em exFAT e FAT32 o driver nao oferece FIEMAP e o filefrag
    cai no FIBMAP antigo, que exige root — justamente nos sistemas de arquivos
    que nos interessam. Dai valer a pena dizer isso em vez de ficar calado.
    """
    try:
        out = subprocess.run(["filefrag", path], capture_output=True,
                             text=True, timeout=120)
    except FileNotFoundError:
        return "filefrag não instalado (pacote e2fsprogs)"
    except subprocess.SubprocessError:
        return "filefrag não respondeu"

    texto = out.stdout + out.stderr
    m = re.search(r"(\d+)\s+extents?\s+found", texto)
    if m:
        return int(m.group(1))
    if "root privileges" in texto or "FIBMAP" in texto:
        return ("precisa de root neste sistema de arquivos — rode:  "
                f"sudo filefrag '{path}'")
    return "não foi possível medir"


def read_at(f, lba, count=1):
    f.seek(lba * SECTOR)
    return f.read(SECTOR * count)


def parse_dir_record(rec):
    """Devolve (lba, tamanho, nome) de um registro de diretório ISO9660."""
    if not rec or rec[0] == 0:
        return None
    lba = int.from_bytes(rec[2:6], "little")
    size = int.from_bytes(rec[10:14], "little")
    name_len = rec[32]
    name = rec[33:33 + name_len].decode("latin-1", "replace")
    return lba, size, name


def find_in_root(f, root_lba, root_size, target):
    """Procura um arquivo no diretório raiz. Compara sem a versão ';1'."""
    data = read_at(f, root_lba, max(1, (root_size + SECTOR - 1) // SECTOR))
    pos = 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:
            # Zero significa fim dos registros neste setor; o próximo pode
            # começar no setor seguinte.
            pos = (pos // SECTOR + 1) * SECTOR
            if pos >= len(data):
                break
            continue
        rec = parse_dir_record(data[pos:pos + rec_len])
        if rec:
            name = rec[2].split(";")[0].upper()
            if name == target.upper():
                return rec
        pos += rec_len
    return None


def inspect(path):
    size = os.path.getsize(path)
    print(f"\n\033[1m{os.path.basename(path)}\033[0m")
    print(f"  tamanho        {size / (1024**3):.2f} GB  ({size:,} bytes)")

    problems = []
    notes = []

    with open(path, "rb") as f:
        pvd = read_at(f, 16)
        if len(pvd) < 2048 or pvd[1:6] != b"CD001":
            print("  \033[31mnão é um ISO9660.\033[0m")
            print("     Formatos como .bin/.cue, .mdf, .nrg ou arquivos ainda")
            print("     compactados (.zip .rar .7z) precisam ser convertidos antes.")
            return False

        volume = pvd[40:72].decode("latin-1").strip()
        print(f"  volume         {volume}")

        root = parse_dir_record(pvd[156:190])
        rec = find_in_root(f, root[0], root[1], "SYSTEM.CNF") if root else None

        if not rec:
            print("  \033[33msem SYSTEM.CNF na raiz.\033[0m")
            print("     Um disco de PS1 ou PS2 sempre tem esse arquivo. Sem ele,")
            print("     provavelmente é de outro sistema (PC, Dreamcast, Xbox...).")
            return False

        f.seek(rec[0] * SECTOR)
        cnf = f.read(min(rec[1], 4096)).decode("latin-1", "replace")

    # BOOT2 = PlayStation 2 · BOOT = PlayStation 1. É essa linha que separa os
    # dois, e é o teste que responde "preciso de emulador?".
    m2 = re.search(r"BOOT2\s*=\s*cdrom0?:\\?([^\s;]+)", cnf, re.I)
    m1 = re.search(r"^\s*BOOT\s*=\s*cdrom0?:\\?([^\s;]+)", cnf, re.I | re.M)
    ver = re.search(r"VER\s*=\s*([^\s]+)", cnf, re.I)

    if m2:
        code = m2.group(1).replace("\\", "").strip()
        print(f"  sistema        \033[32mPlayStation 2\033[0m")
        print(f"  código         \033[1m{code}\033[0m")
        # O OPL guarda o codigo em 11 caracteres mais o terminador
        # (GAME_STARTUP_MAX = 12) e TRUNCA sem avisar (supportbase.c:112-114).
        # Codigo comercial cabe justo — "SLUS_211.34" tem exatos 11. Disco de
        # coletanea, com nome de boot proprio, costuma passar disso, e a capa
        # so aparece se o arquivo usar o nome ja cortado.
        used = code[:11]
        if used != code:
            notes.append(f"o OPL corta o código em 11 caracteres: usa \033[1m{used}\033[0m")
        notes.append(f"capa deve se chamar  ART/{used}_COV.png")
    elif m1:
        code = m1.group(1).replace("\\", "").strip()
        print(f"  sistema        \033[33mPlayStation 1\033[0m")
        print(f"  código         {code}")
        problems.append("O OPL não roda jogo de PS1. Precisa do POPStarter, "
                        "e o jogo convertido para .VCD.")
    else:
        print("  sistema        desconhecido — SYSTEM.CNF sem linha BOOT")
        problems.append("SYSTEM.CNF existe mas não declara BOOT nem BOOT2. "
                        "Dump provavelmente incompleto.")

    if ver:
        print(f"  versão         {ver.group(1)}")

    # --- motivos comuns de não abrir ---------------------------------------
    if size > 4 * 1024**3 - 1:
        problems.append(f"{size / 1024**3:.2f} GB não cabe num arquivo FAT32 "
                        "(limite 4 GB). Converta para o formato UL/USBExtreme, "
                        "ou use exFAT — lembrando que aí só o OPL lê o pendrive.")

    # Disco de CD (azul) vive em CD/, de DVD em DVD/. Trocar as pastas e um
    # erro comum e o sintoma e exatamente "nao abre".
    folder = os.path.basename(os.path.dirname(os.path.abspath(path))).upper()
    is_cd = size <= 800 * 1024**2
    if folder in ("CD", "DVD"):
        want = "CD" if is_cd else "DVD"
        if folder != want:
            problems.append(f"está em {folder}/ mas o tamanho indica {want}/. "
                            "Jogo de CD (disco azul) vai em CD/, de DVD em DVD/.")
    else:
        notes.append(f"pasta atual: {folder}/ — deve estar em CD/ ou DVD/")

    if not path.lower().endswith(".iso"):
        problems.append("a extensão precisa ser .iso minúsculo ou maiúsculo; "
                        "o OPL não lista outros nomes.")

    # Fragmentacao. O OPL monta o ISO para ler o SYSTEM.CNF durante a varredura;
    # se a montagem falhar ele descarta o jogo da lista EM SILENCIO
    # (supportbase.c:334-339). Acima de 64 fragmentos o driver desiste
    # (BDM_MAX_FRAGS, cdvd_config.h:65) — e o sintoma e o jogo simplesmente nao
    # aparecer, sem nenhuma mensagem.
    frags = fragment_count(path)
    if isinstance(frags, str):
        notes.append(f"fragmentos não medidos: {frags}")
    elif frags > 64:
        problems.append(f"{frags} fragmentos — o OPL desiste acima de 64, e o "
                        "jogo nem aparece na lista. Não use desfragmentador: "
                        "copie tudo para o PC, formate o pendrive e copie de "
                        "volta, com os arquivos grandes primeiro.")
    else:
        print(f"  fragmentos     {frags}  (limite 64)")

    for n in notes:
        print(f"  \033[36m·\033[0m {n}")

    if problems:
        print()
        for p in problems:
            print(f"  \033[31m!\033[0m {p}")
        return False

    print("  \033[32m✓ nada de errado com o arquivo em si.\033[0m")
    print("    Se mesmo assim não abrir: pode ser fragmentação (acima de 64")
    print("    fragmentos o OPL desiste) ou o dump estar corrompido. Copiar")
    print("    para um pendrive recém-formatado resolve o primeiro caso.")
    return True


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1

    ok = True
    for path in args:
        if not os.path.isfile(path):
            print(f"\nnão encontrei: {path}")
            ok = False
            continue
        try:
            ok = inspect(path) and ok
        except Exception as e:
            print(f"  erro ao ler: {e}")
            ok = False
    print()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
