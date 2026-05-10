#include <stdio.h>
#include <unistd.h>

int main() {
    printf("Before fork: I am process %d\n", getpid());
    
    fork();  
    
    printf("After fork: I am process %d\n", getpid());
    
    return 0;
}