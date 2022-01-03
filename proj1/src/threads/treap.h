/**
 * Created by xehoth on 2021/10/2.
 */
#ifndef THREADS_TREAP_H
#define THREADS_TREAP_H
#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* XOR-SHIFT random algorithm for treap rank */
static uint32_t
treap_rand ()
{
  static uint32_t seed = 495; /* An arbitrary seed for xor-shift */
  seed ^= seed << 13;         /* 13: First step in xor-shift */
  seed ^= seed >> 17;         /* 17: Second step in xor-shift */
  seed ^= seed << 5;          /* 5: Third step in xor-shift */
  return seed;
}

/* Treap struct */
struct treap;

/* Treap note struct */
struct treap_node
{
  struct treap_node *child[2]; /* Tree struct childs, 0/1 for left and right */
  void *data;                  /* Data stored in treap node */
  uint32_t rank;               /* Treap node rank */
  int size;                    /* Size of the current node's subtree */
  struct treap *treap;         /* The root of this treap */
};

/* Compare function used in treap */
typedef bool treap_cmp_func (const struct treap_node *a,
                             const struct treap_node *b);

/* Treap struct */
struct treap
{
  struct treap_node *root; /* Root node of this treap */
  treap_cmp_func *cmp;     /* The comparing function used in this treap */
};

/* Init a new treap with given comparing function */
static void
treap_init (struct treap *t, treap_cmp_func *cmp)
{
  /* Invalid treap or cmp func */
  if (!t || !cmp)
    return;
  /* Init a treap with null node root and given cmp func */
  t->root = NULL;
  t->cmp = cmp;
}

/* Init a new treap node with given data */
static void
treap_node_init (struct treap_node *node, void *data)
{
  /* Invailid treap node */
  if (!node)
    return;
  /* The only node do not have children */
  node->child[0] = node->child[1] = NULL;
  node->data = data;
  node->rank = treap_rand ();
  /* One new node's size is 1 */
  node->size = 1;
  node->treap = NULL;
}

/* Maintain the infomation treap needs */
static void
treap_node_maintain (struct treap_node *node)
{
  /* Calculate the size of the current node's subtree */
  node->size = 1;
  if (node->child[0])
    node->size += node->child[0]->size;
  if (node->child[1])
    node->size += node->child[1]->size;
}

/* Merge two treap nodes, they should satisfies the order feature of treap
   Time complexity: O(\log n)
*/
static struct treap_node *
treap_node_merge (struct treap_node *u, struct treap_node *v)
{
  if (!u)
    return v;
  if (!v)
    return u;
  /* Compare the rank of the treap to maintain the feature of heap */
  if (u->rank < v->rank)
    {
      /* Min-heap & balanced tree feature, merge v into u's right child */
      u->child[1] = treap_node_merge (u->child[1], v);
      treap_node_maintain (u);
      return u;
    }
  else
    {
      /* Min-heap & balanced tree feature, merge u into v's left child */
      v->child[0] = treap_node_merge (u, v->child[0]);
      treap_node_maintain (v);
      return v;
    }
  return NULL;
}

/* Split treap p into two parts with given size k
   Time complexity: O(\log n)
*/
static void
treap_node_split (struct treap_node *p, int k, struct treap_node **l,
                  struct treap_node **r)
{
  /* Recursion boundary with null node */
  if (!p)
    {
      *l = *r = NULL;
      return;
    }
  /* Left subtree's size */
  int left_size = p->child[0] ? p->child[0]->size : 0;
  if (k <= left_size)
    {
      /* k is less, then just need to split in left subtree */
      treap_node_split (p->child[0], k, l, r);
      p->child[0] = *r;
      *r = p;
    }
  else
    {
      /* Need more nodes, k - left_size - 1 are needed in right subtree */
      treap_node_split (p->child[1], k - left_size - 1, l, r);
      p->child[1] = *l;
      *l = p;
    }
  /* Update treap infomation */
  treap_node_maintain (p);
}

/* Get how many nodes in the treap t that are less than given node */
static int
treap_lower_rank (struct treap *t, struct treap_node *node)
{
  /* Invalid input */
  if (!t || !node)
    return 0; /* For this case, we give 0 showing that no nodes satisfied */
  /* Init the ret with 0 the same as above */
  int ret = 0;
  /* Loop the treap from top to the bottom */
  for (struct treap_node *p = t->root; p;)
    {
      /* Left subtree's size */
      int left_size = p->child[0] ? p->child[0]->size : 0;
      /* Lower rank: when p < node we count size */
      if (!t->cmp (p, node))
        {
          /* p >= node */
          p = p->child[0];
        }
      else
        {
          /* p < node, count the left subtree and current node */
          ret += left_size + 1;
          p = p->child[1];
        }
    }
  return ret;
}

/* Get how many nodes in the treap t that are less/equal than given node */
static int
treap_upper_rank (struct treap *t, struct treap_node *node)
{
  /* Invalid input */
  if (!t || !node)
    return 0; /* For this case, we give 0 showing that no nodes satisfied */
  /* Init the ret with 0 the same as above */
  int ret = 0;
  /* Loop the treap from top to the bottom */
  for (struct treap_node *p = t->root; p;)
    {
      /* Left subtree's size */
      int left_size = p->child[0] ? p->child[0]->size : 0;
      /* Lower rank: when p <= node we count size */
      if (t->cmp (node, p))
        {
          /* p > node */
          p = p->child[0];
        }
      else
        {
          /* p <= node, count the left subtree and current node */
          ret += left_size + 1;
          p = p->child[1];
        }
    }
  return ret;
}

