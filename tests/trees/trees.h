/*
 * trees.c
 *
 * Naive binary search tree implementation.
 * Author: Petr Muller
 */

struct tree_node {
  int offsetter;
  struct tree_node *left;
  struct tree_node *right;
};

typedef struct tree_node tree_node;
