#include <stdlib.h>
#include <verifier-builtins.h>

#include "dummy-tree.h"

int main(){
  struct tree_node *root = alloc_node();
  ___sl_plot("test-0001-snapshot");
  root->left = simple_tree(true, true);
  ___sl_plot("test-0002-snapshot");
  root->right = simple_tree(true, true);
  ___sl_plot("test-0003-snapshot");

  return 0;
}
