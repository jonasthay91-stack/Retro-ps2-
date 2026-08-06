/*
  RetroHub PS2 — tela "Estante"
  Licenciado sob a Academic Free License version 3.0, como o restante do OPL.

  Uma prateleira vista de frente: as lombadas dos jogos lado a lado, a capa do
  que estiver em foco em tamanho grande, e a mesma capa desfocada ao fundo.

  Tres restricoes moldaram tudo o que esta aqui:

  1. O renderman tem quatro primitivas. Nao ha sombra, nem gradiente, nem canto
     arredondado, nem rotacao. Tudo abaixo e feito com retangulos e uma imagem.
  2. O PS2 le USB 1.1. Uma lombada custa dois retangulos e zero byte de textura,
     entao a fila inteira e de graca; so o jogo em foco paga leitura de disco.
  3. O alvo real e uma TV pequena e entrelacada. Fonte pequena demais nao existe;
     contraste baixo nao existe. Dai o fundo escurecido atras de todo texto.

  Coordenadas: espaco virtual 640x480. Em 16:9 o renderman multiplica apenas
  LARGURAS por 3/4 quando SCALING_RATIO e pedido; posicoes nunca mudam.
*/

#include "include/opl.h"
#include "include/menusys.h"
#include "include/iosupport.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/texcache.h"
#include "include/themes.h"
#include "include/pad.h"
#include "include/gui.h"
#include "include/lang.h"
#include "include/sound.h"
#include "include/rh_shelf.h"

// ---------------------------------------------------------------- paleta ---
// Alfa 0x80 e opaco na convencao do GS; 0x00 e invisivel.
#define A_SOLID 0x80

#define C(r, g, b, a) GS_SETREG_RGBA((r), (g), (b), (a))

#define C_BASE     C(0x0A, 0x0C, 0x12, A_SOLID) // fundo quando nao ha capa
#define C_TEXT     C(0xFF, 0xFF, 0xFF, A_SOLID)
#define C_TEXT_DIM C(0x9A, 0xA6, 0xB8, A_SOLID)
#define C_ACCENT   C(0x4C, 0xC2, 0xFF, A_SOLID)

// Cores dos botoes do controle. Um ponto colorido comunica "quadrado/circulo"
// para quem cresceu com um PS2 na mao mais rapido que qualquer legenda — e a
// fonte nao tem os glifos ✕ ○ △ mesmo.
#define C_BTN_CROSS    C(0x6E, 0x9E, 0xE8, A_SOLID)
#define C_BTN_CIRCLE   C(0xE8, 0x6A, 0x6A, A_SOLID)
#define C_BTN_TRIANGLE C(0x6A, 0xD9, 0x9A, A_SOLID)

// --------------------------------------------------------------- medidas ---
// Margem de seguranca: TVs antigas e adaptadores HDMI baratos comem a borda.
#define M_SAFE 32

// A capa e desenhada 1:1 com o arquivo (192x276, ver docs/06). Reduzir abaixo
// de 0.5x faria o filtro bilinear do GS pular texels e serrilhar; em 1:1 nao ha
// reamostragem nenhuma.
#define COVER_X 44
#define COVER_Y 74
#define COVER_W 192
#define COVER_H 276

// Coluna de texto, a direita da capa.
#define INFO_X 268

// A prateleira.
#define SHELF_Y     452 // linha do movel
#define SPINE_H      92
#define SPINE_SEL_H 118
#define SPINE_W      14
#define SPINE_SEL_W  22
#define SPINE_GAP     4
#define SPINE_PITCH (SPINE_W + SPINE_GAP)

// ----------------------------------------------------------------- estado ---
static image_cache_t *coverCache = NULL;

// A estante mostra UMA capa por vez, entao um par de slots basta. O cache do
// OPL aceita qualquer int* aqui — o tema usa um por item porque desenha varios
// ao mesmo tempo, o que nao e o nosso caso.
static int coverCacheId = -1;
static int coverUID = -1;

// Fontes proprias. O padrao do OPL tem 17 px e so ele deixaria tudo no mesmo
// peso visual. fntLoadFile(NULL, n) carrega a MESMA fonte embutida noutro
// tamanho, sem depender de arquivo no cartao nem no pendrive.
static int fntBig = FNT_DEFAULT;
static int fntSmall = FNT_DEFAULT;

