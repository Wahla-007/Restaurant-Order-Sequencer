#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/shm.h>
#include "restaurant.h"

int msg_queue_id;
pthread_mutex_t oven_mutex;
sem_t ingredients;
pthread_mutex_t stats_file_mutex;
int shm_id;
struct KitchenStatus *kitchen_status;

void log_activity(const char *message) {
    FILE *log_file = fopen(LOG_FILE, "a");
    if (!log_file) return;

    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    
    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d]",
            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
            
    fprintf(log_file, "%s %s\n", timestamp, message);
    fclose(log_file);
}

void cleanup_handler(int sig) {
    if (kitchen_status) {
        kitchen_status->is_running = 0;
        shmdt(kitchen_status);
    }
    
    printf("\n=== KITCHEN CLOSED ===\n");
    log_activity("KITCHEN CLOSED");
    
    pthread_mutex_destroy(&oven_mutex);
    pthread_mutex_destroy(&stats_file_mutex);
    sem_destroy(&ingredients);
    exit(0);
}

void update_order_status(int table_id, const char *status) {
    pthread_mutex_lock(&stats_file_mutex);
    
    FILE *stats_file = fopen(STATS_FILE, "r+");
    if (stats_file) {
        struct RestaurantStats stats;
        fread(&stats, sizeof(struct RestaurantStats), 1, stats_file);
        
        for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
            if (stats.orders[i].table_id == table_id && !stats.orders[i].is_completed) {
                if (strcmp(status, "COOKING") == 0) {
                    stats.orders[i].oven_lock_status = 1;
                }
                break;
            }
        }
        
        rewind(stats_file);
        fwrite(&stats, sizeof(struct RestaurantStats), 1, stats_file);
        fclose(stats_file);
    }
    
    pthread_mutex_unlock(&stats_file_mutex);
}

void update_completion_stats(int table_id, long completion_time) {
    pthread_mutex_lock(&stats_file_mutex);
    
    FILE *stats_file = fopen(STATS_FILE, "r+");
    if (stats_file) {
        struct RestaurantStats stats;
        fread(&stats, sizeof(struct RestaurantStats), 1, stats_file);
        
        for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
            if (stats.orders[i].table_id == table_id && !stats.orders[i].is_completed) {
                stats.orders[i].is_completed = 1;
                stats.orders[i].completion_time = completion_time;
                stats.orders[i].oven_lock_status = 2; 
                break;
            }
        }
        
        stats.orders_completed++;
        stats.total_completion_time += completion_time;
        
        if (stats.orders_completed == 1) {
            stats.min_time = completion_time;
            stats.max_time = completion_time;
        } else {
            if (completion_time < stats.min_time) stats.min_time = completion_time;
            if (completion_time > stats.max_time) stats.max_time = completion_time;
        }
        
        rewind(stats_file);
        fwrite(&stats, sizeof(struct RestaurantStats), 1, stats_file);
        fclose(stats_file);
    }
    
    pthread_mutex_unlock(&stats_file_mutex);
}

void* chef_routine(void* arg) {
    int id = *(int*)arg;
    struct OrderMsg order;
    char log_buf[256];

    while(1) {
        if (msgrcv(msg_queue_id, &order, sizeof(order) - sizeof(long), -2, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) break;
            continue;
        }

        sem_wait(&ingredients);
        
        const char* type_str = (order.type == 1) ? "VIP" : "REGULAR";
        const char* dish_str = (order.dish_id == 1) ? "Burger" : "Steak";
        
        snprintf(log_buf, sizeof(log_buf), "CHEF #%d: Table %d | %s | %s", 
                 id, order.table_id, dish_str, type_str);
        log_activity(log_buf);
        
        update_order_status(order.table_id, "COOKING");
        printf("\033[1;33m[Chef %d]\033[0m Cooking %s for Table %d (%s)...\n", 
               id, dish_str, order.table_id, type_str);

        pthread_mutex_lock(&oven_mutex);
        long start = time(NULL);
        sleep(25); 
        long end = time(NULL);
        pthread_mutex_unlock(&oven_mutex);
        
        long duration = end - start;

        printf("\033[1;36m--- BILL: Table %d | Cost: $20 | Time: %lds ---\033[0m\n", 
               order.table_id, duration);
        
        update_completion_stats(order.table_id, duration);
        
        snprintf(log_buf, sizeof(log_buf), "DONE: Table %d | %s | %s | %lds",
                 order.table_id, dish_str, type_str, duration);
        log_activity(log_buf);
        
        printf("\033[1;32m[Chef %d]\033[0m Finished Table %d\n", id, order.table_id);
        
        sem_post(&ingredients);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, cleanup_handler);

    printf("\033[1;36m=== KITCHEN SERVICE ===\033[0m\n");

    key_t key = ftok(QUEUE_KEY_PATH, PROJECT_ID);
    msg_queue_id = msgget(key, 0666);

    if (msg_queue_id == -1) {
        printf("Error: Interface not running.\n");
        return 1;
    }

    key_t shm_key = ftok(QUEUE_KEY_PATH, SHM_PROJECT_ID);
    shm_id = shmget(shm_key, sizeof(struct KitchenStatus), 0666 | IPC_CREAT);
    if (shm_id == -1) {
        perror("shmget failed");
        return 1;
    }
    
    kitchen_status = (struct KitchenStatus*)shmat(shm_id, NULL, 0);
    if (kitchen_status == (void*)-1) {
        perror("shmat failed");
        return 1;
    }
    kitchen_status->is_running = 1;

    pthread_mutex_init(&oven_mutex, NULL);
    pthread_mutex_init(&stats_file_mutex, NULL);
    sem_init(&ingredients, 0, 8);

    log_activity("KITCHEN OPENED");

    if (argc == 2) {
        int id = atoi(argv[1]);
        printf("Chef #%d active.\n", id);
        chef_routine(&id);
    } else {
        printf("Ready for orders.\nUse Ctrl+C to stop.\n\n");
        pthread_t chefs[NUM_CHEFS];
        int ids[NUM_CHEFS];
        for(int i=0; i<NUM_CHEFS; i++) {
            ids[i] = i+1;
            pthread_create(&chefs[i], NULL, chef_routine, &ids[i]);
            printf("Chef #%d ready\n", i+1);
        }
        for(int i=0; i<NUM_CHEFS; i++) pthread_join(chefs[i], NULL);
    }

    cleanup_handler(0);
    return 0;
}