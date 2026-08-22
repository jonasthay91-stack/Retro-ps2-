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

// Quanto o fundo desliza para cada lado. Vinte pixels bastam: acima disso a
// borda ampliada da capa comeca a aparecer, e o efeito vira defeito.
#define RH_PARALLAX 20

// ----------------------------------------------------------------- estado ---
static image_cache_t *coverCache = NULL;

// A estante mostra UMA capa por vez, entao um par de slots basta. O cache do
// OPL aceita qualquer int* aqui — o tema usa um por item porque desenha varios
// ao mesmo tempo, o que nao e o nosso caso.
static int coverCacheId = -1;
static int coverUID = -1;
static char coverKey[64] = ""; // codigo do disco cuja capa esta no cache

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
static float growthVel = 0.0f;  // velocidade da mola de 'growth'
static float textFade = 0.0f;   // 0..1, entrada atrasada do texto
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
        int id = fntLoadFile(NULL, 15);
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

    // Quando o arquivo nao existe, cacheGetTexture grava -2 no identificador e
    // passa a devolver NULL de saida, sem tentar de novo (texcache.c:5-7). No
    // tema do OPL isso e guardado POR JOGO, entao significa "este jogo nao tem
    // capa". Aqui o identificador e um so, compartilhado — sem zerar na troca,
    // um unico jogo sem capa desligaria as capas de todos ate reiniciar.
    if (strcmp(coverKey, startup) != 0) {
        snprintf(coverKey, sizeof(coverKey), "%s", startup);
        coverCacheId = -1;
        coverUID = -1;
    }

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

