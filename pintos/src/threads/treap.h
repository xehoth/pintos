#ifndef THREADS_TREAP_H
#define THREADS_TREAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// XOR-SHIFT random algorithm for treap rank
static uint32_t
treap_rand ()
{
  static uint32_t seed = 495;
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

// Treap Node
struct treap_node
{
  struct treap_node *child[2];
  void *data;
  uint32_t rank;
  int size;
};

/**/
typedef bool treap_cmp_func (const struct treap_node *a,
                             const struct treap_node *b);

struct treap
{
  struct treap_node *root;
  treap_cmp_func *cmp;
};

static void
treap_init (struct treap *t, treap_cmp_func *cmp)
{
  t->root = NULL;
  t->cmp = cmp;
}

static void
treap_node_init (struct treap_node *node, void *data)
{
  node->child[0] = node->child[1] = NULL;
  node->data = data;
  node->rank = treap_rand ();
  node->size = 1;
}

static void
treap_node_maintain (struct treap_node *node)
{
  node->size = 1;
  if (node->child[0])
    node->size += node->child[0]->size;
  if (node->child[1])
    node->size += node->child[1]->size;
}

static struct treap_node *
treap_node_merge (struct treap_node *u, struct treap_node *v)
{
  if (!u)
    return v;
  if (!v)
    return u;
  if (u->rank < v->rank)
    {
      u->child[1] = treap_node_merge (u->child[1], v);
      treap_node_maintain (u);
      return u;
    }
  else
    {
      v->child[0] = treap_node_merge (u, v->child[0]);
      treap_node_maintain (v);
      return v;
    }
  return NULL;
}

static void
treap_node_split (struct treap_node *p, int k, struct treap_node **l,
                  struct treap_node **r)
{
  if (!p)
    {
      *l = *r = NULL;
      return;
    }
  int left_size = p->child[0] ? p->child[0]->size : 0;
  if (k <= left_size)
    {
      treap_node_split (p->child[0], k, l, r);
      p->child[0] = *r;
      *r = p;
    }
  else
    {
      treap_node_split (p->child[1], k - left_size - 1, l, r);
      p->child[1] = *l;
      *l = p;
    }
  treap_node_maintain (p);
}

static int
treap_lower_rank (struct treap *t, struct treap_node *node)
{
  int ret = 0;
  for (struct treap_node *p = t->root; p;)
    {
      int left_size = p->child[0] ? p->child[0]->size : 0;
      if (!t->cmp (p, node))
        {
          p = p->child[0];
        }
      else
        {
          ret += left_size + 1;
          p = p->child[1];
        }
    }
  return ret;
}

static int
treap_upper_rank (struct treap *t, struct treap_node *node)
{
  int ret = 0;
  for (struct treap_node *p = t->root; p;)
    {
      int left_size = p->child[0] ? p->child[0]->size : 0;
      if (t->cmp (node, p))
        {
          p = p->child[0];
        }
      else
        {
          ret += left_size + 1;
          p = p->child[1];
        }
    }
  return ret;
}

static struct treap_node *
treap_select (struct treap *t, int k)
{
  struct treap_node *p = t->root;
  for (; p;)
    {
      int left_size = p->child[0] ? p->child[0]->size : 0;
      if (left_size + 1 == k)
        break;
      if (k <= left_size)
        {
          p = p->child[0];
        }
      else
        {
          k -= left_size + 1;
          p = p->child[1];
        }
    }
  return p;
}

static void
treap_insert (struct treap *t, struct treap_node *node)
{
  int k = treap_lower_rank (t, node);
  struct treap_node *l, *r;
  treap_node_split (t->root, k, &l, &r);
  t->root = treap_node_merge (l, treap_node_merge (node, r));
}

static void
treap_erase (struct treap *t, struct treap_node *node)
{
  int k = treap_lower_rank (t, node);
  struct treap_node *l, *r, *L, *R;
  treap_node_split (t->root, k, &l, &r);
  treap_node_split (r, 1, &L, &R);
  t->root = treap_node_merge (l, R);
}

static int
treap_size (struct treap *t)
{
  if (t->root)
    return t->root->size;
  return 0;
}

typedef void treap_foreach_func (struct treap_node *node, void *aux);

static void
treap_node_foreach_inorder (struct treap_node *node, treap_foreach_func *func,
                            void *aux)
{
  if (!node)
    return;
  treap_node_foreach_inorder (node->child[0], func, aux);
  func (node, aux);
  treap_node_foreach_inorder (node->child[1], func, aux);
}

static void
treap_foreach (struct treap *t, treap_foreach_func *func, void *aux)
{
  treap_node_foreach_inorder (t->root, func, aux);
}

static struct treap_node *
treap_front (struct treap *t)
{
  return treap_select (t, 1);
}

static struct treap_node *
treap_pop_front (struct treap *t)
{
  struct treap_node *ret = treap_front (t);
  treap_erase (t, ret);
  return ret;
}
#endif