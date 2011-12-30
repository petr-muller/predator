#include <stdbool.h>

struct tree_node{
  struct tree_node *left;
  struct tree_node *right;
};

void bind_tree(struct tree_node *parent,
               struct tree_node *l,
               struct tree_node *r){
  parent->left = l;
  parent->right = r;
}

struct tree_node *alloc_node(){
  struct tree_node *retval = malloc(sizeof(struct tree_node));
  if (!retval){
    abort();
  }
  retval->left = NULL;
  retval->right = NULL;
  return retval;
}

struct tree_node *simple_tree(bool left, bool right){
  struct tree_node *retval = alloc_node();
  if (left)
    retval->left = alloc_node();
  if (right)
    retval->right = alloc_node();

  return retval;
}