// Animacao. Suavizacao exponencial: a cada quadro anda uma fracao do que falta.
// Nao depende de medir tempo — e o mesmo gesto em 60 Hz (NTSC) e 50 Hz (PAL),
// so um pouco mais lento no segundo, o que ninguem percebe.
static float scrollPx = 0.0f;   // posicao atual da fila
static float growth = 0.0f;     // 0..1, o "puxar o livro da estante"
static float coverFade = 0.0f;  // 0..1, entrada da capa nova
static void *lastCoverMem = NULL;
static int animReady = 0;       // primeiro quadro assenta sem animar

#define EASE(cur, target, rate) ((cur) += ((target) - (cur)) * (rate))

void rhShelfInit(void)
{
    // Sufixo "COV", sem underscore: quem monta o caminho ja poe o separador —
    // bdmsupport.c:584 faz "%s%s/%s_%s" e texDiscoverLoad acrescenta ".png",
    // resultando em ART/<startup>_COV.png. Passar "_COV" aqui geraria
    // "__COV" e a capa nunca seria encontrada.
    if (!coverCache)
        coverCache = cacheInitCache(-1, "ART", 1, "COV", 1);

    // Se algum slot nao estiver livre, fntLoadFile devolve FNT_ERROR e ficamos
    // com a fonte padrao. Feio, mas funcional — nunca sem texto.
    if (fntBig == FNT_DEFAULT) {
        int id = fntLoadFile(NULL, 26);
        if (id != FNT_ERROR)
            fntBig = id;
    }
    if (fntSmall == FNT_DEFAULT) {
        int id = fntLoadFile(NULL, 13);
        if (id != FNT_ERROR)
            fntSmall = id;
    }

    scrollPx = 0.0f;
    growth = 1.0f;
    coverFade = 0.0f;
    lastCoverMem = NULL;
    animReady = 0;
}

void rhShelfEnd(void)
{
    if (coverCache) {
        cacheDestroyCache(coverCache);
        coverCache = NULL;
    }
    if (fntBig != FNT_DEFAULT) {
        fntRelease(fntBig);
        fntBig = FNT_DEFAULT;
    }
    if (fntSmall != FNT_DEFAULT) {
        fntRelease(fntSmall);
        fntSmall = FNT_DEFAULT;
    }
    coverCacheId = -1;
    coverUID = -1;
}

// Marca de identificacao. __DATE__/__TIME__ sao preenchidos pelo compilador, e
// e justamente por mudarem a cada build que servem: se o valor na tela nao
// mudou depois de recompilar, o console abriu outro arquivo.
//
// A hora vem do container do build, que pode estar noutro fuso — o que importa
// nao e bater com o relogio, e sim mudar.
void rhShelfWatermark(void)
{
    static const char tag[] = "RetroHub " __TIME__;

    rmDrawRect(0, 462, 132, 18, C(0x00, 0x00, 0x00, 0x5C));
    rmDrawRect(0, 462, 3, 18, C_ACCENT);
    fntRenderString(FNT_DEFAULT, 9, 463, ALIGN_NONE, 0, 0, tag, C_ACCENT);
}

// ------------------------------------------------------------------ dados ---
static submenu_list_t *rhGetList(menu_item_t **outMenu)
{
    menu_item_t *cur = menuGetSelectedItem();

    if (outMenu)
        *outMenu = cur;

    if (!cur || !cur->userdata)
        return NULL;

    return cur->submenu;
}

static char *rhStartupOf(item_list_t *list, submenu_list_t *node)
{
    if (!list || !list->itemGetStartup || !node)
        return NULL;
    return list->itemGetStartup(list, node->item.id);
}

static GSTEXTURE *rhGetCover(item_list_t *list, submenu_list_t *sel)
{
    char *startup;

    if (!gEnableArt || !coverCache || !list || !sel)
        return NULL;

    startup = rhStartupOf(list, sel);
    if (!startup)
        return NULL;

    // Assincrono: devolve NULL enquanto o arquivo nao chegou do pendrive. Quem
    // desenha trata isso como "ainda nao", nunca como erro.
    return cacheGetTexture(coverCache, list, &coverCacheId, &coverUID, startup);
}

// Cor estavel por jogo, derivada do codigo do disco. Dois jogos quase nunca
// caem na mesma cor, e o mesmo jogo tem sempre a sua — a estante fica
// reconhecivel de longe sem guardar um byte em disco.
static void rhSpineColor(const char *key, int *r, int *g, int *b)
{
    u32 h = 2166136261u; // FNV-1a

    if (key) {
        while (*key) {
            h ^= (u8)(*key++);
            h *= 16777619u;
        }
    }

    // 52..168: escuro o bastante para o branco por cima, claro o bastante para
    // uma lombada se distinguir da vizinha.
    *r = 52 + (int)((h) % 117u);
    *g = 52 + (int)((h >> 9) % 117u);
    *b = 52 + (int)((h >> 18) % 117u);
}

