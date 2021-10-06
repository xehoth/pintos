#include "vm/page.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

extern bool install_page (void *, void *, bool);

bool
grow_stack (void *addr)
{
  void *kpage = palloc_get_page (PAL_USER);
  void *upage = pg_round_down (addr);
  return install_page (upage, kpage, true);
}