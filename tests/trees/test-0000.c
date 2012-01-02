#include <stdlib.h>
#include <verifier-builtins.h>

#include "dummy-tree.h"

int main(){
  struct tree_node *tmproot1 = simple_tree(true, true);
  ___sl_plot("test-0001-snapshot");

  return 0;
}
