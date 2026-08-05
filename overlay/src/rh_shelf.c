/*
  RetroHub PS2 — tela "Estante"
  Licenciado sob a Academic Free License version 3.0, como o restante do OPL.

  Uma prateleira vista de frente: as lombadas dos jogos lado a lado, e a capa
  do que estiver em foco em tamanho grande. E a leitura que a arquitetura do
  PS2 favorece — a lombada custa quatro retangulos e nenhum byte de textura,
  entao a fila inteira e praticamente de graca, e so a capa do selecionado
  paga I/O.

  Esta tela convive com a lista do OPL, nao a substitui. As duas compartilham
  o mesmo 'current' do menu, entao alternar entre elas nao perde o lugar, e um
  defeito aqui nunca deixa o usuario sem como abrir um jogo.

  Coordenadas: espaco virtual 640x480. Em 16:9 o renderman multiplica apenas
  LARGURAS por 3/4 quando SCALING_RATIO e pedido; posicoes nunca mudam. Por
  isso os X sao calculados no espaco de 640 e so as larguras de imagem pedem
  escala — o mesmo motivo pelo qual a prateleira cabe mais lombadas em 16:9.
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

#define RH_RGBA(r, g, b, a) GS_SETREG_RGBAQ((r), (g), (b), (a), 0x00)

#define RH_OPAQUE 0x80

// Margem de seguranca. TVs antigas e adaptadores HDMI baratos cortam a borda;
// 32 px cobrem o pior caso observado sem desperdicar tela util.
#define RH_MARGIN 32

// A capa nasce 192x276 (ver docs/06). Aqui ela aparece inteira, sem reducao,
// porque abaixo de 0.5x o filtro bilinear do GS comeca a pular texels.
#define RH_COVER_X 48
#define RH_COVER_Y 108
#define RH_COVER_W 192
#define RH_COVER_H 276

// A prateleira. Lombada fina, selecionada mais larga e mais alta — o gesto de
// puxar um livro da estante.
#define RH_SHELF_BASE_Y 430
#define RH_SPINE_H      92
#define RH_SPINE_SEL_H  116
#define RH_SPINE_W      14
#define RH_SPINE_SEL_W  22
#define RH_SPINE_GAP    4

static image_cache_t *coverCache = NULL;

// A estante mostra UMA capa por vez, entao um par de slots basta. O cache do
// OPL aceita qualquer int* aqui — o tema usa um por item porque desenha varios
// ao mesmo tempo, o que nao e o nosso caso.
static int coverCacheId = -1;
static int coverUID = -1;

void rhShelfInit(void)
{
    // Cache de uma entrada sobre ART/<startup>_COV.png. Prefixo relativo faz o
    // OPL montar o caminho do dispositivo ativo (mass:, hdd:, smb:...).
    if (!coverCache)
        coverCache = cacheInitCache(-1, "ART", 1, "_COV", 1);
}

void rhShelfEnd(void)
{
    if (coverCache) {
        cacheDestroyCache(coverCache);
        coverCache = NULL;
    }
    coverCacheId = -1;
    coverUID = -1;
}

// Cor estavel por jogo, derivada do codigo do disco. Dois jogos diferentes
// quase nunca caem na mesma cor, e o mesmo jogo tem sempre a mesma — a estante
// fica reconhecivel de longe sem guardar nada em disco.
static void rhSpineColor(const char *key, u8 *r, u8 *g, u8 *b)
{
    u32 h = 2166136261u; // FNV-1a

    if (key) {
        while (*key) {
            h ^= (u8)(*key++);
            h *= 16777619u;
        }
    }

    // Faixa 60..170: escuro o suficiente para o texto branco por cima, claro o
    // suficiente para as lombadas se distinguirem entre si.
    *r = 60 + (h & 0x7F) % 111;
    *g = 60 + ((h >> 8) & 0x7F) % 111;
    *b = 60 + ((h >> 16) & 0x7F) % 111;
}

static submenu_list_t *rhShelfGetList(menu_item_t **outMenu)
{
    menu_item_t *cur = menuGetSelectedItem();

    if (outMenu)
        *outMenu = cur;

    if (!cur || !cur->userdata)
        return NULL;

    return cur->submenu;
}

static GSTEXTURE *rhShelfGetCover(menu_item_t *menu, submenu_list_t *sel)
{
    item_list_t *list;
    char *startup;

    if (!gEnableArt || !coverCache || !menu || !sel)
        return NULL;

    list = (item_list_t *)menu->userdata;
    if (!list || !list->itemGetStartup)
        return NULL;

    startup = list->itemGetStartup(list, sel->item.id);
    if (!startup)
        return NULL;

    // Assincrono: devolve NULL enquanto o arquivo nao chegou. Quem desenha
    // trata isso como "ainda nao", nunca como erro.
    return cacheGetTexture(coverCache, list, &coverCacheId, &coverUID, startup);
}

// Desenha uma lombada. 'sel' engrossa, levanta e clareia — sem mudar o lugar
// das vizinhas, para a fila nao dancar quando o foco anda.
static void rhDrawSpine(int x, const char *key, int sel)
{
    int w = sel ? RH_SPINE_SEL_W : RH_SPINE_W;
    int h = sel ? RH_SPINE_SEL_H : RH_SPINE_H;
    int y = RH_SHELF_BASE_Y - h;
    u8 r, g, b;

    rhSpineColor(key, &r, &g, &b);

    if (sel) {
        // Clareia o selecionado sem estourar.
        r = (r > 175) ? 255 : r + 80;
        g = (g > 175) ? 255 : g + 80;
        b = (b > 175) ? 255 : b + 80;
    }

    rmDrawRect(x, y, w, h, RH_RGBA(r, g, b, RH_OPAQUE));

    // Filete claro no topo: sugere a espessura do papel e separa a lombada do
    // fundo escuro sem custar textura.
    rmDrawRect(x, y, w, 3, RH_RGBA(255, 255, 255, 0x40));

    if (sel) {
        // Contorno do item em foco. Quatro retangulos de 2 px, nao uma imagem.
        u64 c = RH_RGBA(255, 255, 255, RH_OPAQUE);
        rmDrawRect(x, y, w, 2, c);
        rmDrawRect(x, y + h - 2, w, 2, c);
        rmDrawRect(x, y, 2, h, c);
        rmDrawRect(x + w - 2, y, 2, h, c);
    }
}

// Fundo: duas faixas. Nao e um degrade de verdade — sao dois retangulos, e a
// diferenca no console e imperceptivel a 480 linhas entrelacadas.
static void rhDrawBackdrop(void)
{
    rmDrawRect(0, 0, 640, 300, RH_RGBA(14, 18, 28, RH_OPAQUE));
    rmDrawRect(0, 300, 640, 180, RH_RGBA(8, 10, 16, RH_OPAQUE));

    // A linha do movel. E o que faz as lombadas parecerem apoiadas em algo.
    rmDrawRect(0, RH_SHELF_BASE_Y, 640, 3, RH_RGBA(90, 100, 120, RH_OPAQUE));
    rmDrawRect(0, RH_SHELF_BASE_Y + 3, 640, 10, RH_RGBA(30, 34, 44, RH_OPAQUE));
}

// Moldura da capa. Aparece com ou sem imagem: sem ela o retangulo vazio ja
// comunica "e aqui que a capa vai".
static void rhDrawCoverFrame(GSTEXTURE *cover, const char *title)
{
    rmDrawRect(RH_COVER_X - 3, RH_COVER_Y - 3, RH_COVER_W + 6, RH_COVER_H + 6,
               RH_RGBA(0, 0, 0, 0x60));

    if (cover && cover->Mem) {
        rmDrawPixmap(cover, RH_COVER_X, RH_COVER_Y, ALIGN_NONE,
                     RH_COVER_W, RH_COVER_H, SCALING_RATIO,
                     RH_RGBA(0x80, 0x80, 0x80, RH_OPAQUE));
    } else {
        rmDrawRect(RH_COVER_X, RH_COVER_Y, RH_COVER_W, RH_COVER_H,
                   RH_RGBA(26, 32, 46, RH_OPAQUE));
        // Sem capa, o nome ocupa o lugar dela. Melhor que um vazio mudo.
        if (title)
            fntRenderString(0, RH_COVER_X + 12, RH_COVER_Y + 24, ALIGN_NONE,
                            RH_COVER_W - 24, 0, title,
                            RH_RGBA(0x60, 0x68, 0x78, RH_OPAQUE));
    }
}

void rhShelfRender(void)
{
    menu_item_t *menu = NULL;
    submenu_list_t *head = rhShelfGetList(&menu);
    submenu_list_t *sel = menu ? menu->current : NULL;
    item_list_t *list = menu ? (item_list_t *)menu->userdata : NULL;
    char *title = sel ? submenuItemGetText(&sel->item) : NULL;
    char *startup = NULL;
    int x, half, i;
    submenu_list_t *node;

    rhDrawBackdrop();

    if (!head || !sel) {
        // Literal de proposito: os ids de idioma vem de lang_autogen.h, gerado
        // no build a partir dos .lng. Criar um id novo para um aviso que quase
        // nunca aparece acoplaria esta tela ao sistema de traducao sem ganho.
        fntRenderString(0, 320, 220, ALIGN_CENTER, 0, 0,
                        "Prateleira vazia", RH_RGBA(0x70, 0x78, 0x88, RH_OPAQUE));
        return;
    }

    if (list && list->itemGetStartup)
        startup = list->itemGetStartup(list, sel->item.id);

    rhDrawCoverFrame(rhShelfGetCover(menu, sel), title);

    // Bloco de texto a direita da capa.
    if (title)
        fntRenderString(0, 276, RH_COVER_Y + 6, ALIGN_NONE, 640 - 276 - RH_MARGIN, 0,
                        title, RH_RGBA(0xFF, 0xFF, 0xFF, RH_OPAQUE));

    if (startup)
        fntRenderString(0, 276, RH_COVER_Y + 48, ALIGN_NONE, 0, 0, startup,
                        RH_RGBA(0x78, 0x88, 0xA8, RH_OPAQUE));

    // A prateleira, centrada no item em foco. Percorre para tras metade da
    // largura disponivel e desenha para a frente ate sair da tela.
    half = (640 - 2 * RH_MARGIN) / 2;
    node = sel;
    for (i = 0; i < half / (RH_SPINE_W + RH_SPINE_GAP) && node->prev; i++)
        node = node->prev;

    x = RH_MARGIN;
    while (node && x < 640 - RH_MARGIN) {
        char *k = NULL;

        if (list && list->itemGetStartup)
            k = list->itemGetStartup(list, node->item.id);
        if (!k)
            k = submenuItemGetText(&node->item);

        rhDrawSpine(x, k, node == sel);
        x += (node == sel ? RH_SPINE_SEL_W : RH_SPINE_W) + RH_SPINE_GAP;
        node = node->next;
    }
}

void rhShelfHandleInput(void)
{
    menu_item_t *menu = NULL;

    rhShelfGetList(&menu);
    if (!menu)
        return;

    if (getKey(KEY_LEFT)) {
        if (menu->current && menu->current->prev) {
            menu->current = menu->current->prev;
            sfxPlay(SFX_CURSOR);
        }
    } else if (getKey(KEY_RIGHT)) {
        if (menu->current && menu->current->next) {
            menu->current = menu->current->next;
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
        guiSwitchScreen(GUI_SCREEN_MAIN);
    } else if (getKeyOn(KEY_SELECT)) {
        if (menu->refresh)
            menu->refresh(menu);
    }
}
