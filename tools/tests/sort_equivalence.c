// Verificacao de equivalencia: qsort novo vs bubblesort original.
// Reproduz apenas as estruturas de que submenuSort depende.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    char *text;
    int text_id;
    int id;
} submenu_item_t;

struct submenu_list;
typedef struct submenu_list
{
    struct submenu_list *prev;
    submenu_item_t item;
    struct submenu_list *next;
} submenu_list_t;

char *submenuItemGetText(submenu_item_t *it) { return it->text; }

/* ---------------- original ---------------- */
static void swap(submenu_list_t *a, submenu_list_t *b)
{
    submenu_list_t *pa, *nb;
    pa = a->prev;
    nb = b->next;
    a->next = nb;
    b->prev = pa;
    b->next = a;
    a->prev = b;
    if (pa)
        pa->next = b;
    if (nb)
        nb->prev = a;
}

static void submenuSortBubble(submenu_list_t **submenu)
{
    submenu_list_t *head;
    int sorted = 0;
    if ((submenu == NULL) || (*submenu == NULL) || ((*submenu)->next == NULL))
        return;
    head = *submenu;
    while (!sorted) {
        sorted = 1;
        submenu_list_t *tip = head;
        while (tip->next) {
            submenu_list_t *nxt = tip->next;
            char *txt1 = submenuItemGetText(&tip->item);
            char *txt2 = submenuItemGetText(&nxt->item);
            int cmp = strcasecmp(txt1, txt2);
            if (cmp > 0) {
                swap(tip, nxt);
                if (tip == head)
                    head = nxt;
                sorted = 0;
            } else {
                tip = tip->next;
            }
        }
    }
    *submenu = head;
}

/* ---------------- novo ---------------- */
typedef struct
{
    submenu_list_t *node;
    int idx;
} submenu_ref_t;

static int submenuSortCompare(const void *a, const void *b)
{
    const submenu_ref_t *ra = (const submenu_ref_t *)a;
    const submenu_ref_t *rb = (const submenu_ref_t *)b;
    int cmp = strcasecmp(submenuItemGetText(&ra->node->item),
                         submenuItemGetText(&rb->node->item));
    if (cmp != 0)
        return cmp;
    return ra->idx - rb->idx;
}

static int forced_oom = 0;

void submenuSort(submenu_list_t **submenu)
{
    submenu_list_t *node;
    submenu_ref_t *refs;
    int count, i;

    if ((submenu == NULL) || (*submenu == NULL) || ((*submenu)->next == NULL))
        return;

    for (count = 0, node = *submenu; node != NULL; node = node->next)
        count++;

    refs = forced_oom ? NULL : (submenu_ref_t *)malloc(count * sizeof(submenu_ref_t));
    if (refs == NULL) {
        submenuSortBubble(submenu);
        return;
    }

    for (i = 0, node = *submenu; node != NULL; node = node->next, i++) {
        refs[i].node = node;
        refs[i].idx = i;
    }

    qsort(refs, count, sizeof(submenu_ref_t), submenuSortCompare);

    for (i = 0; i < count; i++) {
        refs[i].node->prev = (i > 0) ? refs[i - 1].node : NULL;
        refs[i].node->next = (i + 1 < count) ? refs[i + 1].node : NULL;
    }

    *submenu = refs[0].node;
    free(refs);
}

/* ---------------- arnes ---------------- */
static submenu_list_t *build(char **titles, int n)
{
    submenu_list_t *head = NULL, *tail = NULL;
    for (int i = 0; i < n; i++) {
        submenu_list_t *it = calloc(1, sizeof(submenu_list_t));
        it->item.text = titles[i];
        it->item.id = i;
        if (!head) {
            head = tail = it;
        } else {
            tail->next = it;
            it->prev = tail;
            tail = it;
        }
    }
    return head;
}

// Devolve a sequencia de ids e checa a integridade dos elos nos dois sentidos.
static int dump(submenu_list_t *h, int *out, int n)
{
    int c = 0;
    submenu_list_t *p = NULL, *it = h;
    if (h && h->prev != NULL) {
        printf("  ERRO: prev da cabeca nao e NULL\n");
        return -1;
    }
    while (it) {
        if (c >= n) {
            printf("  ERRO: lista maior que o esperado (ciclo?)\n");
            return -1;
        }
        if (it->prev != p) {
            printf("  ERRO: elo prev quebrado em %d\n", c);
            return -1;
        }
        out[c++] = it->item.id;
        p = it;
        it = it->next;
    }
    // percorre de volta pelo prev
    int back = 0;
    for (submenu_list_t *q = p; q; q = q->prev)
        back++;
    if (back != c) {
        printf("  ERRO: travessia reversa deu %d, esperado %d\n", back, c);
        return -1;
    }
    return c;
}