// ----------------------------------------------------------------- desenho --

// Faixa escura em degrade. O GS nao tem gradiente, entao sao N retangulos
// empilhados com o alfa variando. Acima de ~10 faixas a banda some numa TV
// entrelacada, e cada uma custa uma primitiva — 14 e o ponto de equilibrio.
static void rhGradient(int y, int h, int r, int g, int b, int a0, int a1, int steps)
{
    int i;

    for (i = 0; i < steps; i++) {
        int y0 = y + (h * i) / steps;
        int y1 = y + (h * (i + 1)) / steps;
        int a = a0 + ((a1 - a0) * i) / (steps - 1);

        if (y1 > y0)
            rmDrawRect(0, y0, 640, y1 - y0, C(r, g, b, a));
    }
}

// A assinatura visual do RetroHub: a capa do jogo em foco, ampliada para tela
// cheia e escurecida, virando o fundo. Custa UMA primitiva e zero byte — a
// textura ja esta carregada para o painel. O borrao e efeito colateral do
// filtro bilinear do GS ampliando 192 px para 640, nao um filtro que pagamos.
static void rhBackdrop(GSTEXTURE *cover, float fade)
{
    int t;

    rmDrawRect(0, 0, 640, 480, C_BASE);

    if (cover && cover->Mem) {
        // Multiplicacao de cor: abaixo de 0x80 escurece. Entra junto com o fade
        // para a troca de jogo nao piscar.
        t = 0x1E + (int)(0x14 * fade);
        rmDrawPixmap(cover, 0, 0, ALIGN_NONE, 640, 480, SCALING_NONE,
                     C(t, t, t + 6, A_SOLID));
    }

    // Escurecimento por cima, mais forte embaixo: garante contraste para o
    // texto e para a prateleira, independentemente da capa que estiver ali.
    rhGradient(0, 300, 0x08, 0x0A, 0x10, 0x30, 0x58, 8);
    rhGradient(300, 180, 0x06, 0x07, 0x0C, 0x58, 0x78, 6);
}

// Rotulo pequeno com um filete de acento embaixo. Marca o topo da tela sem
// competir com o titulo do jogo.
static void rhHeader(int total, int index)
{
    char buf[32]; // dois inteiros com sinal e o separador cabem com folga

    fntRenderString(fntSmall, M_SAFE, 30, ALIGN_NONE, 0, 0, "BIBLIOTECA", C_ACCENT);
    rmDrawRect(M_SAFE, 48, 74, 2, C_ACCENT);

    if (total > 0) {
        snprintf(buf, sizeof(buf), "%d / %d", index + 1, total);
        fntRenderString(fntSmall, 640 - M_SAFE, 30, ALIGN_RIGHT, 0, 0, buf, C_TEXT_DIM);
    }
}

// A capa em foco, com sombra e moldura. A sombra sao dois retangulos deslocados
// com alfa baixo — nao ha desfoque, mas a 480 linhas entrelacadas o olho aceita
// como sombra, e custa duas primitivas em vez de uma textura.
static void rhCover(GSTEXTURE *cover, const char *title, float fade)
{
    int inset = (int)(6.0f * (1.0f - fade)); // entra crescendo, discreto

    rmDrawRect(COVER_X + 5, COVER_Y + 7, COVER_W, COVER_H, C(0, 0, 0, 0x38));
    rmDrawRect(COVER_X + 2, COVER_Y + 4, COVER_W, COVER_H, C(0, 0, 0, 0x30));

    if (cover && cover->Mem) {
        int a = 0x40 + (int)(0x40 * fade);
        rmDrawPixmap(cover, COVER_X + inset, COVER_Y + inset, ALIGN_NONE,
                     COVER_W - inset * 2, COVER_H - inset * 2, SCALING_RATIO,
                     C(0x80, 0x80, 0x80, a));
    } else {
        // Sem capa a moldura continua ali: o vazio comunica "e aqui que a capa
        // vai", em vez de deslocar o resto do layout.
        rmDrawRect(COVER_X, COVER_Y, COVER_W, COVER_H, C(0x16, 0x1B, 0x26, A_SOLID));
        if (title)
            fntRenderString(fntSmall, COVER_X + COVER_W / 2, COVER_Y + COVER_H / 2,
                            ALIGN_CENTER, COVER_W - 24, 0, title, C(0x55, 0x5E, 0x6E, A_SOLID));
    }

    // Luz na borda de cima e na esquerda: sugere volume sem custar textura.
    rmDrawRect(COVER_X, COVER_Y, COVER_W, 1, C(0xFF, 0xFF, 0xFF, 0x2A));
    rmDrawRect(COVER_X, COVER_Y, 1, COVER_H, C(0xFF, 0xFF, 0xFF, 0x1C));
}

