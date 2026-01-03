#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include "restaurant.h"

int msgQueueId;
struct RestaurantStats stats;
pthread_mutex_t statsMutex;
FILE *logFile;
int shm_id;
struct KitchenStatus *kitchen_status;
volatile int keepRunning = 1;
pid_t kitchen_pid = -1;
pthread_t refreshThread;

void log_activity(const char *message) {
    logFile = fopen(LOG_FILE, "a");
    if (!logFile) return;
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    fprintf(logFile, "[%02d:%02d:%02d] %s\n", 
            t->tm_hour, t->tm_min, t->tm_sec, message);
    fclose(logFile);
}

void init_stats() {
    FILE *f = fopen(STATS_FILE, "w");
    if (f) {
        memset(&stats, 0, sizeof(stats));
        fwrite(&stats, sizeof(struct RestaurantStats), 1, f);
        fclose(f);
    }
    
    f = fopen(LOG_FILE, "w");
    if (f) fclose(f);
}

void load_stats() {
    FILE *f = fopen(STATS_FILE, "r");
    if (f) {
        fread(&stats, sizeof(struct RestaurantStats), 1, f);
        fclose(f);
    }
}

void save_stats() {
    FILE *f = fopen(STATS_FILE, "w");
    if (f) {
        fwrite(&stats, sizeof(struct RestaurantStats), 1, f);
        fclose(f);
    }
}

void clear_screen() {
    printf("\033[2J\033[H");
}

const char* get_status_str(const struct OrderTrackingData *o) {
    if (o->is_completed) return "\033[1;32mDONE\033[0m";
    if (o->oven_lock_status == 1) return "\033[1;33mCOOKING\033[0m";
    return "QUEUED";
}

int get_valid_input(int min, int max, const char* prompt) {
    int value;
    char buffer[100];
    
    while (1) {
        printf("%s", prompt);
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            return min; 
        }
        
        char *endptr;
        value = strtol(buffer, &endptr, 10);
        
        if (endptr == buffer || *endptr != '\n') {
            printf("\033[1;31mInvalid input. Please enter a number.\033[0m\n");
            continue;
        }
        
        if (value < min || value > max) {
            printf("\033[1;31mPlease choose between %d and %d.\033[0m\n", min, max);
            continue;
        }
        
        return value;
    }
}

void show_interface() {
    clear_screen();
    printf("\n\033[1;36mRESTAURANT MANAGEMENT SYSTEM\033[0m\n");
    printf("\033[1;36m═══════════════════════════════════════════════════════════════════════════\033[0m\n\n");

    pthread_mutex_lock(&statsMutex);
    load_stats();
    
    float rate = 0;
    if (stats.orders_received > 0) {
        rate = ((float)stats.orders_completed / stats.orders_received) * 100;
    }
    
    long avg = 0;
    if (stats.orders_completed > 0) {
        avg = stats.total_completion_time / stats.orders_completed;
    }
    pthread_mutex_unlock(&statsMutex);

    printf("  \033[1m>> STATISTICS\033[0m\n");
    printf("  \033[1;36m┌───────────────────────────────────────────────────────────────────────────┐\033[0m\n");
    if (stats.orders_received > 0) {
    printf("  \033[1;36m│\033[0m Total: %-4d        Done: \033[1;32m%-4d\033[0m        Pending: \033[1;33m%-4d\033[0m                    \033[1;36m│\033[0m\n",
           stats.orders_received, stats.orders_completed, 
           stats.orders_received - stats.orders_completed);
    printf("  \033[1;36m│\033[0m Rate: %5.1f%%       Avg: %3lds       Min/Max: %3ld/%-3ld               \033[1;36m│\033[0m\n",
               rate, avg, stats.min_time, stats.max_time);
    } else {
        printf("  \033[1;36m│\033[0m No orders yet.                                                            \033[1;36m│\033[0m\n");
    }
    printf("  \033[1;36m└───────────────────────────────────────────────────────────────────────────┘\033[0m\n\n");

    printf("  \033[1;35m>> VIP ORDERS\033[0m\n");
    printf("  \033[1;36m═══════════════════════════════════════════════════════════════════════\033[0m\n");
    
    int count = 0;
    for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
        struct OrderTrackingData *o = &stats.orders[i];
        if (o->table_id == 0 || o->order_type != 1) continue;
        
        count++;
        long t = o->is_completed ? o->completion_time : 
                 (o->oven_lock_status == 1 ? 25 : 0);
                 
        printf("    #%-2d  T%-3d  │ %-8s │ %-8s │ %3lds │ Oven: %s\n",
               i+1, o->table_id,
               o->dish_id == 1 ? "Burger" : "Steak",
               get_status_str(o), t, 
               o->oven_lock_status == 1 ? "\033[1;31mBUSY\033[0m" : "\033[1;32mFREE\033[0m");
    }
    if (!count) printf("    (empty)\n");
    printf("\n");

    printf("  \033[1;34m>> REGULAR ORDERS\033[0m\n");
    printf("  \033[1;36m═══════════════════════════════════════════════════════════════════════\033[0m\n");

    count = 0;
    for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
        struct OrderTrackingData *o = &stats.orders[i];
        if (o->table_id == 0 || o->order_type != 2) continue;
        
        count++;
        long t = o->is_completed ? o->completion_time : 
                 (o->oven_lock_status == 1 ? 25 : 0);

        printf("    #%-2d  T%-3d  │ %-8s │ %-8s │ %3lds │ Oven: %s\n",
               i+1, o->table_id,
               o->dish_id == 1 ? "Burger" : "Steak",
               get_status_str(o), t,
               o->oven_lock_status == 1 ? "\033[1;31mBUSY\033[0m" : "\033[1;32mFREE\033[0m");
    }
    if (!count) printf("    (empty)\n");
    printf("\n");

    printf("  \033[1m>> LOGS\033[0m\n");
    printf("  \033[1;36m═══════════════════════════════════════════════════════════════════════\033[0m\n");
    
    FILE *f = fopen(LOG_FILE, "r");
    if (f) {
        char lines[10][256];
        int n = 0;
        char buf[256];
        
        while (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\n")] = 0;
            if (n < 10) strcpy(lines[n++], buf);
            else {
                for(int j=0; j<9; j++) strcpy(lines[j], lines[j+1]);
                strcpy(lines[9], buf);
            }
        }
        
        for (int i=0; i<n; i++) printf("    %s\n", lines[i]);
        fclose(f);
    }
    printf("\n");
    
    printf("\033[1;36m═══════════════════════════════════════════════════════════════════════════\033[0m\n");
    printf("  1. Order    2. Refresh    3. Exit\n");
    printf("\033[1;36m═══════════════════════════════════════════════════════════════════════════\033[0m\n");
}