static void freelist(submenu_list_t *h)
{
    while (h) {
        submenu_list_t *n = h->next;
        free(h);
        h = n;
    }
}

static int run_case(const char *name, char **titles, int n, int oom)
{
    submenu_list_t *a = build(titles, n);
    submenu_list_t *b = build(titles, n);
    int *ra = calloc(n + 8, sizeof(int)), *rb = calloc(n + 8, sizeof(int));

    submenuSortBubble(&a);
    forced_oom = oom;
    submenuSort(&b);
    forced_oom = 0;

    int ca = dump(a, ra, n + 8);
    int cb = dump(b, rb, n + 8);
    int ok = (ca == n && cb == n && memcmp(ra, rb, n * sizeof(int)) == 0);

    printf("%-42s %s (n=%d%s)\n", name, ok ? "OK" : "FALHOU", n, oom ? ", oom" : "");
    if (!ok && n <= 20) {
        printf("   bubble:");
        for (int i = 0; i < ca; i++) printf(" %d", ra[i]);
        printf("\n   qsort :");
        for (int i = 0; i < cb; i++) printf(" %d", rb[i]);
        printf("\n");
    }
    free(ra); free(rb); freelist(a); freelist(b);
    return ok;
}

int main(void)
{
    int fails = 0;

    char *t1[] = {"Zelda"};
    char *t2[] = {"Zelda", "Ape Escape"};
    char *t3[] = {"Ape Escape", "Zelda"};
    char *t4[] = {"God of War", "god of war", "GOD OF WAR"};   // empate por caixa
    char *t5[] = {"Ico", "Ico", "Ico", "Ico"};                 // todos iguais
    char *t6[] = {"Tekken 5", "Tekken 4", "Tekken 3", "Tekken 2", "Tekken"};
    char *t7[] = {"a", "B", "c", "D", "e", "F", "g", "H"};
    char *t8[] = {"", "Zelda", "", "Ape"};                     // titulos vazios

    fails += !run_case("um item", t1, 1, 0);
    fails += !run_case("dois, fora de ordem", t2, 2, 0);
    fails += !run_case("dois, ja em ordem", t3, 2, 0);
    fails += !run_case("empate por caixa (estabilidade)", t4, 3, 0);
    fails += !run_case("todos iguais", t5, 4, 0);
    fails += !run_case("ordem inversa", t6, 5, 0);
    fails += !run_case("caixa alternada", t7, 8, 0);
    fails += !run_case("titulos vazios", t8, 4, 0);
    fails += !run_case("fallback com malloc falhando", t6, 5, 1);

    // aleatorio, com repeticoes forcadas para exercitar o desempate
    srand(1234);
    static char pool[600][24];
    static char *ptr[600];
    for (int trial = 0; trial < 5; trial++) {
        int n = 50 + rand() % 400;
        for (int i = 0; i < n; i++) {
            int r = rand() % 40;
            const char *caixa = (rand() % 2) ? "Jogo %02d" : "JOGO %02d";
            snprintf(pool[i], sizeof(pool[i]), caixa, r);
            ptr[i] = pool[i];
        }
        char nome[64];
        snprintf(nome, sizeof(nome), "aleatorio com repeticoes #%d", trial + 1);
        fails += !run_case(nome, ptr, n, 0);
    }

    // custo: 400 jogos
    {
        int n = 400;
        for (int i = 0; i < n; i++) {
            snprintf(pool[i], sizeof(pool[i]), "Jogo %04d", (n - i) * 7919 % 10000);
            ptr[i] = pool[i];
        }
        submenu_list_t *a = build(ptr, n), *b = build(ptr, n);
        struct timespec s, e;
        clock_gettime(CLOCK_MONOTONIC, &s);
        submenuSortBubble(&a);
        clock_gettime(CLOCK_MONOTONIC, &e);
        double tb = (e.tv_sec - s.tv_sec) * 1e3 + (e.tv_nsec - s.tv_nsec) / 1e6;
        clock_gettime(CLOCK_MONOTONIC, &s);
        submenuSort(&b);
        clock_gettime(CLOCK_MONOTONIC, &e);
        double tq = (e.tv_sec - s.tv_sec) * 1e3 + (e.tv_nsec - s.tv_nsec) / 1e6;
        printf("\ncusto no PC com %d jogos: bubble %.2f ms | qsort %.2f ms | %.0fx\n",
               n, tb, tq, tb / (tq > 0 ? tq : 1e-9));
        freelist(a); freelist(b);
    }

    printf("\n%s\n", fails ? "HOUVE FALHAS" : "todos os casos identicos ao original");
    return fails != 0;
}