/* Select the k'th smallest node in the treap */
static struct treap_node *
treap_select (struct treap *t, int k)
{
#ifdef _MDEBUG
  ASSERT (t && k >= 1);
#endif
  /* Invalid case */
  if (!t || k <= 0)
    return NULL;
  /* Search treap top-down */
  struct treap_node *p = t->root;
  for (; p;)
    {
      /* Left subtree's size */
      int left_size = p->child[0] ? p->child[0]->size : 0;
      /* Here we find the node */
      if (left_size + 1 == k)
        break;
      if (k <= left_size)
        {
          /* The node should be in the left subtree */
          p = p->child[0];
        }
      else
        {
          /* The node should be in the right subtree */
          /* Calculate the remaining size for k */
          k -= left_size + 1;
          p = p->child[1];
        }
    }
  return p;
}

/* Try to find a node in a treap, return found or not */
static bool
treap_find (struct treap *t, struct treap_node *node)
{
#ifdef _MDEBUG
  ASSERT (t && node);
#endif
  /* Invalid case */
  if (!t || !node)
    return false;
  /* size(< node) + 1 is exactly the node's rank in the treap */
  return treap_select (t, treap_lower_rank (t, node) + 1) == node;
}

/* Insert a node into a treap */
static void
treap_insert (struct treap *t, struct treap_node *node)
{
#ifdef _MDEBUG
  ASSERT (t && node);
  ASSERT (!treap_find (t, node));
#endif
  /* If already inserted */
  if (treap_find (t, node))
    return;
  /* Init the node */
  treap_node_init (node, node->data);
  node->treap = t;
  /* Find the place to insert the node */
  int k = treap_lower_rank (t, node);
  struct treap_node *l, *r;
  /* Split the treap into two part with the given place */
  treap_node_split (t->root, k, &l, &r);
  /* Merge the treap with a new node */
  t->root = treap_node_merge (l, treap_node_merge (node, r));
}

/* Erase a node in the treap */
static void
treap_erase (struct treap *t, struct treap_node *node)
{
#ifdef _MDEBUG
  ASSERT (t && node);
  ASSERT (treap_find (t, node));
#endif
  /* No such node in the treap */
  if (!treap_find (t, node))
    return;
  /* Find the place to erase the node */
  int k = treap_lower_rank (t, node);
  struct treap_node *l, *r, *L, *R;
  /* Split the treap into three parts */
  treap_node_split (t->root, k, &l, &r);
  /* The mid one is the node to erase */
  treap_node_split (r, 1, &L, &R);
  /* Merge the side treap into one */
  t->root = treap_node_merge (l, R);
  /* Clear the info for the erased node */
  treap_node_init (node, node->data);
}

/* Get the total size of a treap */
static int
treap_size (struct treap *t)
{
#ifdef _MDEBUG
  ASSERT (t);
#endif
  /* Invalid case */
  if (!t)
    return 0; /* Give 0 because of no treap */
  if (t->root)
    return t->root->size;
  /* Give 0 because of no node */
  return 0;
}

/* A update func in treap */
typedef void treap_node_action_func (struct treap_node *node, void *aux);

/* Update a node in the treap */
static void
treap_node_update (struct treap_node *node, treap_node_action_func *func,
                   void *aux)
{
#ifdef _MDEBUG
  ASSERT (node && func);
#endif
  /* Invalid case */
  if (!node || !func)
    return;
  struct treap *treap = node->treap;
  /* Invalid case */
  if (!treap)
    return;
#ifdef _MDEBUG
  ASSERT (treap_find (treap, node));
#endif
  /* Invalid case: the node must be in the treap */
  if (!treap_find (treap, node))
    return;
  /* First erase the node */
  treap_erase (treap, node);
  /* Do update */
  func (node, aux);
  treap_node_init (node, node->data);
  node->treap = treap;
  /* Re-insert the node */
  treap_insert (treap, node);
}

/* For each node in the treap with inorder */
static void
treap_node_foreach_inorder (struct treap_node *node,
                            treap_node_action_func *func, void *aux)
{
  /* Recursion boundary */
  if (!node)
    return;
  /* Inorder: left, this, right */
  treap_node_foreach_inorder (node->child[0], func, aux);
  func (node, aux);
  treap_node_foreach_inorder (node->child[1], func, aux);
}

/* Treap foreach inorder */
static void
treap_foreach (struct treap *t, treap_node_action_func *func, void *aux)
{
  treap_node_foreach_inorder (t->root, func, aux);
}

/* Get the smallest node in the treap */
static struct treap_node *
treap_front (struct treap *t)
{
#ifdef _MDEBUG
  ASSERT (t);
#endif
  /* Invalid case */
  if (!t)
    return NULL;
  /* The samllest rank is 1 */
  return treap_select (t, 1);
}

/* Erase the smallest node */
static struct treap_node *
treap_pop_front (struct treap *t)
{
#ifdef _MDEBUG
  ASSERT (t);
#endif
  /* Invalid case */
  if (!t)
    return NULL;
  /* Get the front and then erase */
  struct treap_node *ret = treap_front (t);
  treap_erase (t, ret);
  return ret;
}
#endif