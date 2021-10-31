#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define NFORK 10
#define IO 5

int main(int argc, char *argv[])
{
    int n, pid;
    int numofforks = 0;
    //volatile double x = 0;
    //int wtime, rtime;
    //int twtime = 0, trtime = 0;

    if (argc < 2)
    {
        numofforks = 1; //default value
    }
    else
    {
        numofforks = atoi(argv[1]);
    }


    for (n = 0; n < numofforks; n++)
    {
        pid = fork();
        if (pid < 0)
            break;
        else if (pid > 0)
        {
            //in parent
            //printf("Parent %d creating child  %d\n", getpid(), pid);
            wait(0);
        }
        else
        {
            //in child
            printf("Child %d running\n", getpid());
            for (volatile int i = 0; i < 2000000000; i++)
            {
                //x = x + 3*84;
            } // CPU bound process
            
            printf("Process %d finished\n", n);
            break;
            //exit(0);
        }
    }
    // for (; n > 0; n--)
    // {
    //     if (waitx(0, &wtime, &rtime) >= 0)
    //     {
    //         trtime += rtime;
    //         twtime += wtime;
    //     }
    // }
    printf("Done\n");
    exit(0);
}

// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"
// #include "kernel/fcntl.h"

// int main(int argc, char *argv[])
// {
//     volatile int k, n, pid;
//     volatile double x = 0;
//     volatile int z;

//     if (argc < 2)
//     {
//         n = 1; //default value
//     }
//     else
//     {
//         n = atoi(argv[1]);
//     }

//     x = 0;
//     pid = 0;
//     for (k = 0; k < n; k++)
//     {
//         pid = fork();
//         if (pid < 0)
//         {
//             printf("Fork failed\n");
//             break;
//         }
//         else if (pid > 0)
//         {
//             //in parent
//             printf("Parent %d creating child  %d\n", getpid(), pid);
//             wait(0);
//         }
//         else
//         {
//             //in child
//             printf("Child %d running\n", getpid());
//             for (z = 0; z < 9000000; z++)
//             {
//                 x = x + 3.14 * 84.32;       //calculations to consume CPU time
//             }
//             exit(0);
//             //break;
//         }
//     }
//     exit(0);
// }