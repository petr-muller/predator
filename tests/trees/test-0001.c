#include <stdlib.h>
#include <verifier-builtins.h>

#include "dummy-tree.h"

int main(){
  struct tree_node *root = alloc_node();
  ___sl_plot("test-0001-snapshot");
  struct tree_node *tmproot1 = simple_tree(true, true);
  ___sl_plot("test-0002-snapshot");
  struct tree_node *tmproot2 = simple_tree(true, true);
  ___sl_plot("test-0003-snapshot");
  bind_tree(root, tmproot1, tmproot2);

  ___sl_plot("test-0004-snapshot");
  tmproot1=NULL;
  tmproot2=NULL;
  ___sl_plot("test-0005-snapshot");

  return 0;
}
