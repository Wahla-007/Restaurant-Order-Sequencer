#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

typedef struct {
    char name[30];
    int cpu_burst;
    int priority;
    int waiting_time;
    int turnaround_time;
} Service;

void* thread_function(void* arg) {
    int thread_num = *(int*)arg;
    printf("Thread %d: Thread ID = %lu, Process ID = %d\n", 
           thread_num, (unsigned long)pthread_self(), getpid());
    return NULL;
}

void sort_by_priority(Service services[], int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (services[j].priority > services[j + 1].priority) {
                Service temp = services[j];
                services[j] = services[j + 1];
                services[j + 1] = temp;
            }
        }
    }
}

void calculate_times(Service services[], int n) {
    services[0].waiting_time = 0;
    services[0].turnaround_time = services[0].cpu_burst;

    for (int i = 1; i < n; i++) {
        services[i].waiting_time = services[i - 1].waiting_time + services[i - 1].cpu_burst;
        services[i].turnaround_time = services[i].waiting_time + services[i].cpu_burst;
    }
}

int main() {
    int n = 4;
    
    Service services[4] = {
        {"Emergency Treatment", 15, 1, 0, 0},
        {"Lab Test Processing", 8, 3, 0, 0},
        {"Pharmacy Billing", 5, 4, 0, 0},
        {"Doctor Consultation", 10, 2, 0, 0}
    };
    
    printf("\nHOSPITAL MANAGEMENT SERVER \n\n");

    sort_by_priority(services, n);
    calculate_times(services, n);



 

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("Fork failed");
            exit(1);
        } 
        else if (pid == 0) {
            // Print individual waiting and turnaround time before each service
            printf("Waiting Time: %d ms, Turnaround Time: %d ms\n",
                   services[i].waiting_time, services[i].turnaround_time);
            printf("Service: %s\n", services[i].name);
            printf("Process ID: %d\n", getpid());
            printf("CPU Burst Time: %d ms\n", services[i].cpu_burst);
            printf("Priority: %d\n", services[i].priority);

            pthread_t thread1, thread2;
            int t1 = 1, t2 = 2;

            pthread_create(&thread1, NULL, thread_function, &t1);
            pthread_create(&thread2, NULL, thread_function, &t2);

            pthread_join(thread1, NULL);
            pthread_join(thread2, NULL);

            printf("\n");
            exit(0);
        } 
        else {
            wait(NULL);
        }
    }

    printf("All processes completed.\n");

    // Calculate and print average waiting time and turnaround time
    float avg_waiting_time = 0, avg_turnaround_time = 0;
    for (int i = 0; i < n; i++) {
        avg_waiting_time += services[i].waiting_time;
        avg_turnaround_time += services[i].turnaround_time;
    }
    avg_waiting_time /= n;
    avg_turnaround_time /= n;

    printf("\n========================================\n");
    printf("Average Waiting Time: %.2f ms\n", avg_waiting_time);
    printf("Average Turnaround Time: %.2f ms\n", avg_turnaround_time);
    printf("========================================\n");

    return 0;
}