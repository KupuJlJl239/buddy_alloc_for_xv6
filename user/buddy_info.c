
#include "kernel/types.h"
#include "kernel/buddy_alloc.h"
#include "user/user.h"

int main(){
    struct buddy_info info;
    if(buddy_info(&info) != 0){
        printf("buddy info: kernel error\n");
    }

    printf("buddy_info:\n  total=%d,\n  free=%d,\n  free_by_size={", info.total, info.free);
    for(int i = 0; i < BUDDY_LEVELS; i++){
        printf("%d", info.free_by_size[i]);
        if(i != BUDDY_LEVELS - 1)
            printf(",");
    }
    printf("}\n");

    exit(0);
}