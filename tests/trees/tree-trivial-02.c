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
  tree_node *l1000 = calloc(1, sizeof(tree_node));
  tree_node *l0100 = calloc(1, sizeof(tree_node));
  tree_node *l0010 = calloc(1, sizeof(tree_node));
  tree_node *l0001 = calloc(1, sizeof(tree_node));

  tree_node *l10 = calloc(1, sizeof(tree_node));
  tree_node *l01 = calloc(1, sizeof(tree_node));

  tree_node *root = calloc(1, sizeof(tree_node));

  l10->left = l1000;
  l10->right = l0100;
  l01->left = l0010;
  l01->right = l0001;

  connect(root, &l10, &l01);

  printf("Root: %x Left: %x Right: %x\n", root, root->left, root->right);
  return 0;
}