void* refresh_routine(void* arg) {
    while(keepRunning) sleep(10);
    return NULL;
}

void place_order() {
    clear_screen();
    printf("\n\033[1;32mNEW ORDER\033[0m\n");

    key_t shm_key = ftok(QUEUE_KEY_PATH, SHM_PROJECT_ID);
    int temp_shm = shmget(shm_key, sizeof(struct KitchenStatus), 0666);
    int is_running = 0;
    
    if (temp_shm != -1) {
        struct KitchenStatus *ks = (struct KitchenStatus*)shmat(temp_shm, NULL, 0);
        if (ks != (void*)-1) {
            is_running = ks->is_running;
            shmdt(ks);
        }
    }
    
    if (!is_running) {
        printf("\n\033[1;31mKitchen offline. Start ./kitchen first.\033[0m\n");
        sleep(2);
        return;
    }

    char id_prompt[50];
    snprintf(id_prompt, sizeof(id_prompt), "Table ID (1-%d): ", MAX_TABLES);
    int table = get_valid_input(1, MAX_TABLES, id_prompt);

    printf("\n1. Burger ($15)\n2. Steak ($25)\n");
    int dish = get_valid_input(1, 2, "Choice: ");

    printf("\n1. VIP\n2. Regular\n");
    int vip = get_valid_input(1, 2, "Choice: ");

    struct OrderMsg msg;
    msg.table_id = table;
    msg.dish_id = dish;
    msg.type = (vip == 1) ? 1 : 2;
    msg.amount = 1;
    msg.timestamp = time(NULL);

    if (msgsnd(msgQueueId, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT) == -1) {
        printf("\033[1;31mSend failed.\033[0m\n"); sleep(1); return;
    }

    pthread_mutex_lock(&statsMutex);
    load_stats();
    if (stats.orders_received < MAX_ORDERS) {
        struct OrderTrackingData *o = &stats.orders[stats.orders_received];
        o->table_id = table;
        o->dish_id = dish;
        o->order_type = (vip == 1) ? 1 : 2;
        o->timestamp = msg.timestamp;
        o->delivery_time = 25;
    }
    stats.orders_received++;
    if (vip == 1) stats.vip_orders++; else stats.regular_orders++;
    save_stats();
    pthread_mutex_unlock(&statsMutex);

    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf), "ORDER #%d: T%d | %s | %s",
             stats.orders_received, table,
             (dish==1?"Burger":"Steak"), (vip==1?"VIP":"REG"));
    log_activity(logbuf);

    printf("\n\033[1;32mOrder placed.\033[0m\n");
    sleep(1);
}

int main() {
    pthread_mutex_init(&statsMutex, NULL);
    
    key_t key = ftok(QUEUE_KEY_PATH, PROJECT_ID);
    msgQueueId = msgget(key, 0666 | IPC_CREAT);
    
    if (msgQueueId == -1) {
        perror("msgget failed");
        return 1;
    }

    
    kitchen_pid = fork();
    if (kitchen_pid == -1) {
        perror("fork failed");
        return 1;
    } else if (kitchen_pid == 0) {
        
        execl("./kitchen", "kitchen", NULL);
        perror("exec failed");
        exit(1);
    }
    
    sleep(1);

    init_stats();
    log_activity("SERVICE STARTED");

    pthread_create(&refreshThread, NULL, refresh_routine, NULL);
    show_interface();

    while(1) {
        int choice = get_valid_input(1, 3, "\nSelect Option: ");

        if (choice == 1) {
            place_order();
            show_interface();
        } else if (choice == 2) {
            show_interface();
        } else if (choice == 3) {
            keepRunning = 0;
            pthread_cancel(refreshThread);
            pthread_join(refreshThread, NULL);
            
            // Terminate kitchen child
            if (kitchen_pid > 0) {
                kill(kitchen_pid, SIGTERM);
                waitpid(kitchen_pid, NULL, 0);
            }

            msgctl(msgQueueId, IPC_RMID, NULL);
            log_activity("SERVICE STOPPED");

            clear_screen();
            printf("\nBye!\n\n");
            break;
        }
    }

    pthread_mutex_destroy(&statsMutex);
    return 0;
}