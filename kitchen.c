#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include "restaurant.h"

// --- GLOBAL SHARED RESOURCES ---
int msg_queue_id;
pthread_mutex_t oven_mutex;
sem_t ingredients;
pthread_mutex_t stats_file_mutex;

// Function to log activity
void log_activity(const char *message) {
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        time_t now = time(NULL);
        struct tm *timeinfo = localtime(&now);
        char timestamp[20];
        snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d]",
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        fprintf(log_file, "%s %s\n", timestamp, message);
        fclose(log_file);
    }
}

// Function to update order status in stats
void update_order_status(int table_id, const char *status) {
    pthread_mutex_lock(&stats_file_mutex);
    
    FILE *stats_file = fopen(STATS_FILE, "r+");
    if (stats_file) {
        struct RestaurantStats stats;
        fread(&stats, sizeof(struct RestaurantStats), 1, stats_file);
        
        // Find the order and update status
        for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
            if (stats.orders[i].table_id == table_id && !stats.orders[i].is_completed) {
                // Store status in oven_lock_status field (repurposed as cooking status)
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

// Function to update completed order in stats
void update_order_completion(int table_id, long completion_time) {
    pthread_mutex_lock(&stats_file_mutex);
    
    FILE *stats_file = fopen(STATS_FILE, "r+");
    if (stats_file) {
        struct RestaurantStats stats;
        fread(&stats, sizeof(struct RestaurantStats), 1, stats_file);
        
        // Find and mark the order as completed
        for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
            if (stats.orders[i].table_id == table_id && !stats.orders[i].is_completed) {
                stats.orders[i].is_completed = 1;
                stats.orders[i].completion_time = completion_time;
                stats.orders[i].oven_lock_status = 2; // 2 = completed
                break;
            }
        }
        
        stats.orders_completed++;
        stats.total_completion_time += completion_time;
        
        // Fix min/max time logic
        if (stats.orders_completed == 1) {
            // First order sets both min and max
            stats.min_time = completion_time;
            stats.max_time = completion_time;
        } else {
            // Subsequent orders update min/max
            if (completion_time < stats.min_time) stats.min_time = completion_time;
            if (completion_time > stats.max_time) stats.max_time = completion_time;
        }
        
        rewind(stats_file);
        fwrite(&stats, sizeof(struct RestaurantStats), 1, stats_file);
        fclose(stats_file);
    }
    
    pthread_mutex_unlock(&stats_file_mutex);
}

// --- CHEF THREAD (The Consumer) ---
void* chef_routine(void* arg) {
    int id = *(int*)arg;
    struct OrderMsg received_order;
    char* receipt;

    while(1) {
        // 1. Wait for Order (Priority Scheduling)
        // Use msgtyp = -2 to get priority messages: type 1 (VIP) before type 2 (Regular)
        // This means: get messages with type <= 2, but prioritize lowest type numbers first
        ssize_t result = msgrcv(msg_queue_id, &received_order, sizeof(received_order) - sizeof(long), -2, 0);
        
        if (result == -1) {
            // If queue is removed or error, exit gracefully
            if (errno == EIDRM || errno == EINVAL) {
                printf("\033[1;31m[Chef %d]\033[0m Queue removed, shutting down...\n", id);
                break;
            }
            continue; // Other errors, try again
        }

        // 2. Check Ingredients (Semaphore Wait)
        sem_wait(&ingredients);
        
        char order_type_str[20];
        snprintf(order_type_str, sizeof(order_type_str), "%s", 
                 received_order.type == 1 ? "VIP" : "REGULAR");
        
        char log_msg[256];
        
        // Log order received
        snprintf(log_msg, sizeof(log_msg),
                 "CHEF #%d RECEIVED: Table %d | %s | %s (Priority: %ld)",
                 id, received_order.table_id,
                 received_order.dish_id == 1 ? "Burger" : "Steak",
                 order_type_str, received_order.type);
        log_activity(log_msg);
        
        // Update status to cooking
        update_order_status(received_order.table_id, "COOKING");
        
        printf("\033[1;33m[Chef %d]\033[0m Preparing %s for Table %d (\033[1;35m%s\033[0m)...\n", 
               id, (received_order.dish_id==1?"Burger":"Steak"), 
               received_order.table_id, (received_order.type==1?"VIP":"Regular"));

        // 3. Critical Section: Use Oven (Mutex Lock)
        snprintf(log_msg, sizeof(log_msg),
                 "OVEN LOCKED: Chef #%d cooking Table %d order", 
                 id, received_order.table_id);
        log_activity(log_msg);
        
        pthread_mutex_lock(&oven_mutex);
            long cooking_start = time(NULL);
            sleep(25); // Simulate cooking time
            long cooking_end = time(NULL);
        pthread_mutex_unlock(&oven_mutex);
        
        snprintf(log_msg, sizeof(log_msg),
                 "OVEN FREE: Chef #%d finished Table %d", 
                 id, received_order.table_id);
        log_activity(log_msg);

        // 4. Memory Management (Dynamic Allocation)
        long completion_time = cooking_end - cooking_start;
        
        receipt = (char*)malloc(100 * sizeof(char));
        sprintf(receipt, "\033[1;36m--- BILL: Table %d | Cost: $20 | Time: %ld seconds ---\033[0m\n", 
                received_order.table_id, completion_time);
        printf("%s", receipt);
        free(receipt);
        
        // Update stats with completion info
        update_order_completion(received_order.table_id, completion_time);
        
        snprintf(log_msg, sizeof(log_msg),
                 "COMPLETED: Table %d | %s | %s | Time: %ld sec",
                 received_order.table_id,
                 received_order.dish_id == 1 ? "Burger" : "Steak",
                 order_type_str, completion_time);
        log_activity(log_msg);
        
        printf("\033[1;32m[Chef %d]\033[0m Order Complete for Table %d (Took %ld seconds)\n", 
               id, received_order.table_id, completion_time);
        
        // Release ingredient
        sem_post(&ingredients);
    }
    return NULL;
}

// --- MAIN FUNCTION ---
int main() {
    printf("\033[1;36m=== STARTING KITCHEN SERVICE ===\033[0m\n");
    
    // A. Setup IPC
    key_t key = ftok(QUEUE_KEY_PATH, PROJECT_ID);
    msg_queue_id = msgget(key, 0666);
    
    if (msg_queue_id == -1) {
        printf("Error: Message queue not found. Start the restaurant interface first!\n");
        return 1;
    }
    
    // B. Initialize Synchronization Tools
    pthread_mutex_init(&oven_mutex, NULL);
    pthread_mutex_init(&stats_file_mutex, NULL);
    sem_init(&ingredients, 0, 8);

    log_activity("KITCHEN OPENED - Chefs ready");

    printf("\033[1;32mConnected to message queue\033[0m\n");
    printf("\033[1;32mInitialized synchronization tools\033[0m\n");
    printf("\033[1;32mKitchen is ready to process orders\033[0m\n");
    printf("\033[1;36m=== KITCHEN RUNNING - Press Ctrl+C to stop ===\033[0m\n\n");

    // C. Create Chef Threads
    pthread_t chefs[NUM_CHEFS];
    int chef_ids[NUM_CHEFS];
    for(int i=0; i<NUM_CHEFS; i++) {
        chef_ids[i] = i+1;
        pthread_create(&chefs[i], NULL, chef_routine, &chef_ids[i]);
        printf("Chef #%d is ready\n", i+1);
    }

    // D. Wait for all chef threads (they run until interrupted)
    for(int i=0; i<NUM_CHEFS; i++) {
        pthread_join(chefs[i], NULL);
    }

    // E. Cleanup
    printf("\n=== KITCHEN CLOSING ===\n");
    log_activity("KITCHEN CLOSED");
    pthread_mutex_destroy(&oven_mutex);
    pthread_mutex_destroy(&stats_file_mutex);
    sem_destroy(&ingredients);
    
    return 0;
}