// Uma acao: ponto colorido do botao + palavra.
static int rhAction(int x, int y, u64 dot, const char *label)
{
    rmDrawRect(x, y + 4, 8, 8, dot);
    fntRenderString(fntSmall, x + 14, y, ALIGN_NONE, 0, 0, label, C_TEXT_DIM);
    return x + 14 + fntCalcDimensions(fntSmall, label) + 20;
}

static void rhInfo(const char *title, const char *startup)
{
    int y = COVER_Y + 6;
    int x;

    if (title) {
        fntRenderString(fntBig, INFO_X, y, ALIGN_NONE, 640 - INFO_X - M_SAFE, 0,
                        title, C_TEXT);
        y += 40;
    }

    rmDrawRect(INFO_X, y, 34, 2, C_ACCENT);
    y += 14;

    if (startup) {
        fntRenderString(fntSmall, INFO_X, y, ALIGN_NONE, 0, 0, startup, C_TEXT_DIM);
        y += 26;
    }

    // Acoes junto do rodape da capa, para o olho encontrar sempre no mesmo lugar
    // em vez de flutuar com o tamanho do titulo.
    y = COVER_Y + COVER_H - 22;
    x = rhAction(INFO_X, y, C_BTN_CROSS, "JOGAR");
    x = rhAction(x, y, C_BTN_CIRCLE, "VOLTAR");
    rhAction(x, y, C_BTN_TRIANGLE, "OPCOES");
}

// Uma lombada. Duas primitivas quando fora de foco — e por isso que a fila
// inteira cabe no orcamento de quadro mesmo com dezenas de jogos na tela.
static void rhSpine(int x, int w, int h, const char *key, int focused, float glow)
{
    int y = SHELF_Y - h;
    int r, g, b;

    rhSpineColor(key, &r, &g, &b);

    if (focused) {
        int lift = (int)(70.0f * glow);
        r += lift; g += lift; b += lift;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
    }

    rmDrawRect(x, y, w, h, C(r, g, b, A_SOLID));

    // Filete claro no topo: da espessura ao "papel" e separa a lombada do fundo.
    rmDrawRect(x, y, w, 2, C(0xFF, 0xFF, 0xFF, focused ? 0x50 : 0x2E));

    if (focused) {
        int a = (int)(0x80 * glow);
        rmDrawRect(x - 2, y - 2, w + 4, 2, C(0x4C, 0xC2, 0xFF, a));
        rmDrawRect(x - 2, SHELF_Y, w + 4, 2, C(0x4C, 0xC2, 0xFF, a));
        rmDrawRect(x - 2, y - 2, 2, h + 4, C(0x4C, 0xC2, 0xFF, a));
        rmDrawRect(x + w, y - 2, 2, h + 4, C(0x4C, 0xC2, 0xFF, a));
    }
}

// O movel. Uma linha clara, uma sombra grossa embaixo — o suficiente para as
// lombadas parecerem apoiadas em algo em vez de flutuarem.
static void rhRail(void)
{
    rmDrawRect(0, SHELF_Y, 640, 2, C(0x7A, 0x88, 0xA0, 0x70));
    rmDrawRect(0, SHELF_Y + 2, 640, 12, C(0x0A, 0x0D, 0x14, 0x66));
}

