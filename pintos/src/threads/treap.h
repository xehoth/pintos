#ifndef THREADS_TREAP_H
#define THREADS_TREAP_H
#include <debug.h>
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

struct treap;
// Treap Node
struct treap_node
{
  struct treap_node *child[2];
  void *data;
  uint32_t rank;
  int size;
  struct treap *treap;
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
#ifndef NDEBUG
  ASSERT (t && cmp);
#endif
  if (!t || !cmp)
    return;
  t->root = NULL;
  t->cmp = cmp;
}

static void
treap_node_init (struct treap_node *node, void *data)
{
#ifndef NDEBUG
  ASSERT (node);
#endif
  if (!node)
    return;
  node->child[0] = node->child[1] = NULL;
  node->data = data;
  node->rank = treap_rand ();
  node->size = 1;
  node->treap = NULL;
}

static void
treap_node_maintain (struct treap_node *node)
{
#ifndef NDEBUG
  ASSERT (node);
#endif
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
#ifndef NDEBUG
  ASSERT (t && node);
#endif
  if (!t || !node)
    return 0;
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
#ifndef NDEBUG
  ASSERT (t && node);
#endif
  if (!t || !node)
    return 0;
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
#ifndef NDEBUG
  ASSERT (t && k >= 1);
#endif
  if (!t || k <= 0)
    return NULL;
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

static bool
treap_find (struct treap *t, struct treap_node *node)
{
#ifndef NDEBUG
  ASSERT (t && node);
#endif
  if (!t || !node)
    return false;
  return treap_select (t, treap_lower_rank (t, node) + 1) == node;
}

static void
treap_insert (struct treap *t, struct treap_node *node)
{
#ifndef NDEBUG
  ASSERT (t && node);
  ASSERT (!treap_find (t, node));
#endif
  if (treap_find (t, node))
    return;
  treap_node_init (node, node->data);
  node->treap = t;
  int k = treap_lower_rank (t, node);
  struct treap_node *l, *r;
  treap_node_split (t->root, k, &l, &r);
  t->root = treap_node_merge (l, treap_node_merge (node, r));
}

static void
treap_erase (struct treap *t, struct treap_node *node)
{
#ifndef NDEBUG
  ASSERT (t && node);
  ASSERT (treap_find (t, node));
#endif
  if (!treap_find (t, node))
    return;
  int k = treap_lower_rank (t, node);
  struct treap_node *l, *r, *L, *R;
  treap_node_split (t->root, k, &l, &r);
  treap_node_split (r, 1, &L, &R);
  t->root = treap_node_merge (l, R);
  treap_node_init (node, node->data);
}

static int
treap_size (struct treap *t)
{
#ifndef NDEBUG
  ASSERT (t);
#endif
  if (!t)
    return 0;
  if (t->root)
    return t->root->size;
  return 0;
}

typedef void treap_node_action_func (struct treap_node *node, void *aux);
static void
treap_node_update (struct treap_node *node, treap_node_action_func *func,
                   void *aux)
{
#ifndef NDEBUG
  ASSERT (node && func);
#endif
  if (!node || !func)
    return;
  struct treap *treap = node->treap;
  if (!treap)
    return;
#ifndef NDEBUG
  ASSERT (treap_find (treap, node));
#endif
  if (!treap_find (treap, node))
    return;
  treap_erase (treap, node);
  func (node, aux);
  treap_node_init (node, node->data);
  node->treap = treap;
  treap_insert (treap, node);
}

static void
treap_node_foreach_inorder (struct treap_node *node,
                            treap_node_action_func *func, void *aux)
{
  if (!node)
    return;
  treap_node_foreach_inorder (node->child[0], func, aux);
  func (node, aux);
  treap_node_foreach_inorder (node->child[1], func, aux);
}

static void
treap_foreach (struct treap *t, treap_node_action_func *func, void *aux)
{
  treap_node_foreach_inorder (t->root, func, aux);
}

static struct treap_node *
treap_front (struct treap *t)
{
#ifndef NDEBUG
  ASSERT (t);
#endif
  if (!t)
    return NULL;
  return treap_select (t, 1);
}

static struct treap_node *
treap_pop_front (struct treap *t)
{
#ifndef NDEBUG
  ASSERT (t);
#endif
  if (!t)
    return NULL;
  struct treap_node *ret = treap_front (t);
  treap_erase (t, ret);
  return ret;
}
#endif