// Encurta o texto ate caber, terminando em reticencias.
//
// Nomes de ISO sao longos e cheios de sufixo — "Black (BR) (DUB IA) (T1.0)..."
// — e sem isso o titulo simplesmente sai pela borda no meio de uma palavra.
//
// fntCalcDimensions devolve largura JA ESCALADA para a tela; por isso o limite
// tambem precisa passar por rmScaleX, senao a conta compara grandezas
// diferentes (e o mesmo cuidado que fntFitString toma).
static void rhFitText(int font, const char *src, int maxW, char *dst, int cap)
{
    int limit = rmScaleX(maxW);
    int len;

    snprintf(dst, cap, "%s", src ? src : "");

    if (fntCalcDimensions(font, dst) <= limit)
        return;

    len = (int)strlen(dst);
    while (len > 1) {
        len--;
        if (len + 4 > cap)
            continue;
        dst[len] = '.';
        dst[len + 1] = '.';
        dst[len + 2] = '.';
        dst[len + 3] = '\0';
        if (fntCalcDimensions(font, dst) <= limit)
            return;
        dst[len] = '\0'; // tira as reticencias antes de cortar mais
    }
}

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
// 'pan' vai de -1 a +1 e diz onde a fila esta: -1 no comeco, +1 no fim.
static void rhBackdrop(GSTEXTURE *cover, float fade, float pan)
{
    int t;

    // Orcamento de preenchimento, nao de primitivas.
    //
    // A primeira versao pintava a tela cheia, depois a capa por cima em tela
    // cheia, depois catorze faixas de 640 px com alfa — cinco telas de pintura
    // sobreposta por quadro, todas com mistura. O numero de primitivas era
    // baixinho, mas quem derruba a taxa de quadros no GS a 480 linhas
    // entrelacadas e a AREA pintada com alfa ligado, nao a contagem de chamadas.
    //
    // Agora: uma camada de base OU a capa (nunca as duas), e o escurecimento so
    // onde ele e necessario — atras do texto e atras da prateleira. O miolo da
    // direita nao precisa de veu nenhum.
    if (cover && cover->Mem) {
        // Paralaxe: o fundo anda no sentido contrario ao da fila, alguns pixels.
        // E a mesma primitiva com outra coordenada — custo ZERO — e mesmo um
        // deslocamento pequeno separa as duas camadas em profundidade. E o
        // truque de profundidade mais barato que existe neste hardware.
        //
        // A imagem e desenhada 40 px mais larga que a tela e comeca fora dela,
        // senao o deslocamento abriria uma faixa vazia na borda.
        int px = -RH_PARALLAX + (int)(-pan * RH_PARALLAX);

        // Multiplicacao de cor: abaixo de 0x80 escurece. Entra junto com o fade
        // para a troca de jogo nao piscar.
        t = 0x1E + (int)(0x14 * fade);
        rmDrawPixmap(cover, px, 0, ALIGN_NONE, 640 + 2 * RH_PARALLAX, 480,
                     SCALING_NONE, C(t, t, t + 6, A_SOLID));
    } else {
        rmDrawRect(0, 0, 640, 480, C_BASE);
    }

    // Faixa de cima, curta: so o suficiente para o rotulo e o contador nao
    // sumirem sobre uma capa clara.
    rhGradient(0, 64, 0x05, 0x07, 0x0C, 0x5A, 0x00, 4);

    // Faixa de baixo: separa a prateleira do fundo e garante contraste para as
    // lombadas. Seis passos bastam — acima disso a banda ja nao se distingue
    // numa TV entrelacada, e cada faixa custa 640 px de largura.
    rhGradient(SHELF_Y - 150, 150 + (480 - SHELF_Y), 0x05, 0x07, 0x0C, 0x18, 0x74, 6);
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
static void rhCover(GSTEXTURE *cover, float fade)
{
    int inset = (int)(6.0f * (1.0f - fade)); // entra crescendo, discreto
    // rmDrawRect nao aplica correcao de proporcao; rmDrawPixmap com
    // SCALING_RATIO aplica. Sem passar a moldura por rmWideScale, em 16:9 ela
    // ficaria mais larga que a capa que deveria emoldurar.
    int w = rmWideScale(COVER_W);

    rmDrawRect(COVER_X + 5, COVER_Y + 7, w, COVER_H, C(0, 0, 0, 0x38));
    rmDrawRect(COVER_X + 2, COVER_Y + 4, w, COVER_H, C(0, 0, 0, 0x30));

    if (cover && cover->Mem) {
        int a = 0x40 + (int)(0x40 * fade);
        rmDrawPixmap(cover, COVER_X + inset, COVER_Y + inset, ALIGN_NONE,
                     COVER_W - inset * 2, COVER_H - inset * 2, SCALING_RATIO,
                     C(0x80, 0x80, 0x80, a));
    } else {
        // Sem capa a moldura continua ali: o vazio comunica "e aqui que a capa
        // vai", em vez de deslocar o resto do layout.
        //
        // Antes isto repetia o titulo em corpo miudo. Com nomes de ISO longos
        // virava um paragrafo ilegivel dentro do retangulo — e o titulo ja esta
        // grande ao lado. Um aviso curto diz mais.
        rmDrawRect(COVER_X, COVER_Y, w, COVER_H, C(0x14, 0x19, 0x24, A_SOLID));
        rmDrawRect(COVER_X + 10, COVER_Y + 10, w - 20, COVER_H - 20,
                   C(0x1B, 0x21, 0x2E, A_SOLID));
        fntRenderString(fntSmall, COVER_X + w / 2, COVER_Y + COVER_H / 2 - 8,
                        ALIGN_CENTER, 0, 0, "SEM CAPA", C(0x4E, 0x58, 0x68, A_SOLID));
    }

    // Luz na borda de cima e na esquerda: sugere volume sem custar textura.
    rmDrawRect(COVER_X, COVER_Y, w, 1, C(0xFF, 0xFF, 0xFF, 0x2A));
    rmDrawRect(COVER_X, COVER_Y, 1, COVER_H, C(0xFF, 0xFF, 0xFF, 0x1C));
}

// Uma acao: ponto colorido do botao + palavra.
static int rhAction(int x, int y, u64 dot, const char *label, int alpha)
{
    rmDrawRect(x, y + 4, 8, 8, dot);
    fntRenderString(fntSmall, x + 14, y, ALIGN_NONE, 0, 0, label,
                    C(0x9A, 0xA6, 0xB8, alpha));
    return x + 14 + fntCalcDimensions(fntSmall, label) + 20;
}

static void rhInfo(const char *title, const char *startup, float fade)
{
    int avail = 640 - INFO_X - M_SAFE;
    int a = (int)(A_SOLID * fade);
    int y = COVER_Y + 6;
    char buf[96];
    int x;

    // Painel discreto atras da coluna de texto. Sem ele o texto flutua sobre a
    // capa desfocada e o contraste depende da imagem que estiver ali.
    rmDrawRect(INFO_X - 14, COVER_Y - 6, avail + 20, COVER_H + 12,
               C(0x08, 0x0B, 0x12, 0x44));

    if (title) {
        rhFitText(fntBig, title, avail, buf, sizeof(buf));
        fntRenderString(fntBig, INFO_X, y, ALIGN_NONE, 0, 0, buf,
                        C(0xFF, 0xFF, 0xFF, a));
        y += 40;
    }

    // O filete de acento cresce junto com o texto: e o unico elemento que
    // sugere direcao, entao vale ele desenhar-se em vez de aparecer pronto.
    rmDrawRect(INFO_X, y, (int)(34 * fade) + 2, 2, C_ACCENT);
    y += 14;

    if (startup) {
        fntRenderString(fntSmall, INFO_X, y, ALIGN_NONE, 0, 0, startup,
                        C(0x9A, 0xA6, 0xB8, a));
        y += 26;
    }

    // Acoes junto do rodape da capa, para o olho encontrar sempre no mesmo lugar
    // em vez de flutuar com o tamanho do titulo.
    y = COVER_Y + COVER_H - 22;
    x = rhAction(INFO_X, y, C_BTN_CROSS, "JOGAR", a);
    x = rhAction(x, y, C_BTN_CIRCLE, "VOLTAR", a);
    rhAction(x, y, C_BTN_TRIANGLE, "OPCOES", a);
}

// Uma lombada. Duas primitivas quando fora de foco — e por isso que a fila
// inteira cabe no orcamento de quadro mesmo com dezenas de jogos na tela.
static void rhSpine(int x, int w, int h, const char *key, int focused, float glow)
{
    int y = SHELF_Y - h;
    int r, g, b;

    rhSpineColor(key, &r, &g, &b);

    if (!focused) {
        // Escurecer o que nao esta em foco faz o foco aparecer sem precisar
        // clarear o selecionado ate perder a cor. Custa zero: e a mesma
        // primitiva, com outro valor.
        r = (r * 58) / 100;
        g = (g * 58) / 100;
        b = (b * 58) / 100;
    }

    if (focused) {
        // Clarear +70 sobre uma cor que ja podia chegar a 168 estourava para
        // quase branco: a lombada em foco virava uma barra chapada, sem cor
        // propria. +38 destaca sem apagar de que jogo ela e.
        int lift = (int)(38.0f * glow);
        r += lift; g += lift; b += lift;
        if (r > 235) r = 235;
        if (g > 235) g = 235;
        if (b > 235) b = 235;
    }

    rmDrawRect(x, y, w, h, C(r, g, b, A_SOLID));

    // Filete claro no topo: da espessura ao "papel" e separa a lombada do fundo.
    rmDrawRect(x, y, w, 2, C(0xFF, 0xFF, 0xFF, focused ? 0x3A : 0x2E));

    if (focused) {
        // Contorno so em ciano — o branco de antes competia com o corpo da
        // lombada e as duas coisas viravam uma mancha unica.
        int a = (int)(0x70 * glow);
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
    float target, span, avail, pan;

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
        growthVel = 0.0f;
        textFade = 1.0f;
        animReady = 1;
    } else {
        EASE(scrollPx, target, 0.22f);

        // Mola em vez de aproximacao simples: o item em foco passa um pouco do
        // tamanho final e volta. E o que separa "cresceu" de "foi escolhido" —
        // e custa tres multiplicacoes, nenhum pixel a mais.
        growthVel += (1.0f - growth) * 0.34f;
        growthVel *= 0.70f; // amortecimento; sem isso oscila para sempre
        growth += growthVel;
        if (growth < 0.0f)
            growth = 0.0f;
        if (growth > 1.35f)
            growth = 1.35f;

        // O texto entra depois que a lombada ja esta a caminho. Escalonar
        // tempos e o truque mais barato que existe para uma interface parecer
        // cuidada: nada acontece tudo de uma vez.
        EASE(textFade, (growth > 0.45f) ? 1.0f : 0.0f, 0.24f);
    }
    EASE(coverFade, 1.0f, 0.16f);

    // ---- camadas -----------------------------------------------------------
    // Onde a fila esta, de -1 (comeco) a +1 (fim). Com poucos jogos nao ha
    // percurso, entao o fundo fica parado no centro em vez de saltar.
    if (span > avail)
        pan = (scrollPx / (span - avail)) * 2.0f - 1.0f;
    else
        pan = 0.0f;

    rhBackdrop(cover, coverFade, pan);
    rhHeader(total, index);
    rhCover(cover, coverFade);
    rhInfo(title, startup, textFade);
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
            growthVel = 0.0f;
            sfxPlay(SFX_CURSOR);
        }
    } else if (getKey(KEY_RIGHT)) {
        if (menu->current && menu->current->next) {
            menu->current = menu->current->next;
            growth = 0.0f;
            growthVel = 0.0f;
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
