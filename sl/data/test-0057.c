#include "../sl.h"
#include <stdlib.h>

struct item {
    struct item *next;
    struct item *prev;
};

struct item* alloc_or_die(void)
{
    struct item *pi = malloc(sizeof(*pi));
    if (pi)
        return pi;
    else
        abort();
}

struct item* alloc_and_zero(void)
{
    struct item *pi = alloc_or_die();
    pi->next = NULL;
    pi->prev = NULL;

    return pi;
}

struct item* create_dll(void)
{
    struct item *dll = alloc_and_zero();
    struct item *now = dll;

    // NOTE: running this on bare metal may cause the machine to swap a bit
    int i;
    for (i = 1; i; ++i) {
        now->next = alloc_and_zero();
        now->next->prev = now;
        now = now->next;
    }

    return dll;
}

void fast_forward(struct item **pDll)
{
    struct item *next;
    while ((next = (*pDll)->next)) {
        ___sl_plot("01-ff-one-step");
        *pDll = next;
    }
}

int main()
{
    struct item *dll = create_dll();
    fast_forward(&dll);
    ___sl_plot_by_ptr(dll, "03-ff-done");

    return 0;
}