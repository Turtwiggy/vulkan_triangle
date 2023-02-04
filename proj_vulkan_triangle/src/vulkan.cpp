#include "vulkan.hpp"

#include <stdio.h>  // printf, fprintf
#include <stdlib.h> // abort

namespace project {

void
check_vk_result(VkResult err)
{
  if (err == 0)
    return;
  fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
  if (err < 0)
    abort();
};

} // namespace project