void rhShelfRender(void)
{
    menu_item_t *menu = NULL;
    submenu_list_t *head = rhGetList(&menu);
    submenu_list_t *sel = menu ? menu->current : NULL;
    item_list_t *list = menu ? (item_list_t *)menu->userdata : NULL;
    submenu_list_t *node;
    GSTEXTURE *cover;
    char *title, *startup;
    int total = 0, index = 0, i, first, x;
    float target, span, avail;

    if (!head || !sel) {
        rmDrawRect(0, 0, 640, 480, C_BASE);
        // Literal de proposito: os ids de idioma vem de lang_autogen.h, gerado
        // no build a partir dos .lng. Criar um id para um aviso que quase nunca
        // aparece acoplaria esta tela ao sistema de traducao sem ganho.
        fntRenderString(fntBig, 320, 228, ALIGN_CENTER, 0, 0,
                        "Prateleira vazia", C_TEXT_DIM);
        fntRenderString(fntSmall, 320, 262, ALIGN_CENTER, 0, 0,
                        "Nenhum jogo neste dispositivo", C(0x60, 0x6A, 0x7A, A_SOLID));
        return;
    }

    for (node = head; node; node = node->next) {
        if (node == sel)
            index = total;
        total++;
    }

    title = submenuItemGetText(&sel->item);
    startup = rhStartupOf(list, sel);
    cover = rhGetCover(list, sel);

    // Capa nova? Reinicia o fade. Comparar o ponteiro de memoria basta: o cache
    // troca o buffer quando carrega outra imagem.
    if (cover && cover->Mem != lastCoverMem) {
        lastCoverMem = cover->Mem;
        coverFade = 0.0f;
    } else if (!cover) {
        lastCoverMem = NULL;
    }

    // ---- animacao ----------------------------------------------------------
    avail = (float)(640 - 2 * M_SAFE);
    span = (float)total * SPINE_PITCH;
    target = (float)index * SPINE_PITCH - (avail - SPINE_SEL_W) * 0.5f;
    if (span < avail)
        target = -(avail - span) * 0.5f; // poucos jogos: centraliza a fila
    else if (target > span - avail)
        target = span - avail;
    if (target < 0.0f)
        target = 0.0f;

    if (!animReady) {
        // Entrar na tela nao deve custar uma animacao de deslize vinda do zero.
        scrollPx = target;
        growth = 1.0f;
        animReady = 1;
    } else {
        EASE(scrollPx, target, 0.22f);
        EASE(growth, 1.0f, 0.20f);
    }
    EASE(coverFade, 1.0f, 0.16f);

    // ---- camadas -----------------------------------------------------------
    rhBackdrop(cover, coverFade);
    rhHeader(total, index);
    rhCover(cover, title, coverFade);
    rhInfo(title, startup);
    rhRail();

    // ---- a fila ------------------------------------------------------------
    // Comeca uma lombada antes da primeira visivel, para quem entra pela borda
    // aparecer deslizando em vez de surgir do nada.
    first = (int)(scrollPx / SPINE_PITCH) - 1;
    if (first < 0)
        first = 0;

    node = head;
    for (i = 0; i < first && node->next; i++)
        node = node->next;

    for (i = first; node && i < total; i++, node = node->next) {
        int focused = (node == sel);
        int w = focused ? SPINE_SEL_W : SPINE_W;
        int h = focused ? (SPINE_H + (int)((SPINE_SEL_H - SPINE_H) * growth)) : SPINE_H;
        char *k = rhStartupOf(list, node);

        x = M_SAFE + (int)((float)i * SPINE_PITCH - scrollPx);
        if (x > 640)
            break;
        if (x + w < 0)
            continue;

        if (!k)
            k = submenuItemGetText(&node->item);

        rhSpine(x, w, h, k, focused, focused ? growth : 0.0f);
    }
}

void rhShelfHandleInput(void)
{
    menu_item_t *menu = NULL;

    rhGetList(&menu);
    if (!menu)
        return;

    if (getKey(KEY_LEFT)) {
        if (menu->current && menu->current->prev) {
            menu->current = menu->current->prev;
            growth = 0.0f;
            sfxPlay(SFX_CURSOR);
        }
    } else if (getKey(KEY_RIGHT)) {
        if (menu->current && menu->current->next) {
            menu->current = menu->current->next;
            growth = 0.0f;
            sfxPlay(SFX_CURSOR);
        }
    } else if (getKeyOn(KEY_CROSS)) {
        if (menu->execCross)
            menu->execCross(menu);
    } else if (getKeyOn(KEY_TRIANGLE)) {
        if (menu->execTriangle)
            menu->execTriangle(menu);
    } else if (getKeyOn(KEY_CIRCLE)) {
        // Volta para a lista do OPL. A saida sempre existe, mesmo que o resto
        // desta tela esteja errado.
        //
        // A lista pagina a partir de 'pagestart'. Como a estante andou pelo
        // 'current' sem paginar, os dois podem ter se afastado — ancorar aqui
        // evita a lista voltar mostrando uma pagina onde o cursor nao esta.
        if (menu->current)
            menu->pagestart = menu->current;
        animReady = 0;
        guiSwitchScreen(GUI_SCREEN_MAIN);
    } else if (getKeyOn(KEY_SELECT)) {
        if (menu->refresh)
            menu->refresh(menu);
    }
}
