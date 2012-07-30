#include "trees.h"

#include <stdlib.h>
#include <stdio.h>

void connect(tree_node *root, tree_node **l, tree_node **r){
  root->left = *l;
  root->right = *r;

  *l = NULL;
  *r = NULL;
}

int main(){
  tree_node *root = malloc(sizeof(tree_node));
  tree_node *left = malloc(sizeof(tree_node));
  tree_node *right = malloc(sizeof(tree_node));

  left->left = NULL;
  left->right = NULL;
  right->left = NULL;
  right->right = NULL;

  connect(root, &left, &right);

  printf("Root: %x Left: %x Right: %x\n", root, root->left, root->right);
  free(root->left);
  free(root->right);
  free(root);

  return 0;
}
