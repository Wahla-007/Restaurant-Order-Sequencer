#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include "restaurant.h"

int msgQueueId;
struct RestaurantStats stats;
pthread_mutex_t statsMutex;
FILE *statsFile;
FILE *logFile;
volatile int keepRunning = 1;
pthread_t refreshThread;
pid_t kitchenPid = -1;

void logActivity(const char *message) {
    logFile = fopen(LOG_FILE, "a");
    if (logFile) {
        time_t now = time(NULL);
        struct tm *timeinfo = localtime(&now);
        char timestamp[20];
        snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d]",
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        fprintf(logFile, "%s %s\n", timestamp, message);
        fclose(logFile);
    }
}

void initStats() {
    statsFile = fopen(STATS_FILE, "w");
    if (statsFile) {
        stats.orders_received = 0;
        stats.orders_completed = 0;
        stats.vip_orders = 0;
        stats.regular_orders = 0;
        stats.total_completion_time = 0;
        stats.min_time = 0;
        stats.max_time = 0;
        memset(stats.orders, 0, sizeof(stats.orders));
        fwrite(&stats, sizeof(struct RestaurantStats), 1, statsFile);
        fclose(statsFile);
    }
    
    logFile = fopen(LOG_FILE, "w");
    if (logFile) {
        fclose(logFile);
    }
}


void readStats() {
    statsFile = fopen(STATS_FILE, "r");
    if (statsFile) {
        fread(&stats, sizeof(struct RestaurantStats), 1, statsFile);
        fclose(statsFile);
    }
}


void writeStats() {
    statsFile = fopen(STATS_FILE, "w");
    if (statsFile) {
        fwrite(&stats, sizeof(struct RestaurantStats), 1, statsFile);
        fclose(statsFile);
    }
}


void clearScreen() {
    printf("\033[2J\033[H");
}

void placeOrderInteractive();
void startService();
void stopService();

const char* getOrderStatus(struct OrderTrackingData *order) {
    if (order->is_completed) {
        return "DONE";
    } else if (order->oven_lock_status == 1) {
        return "COOKING";
    } else {
        return "QUEUED";
    }
}

void displayMainInterface() {
    clearScreen();
    
    pthread_mutex_lock(&statsMutex);
    readStats();
    pthread_mutex_unlock(&statsMutex);
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║              RESTAURANT MANAGEMENT SYSTEM - LIVE VIEW                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n\n");

    printf("STATISTICS\n");
    printf("┌───────────────────────────────────────────────────────────────────────────┐\n");
    printf("│ Total: %2d | Done: %2d | Pending: %2d | VIP: %2d | Regular: %2d           │\n",
           stats.orders_received, stats.orders_completed, 
           stats.orders_received - stats.orders_completed,
           stats.vip_orders, stats.regular_orders);
    
    if (stats.orders_completed > 0) {
        long avgTime = stats.total_completion_time / stats.orders_completed;
        float completionRate = ((float)stats.orders_completed / stats.orders_received) * 100;
        printf("│ Rate: %.1f%% | Avg: %ld sec | Min: %ld | Max: %ld sec                   │\n",
               completionRate, avgTime, stats.min_time, stats.max_time);
    } else {
        printf("│ No orders completed yet                                                   │\n");
    }
    printf("└───────────────────────────────────────────────────────────────────────────┘\n\n");

    printf("\n  >> \033[1;35mVIP ORDERS\033[0m - Priority Queue\n");
    printf("  ═══════════════════════════════════════════════════════════════════════\n\n");

    int vipCount = 0;
    for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
        struct OrderTrackingData *order = &stats.orders[i];
        if (order->table_id == 0 || order->order_type != 1) continue;
        
        vipCount++;
        const char *status = getOrderStatus(order);
        long timeVal = order->is_completed ? order->completion_time : 
                       (order->oven_lock_status == 1 ? 25 : 0);
        const char *ovenStatus = order->oven_lock_status == 1 ? "LOCKED" : 
                                 order->is_completed ? "FREE" : "WAIT";

        printf("    #%-2d  Table %-3d  │  %-8s  │  %-10s  │  %3ld sec  │  Oven: %-6s\n",
               i+1, order->table_id,
               order->dish_id == 1 ? "Burger" : "Steak",
               status, timeVal, ovenStatus);
    }
    
    if (vipCount == 0) {
        printf("    ~ No VIP orders at the moment ~\n");
    }
    printf("\n");

    printf("  >> \033[1;34mREGULAR ORDERS\033[0m - Standard Queue\n");
    printf("  ═══════════════════════════════════════════════════════════════════════\n\n");

    int regCount = 0;
    for (int i = 0; i < stats.orders_received && i < MAX_ORDERS; i++) {
        struct OrderTrackingData *order = &stats.orders[i];
        if (order->table_id == 0 || order->order_type != 2) continue;
        
        regCount++;
        const char *status = getOrderStatus(order);
        long timeVal = order->is_completed ? order->completion_time : 
                       (order->oven_lock_status == 1 ? 25 : 0);
        const char *ovenStatus = order->oven_lock_status == 1 ? "LOCKED" : 
                                 order->is_completed ? "FREE" : "WAIT";

        printf("    #%-2d  Table %-3d  │  %-8s  │  %-10s  │  %3ld sec  │  Oven: %-6s\n",
               i+1, order->table_id,
               order->dish_id == 1 ? "Burger" : "Steak",
               status, timeVal, ovenStatus);
    }
    
    if (regCount == 0) {
        printf("    ~ No regular orders at the moment ~\n");
    }
    printf("\n");

    printf("  >> \033[1;33mACTIVITY LOG\033[0m - Recent Events\n");
    printf("  ═══════════════════════════════════════════════════════════════════════\n");
    
    FILE *activityLog = fopen(LOG_FILE, "r");
    if (!activityLog) {
        printf("\n    ~ No activity yet. Waiting for orders... ~\n");
    } else {
        char lines[15][300];
        int lineCount = 0;
        
        char line[300];
        while (fgets(line, sizeof(line), activityLog)) {
            line[strcspn(line, "\n")] = 0;
            
            if (lineCount >= 15) {
                for (int i = 0; i < 14; i++) {
                    strcpy(lines[i], lines[i+1]);
                }
                strcpy(lines[14], line);
            } else {
                strcpy(lines[lineCount], line);
                lineCount++;
            }
        }
        
        printf("\n");
        for (int i = 0; i < lineCount; i++) {
            printf("    %s\n", lines[i]);
        }
        
        if (lineCount == 0) {
            printf("    ~ No activity yet. Waiting for orders... ~\n");
        }
        
        fclose(activityLog);
    }
    printf("\n");


    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("  1. Place Order    2. Start Kitchen    3. Stop Service    4. Refresh    5. Exit\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
}

void* autoRefreshThread(void* arg) {
    while(keepRunning) {
        sleep(10);
    }
    return NULL;
}

void placeOrderInteractive() {
    clearScreen();
    printf("\nPLACE NEW ORDER\n");
    printf("═════════════════════════════════════════════════════════\n\n");

    int tableId, dishId, isVip;
    
    printf("Enter Table ID (1-100): ");
    scanf("%d", &tableId);
    
    if (tableId < 1 || tableId > 100) {
        printf("Invalid table ID!\n");
        sleep(2);
        return;
    }

    printf("\nSelect Dish:\n");
    printf("1. Burger ($15)\n");
    printf("2. Steak ($25)\n");
    printf("Choice (1-2): ");
    scanf("%d", &dishId);

    if (dishId < 1 || dishId > 2) {
        printf("Invalid dish selection!\n");
        sleep(2);
        return;
    }

    printf("\nSelect Priority:\n");
    printf("1. VIP (Processed First)\n");
    printf("2. Regular\n");
    printf("Choice (1-2): ");
    scanf("%d", &isVip);

    if (isVip < 1 || isVip > 2) {
        printf("Invalid priority!\n");
        sleep(2);
        return;
    }

    struct OrderMsg order;
    order.table_id = tableId;
    order.dish_id = dishId;
    order.type = (isVip == 1) ? 1 : 2;
    order.amount = 1;
    order.timestamp = time(NULL);

    if (msgsnd(msgQueueId, &order, sizeof(order) - sizeof(long), IPC_NOWAIT) == -1) {
        printf("Failed to send order. Is the kitchen running?\n");
        sleep(2);
        return;
    }

    pthread_mutex_lock(&statsMutex);
    readStats();
    
    if (stats.orders_received < MAX_ORDERS) {
        stats.orders[stats.orders_received].table_id = tableId;
        stats.orders[stats.orders_received].dish_id = dishId;
        stats.orders[stats.orders_received].order_type = (isVip == 1) ? 1 : 2;
        stats.orders[stats.orders_received].timestamp = order.timestamp;
        stats.orders[stats.orders_received].is_completed = 0;
        stats.orders[stats.orders_received].completion_time = 0;
        stats.orders[stats.orders_received].oven_lock_status = 0;
        stats.orders[stats.orders_received].delivery_time = 25;
    }
    
    stats.orders_received++;
    if (isVip == 1) stats.vip_orders++;
    else stats.regular_orders++;
    writeStats();
    pthread_mutex_unlock(&statsMutex);

    char logMsg[256];
    snprintf(logMsg, sizeof(logMsg), 
             "ORDER #%d: Table %d | %s | %s | PLACED",
             stats.orders_received,
             tableId, 
             dishId == 1 ? "Burger" : "Steak",
             (isVip == 1) ? "VIP" : "REGULAR");
    logActivity(logMsg);

    printf("\n\033[1;32mOrder placed successfully!\033[0m\n");
    printf("┌─────────────────────────────────┐\n");
    printf("│ Order #%d\n", stats.orders_received);
    printf("│ Table: %d\n", tableId);
    printf("│ Dish: %s\n", dishId == 1 ? "Burger" : "Steak");
    printf("│ Priority: %s\n", (isVip == 1) ? "VIP" : "Regular");
    printf("│ Est. Time: 25 sec\n");
    printf("└─────────────────────────────────┘\n");
    printf("\nPress Enter to return to dashboard...\n");
    getchar();
    getchar();
}

void startService() {
    clearScreen();
    printf("\n\033[1;36mSTARTING RESTAURANT SERVICE\033[0m\n");
    printf("═════════════════════════════════════════════════════════\n\n");

    key_t key = ftok(QUEUE_KEY_PATH, PROJECT_ID);
    msgQueueId = msgget(key, 0666 | IPC_CREAT);
    
    if (msgQueueId == -1) {
        printf("Failed to create message queue!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    initStats();
    logActivity("RESTAURANT OPENED - Service Started");

    printf("\033[1;32mMessage Queue created (ID: %d)\033[0m\n", msgQueueId);
    printf("\033[1;32mStatistics initialized\033[0m\n");
    
    printf("\033[1;32mStarting kitchen chefs...\033[0m\n");
    
    kitchenPid = fork();
    if (kitchenPid == 0) {
        execl("./kitchen", "./kitchen", NULL);
        printf("Failed to start kitchen. Make sure './kitchen' exists!\n");
        exit(1);
    } else if (kitchenPid > 0) {
        sleep(1);
        printf("Kitchen started (PID: %d)\n", kitchenPid);
        printf("3 chefs are now ready to cook!\n");
    } else {
        printf("Failed to fork kitchen process!\n");
    }
    
    printf("\nRestaurant is now fully operational!\n");
    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void stopService() {
    clearScreen();
    printf("\n\033[1;31mSTOPPING RESTAURANT SERVICE\033[0m\n");
    printf("═════════════════════════════════════════════════════════\n\n");

    if (kitchenPid > 0) {
        printf("Stopping kitchen chefs...\n");
        kill(kitchenPid, SIGTERM);
        waitpid(kitchenPid, NULL, 0);
        printf("Kitchen stopped\n");
        kitchenPid = -1;
    }

    if (msgctl(msgQueueId, IPC_RMID, NULL) == -1) {
        printf("Message queue not found or already removed.\n");
    } else {
        printf("Message queue destroyed\n");
    }

    logActivity("RESTAURANT CLOSED - Service Stopped");
    printf("Service stopped successfully\n");
    
    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

int main() {
    pthread_mutex_init(&statsMutex, NULL);
    
    key_t key = ftok(QUEUE_KEY_PATH, PROJECT_ID);
    msgQueueId = msgget(key, 0666);
    
    if (msgQueueId == -1) {
        printf("Kitchen not running. Please start service first (Option 2).\n");
    }

    keepRunning = 1;
    pthread_create(&refreshThread, NULL, autoRefreshThread, NULL);

    displayMainInterface();

    while(1) {
        int choice;
        scanf("%d", &choice);

        if (choice == 1) {
            placeOrderInteractive();
            displayMainInterface();
        } else if (choice == 2) {
            startService();
            displayMainInterface();
        } else if (choice == 3) {
            stopService();
            displayMainInterface();
        } else if (choice == 4) {
            displayMainInterface();
        } else if (choice == 5) {
            keepRunning = 0;
            pthread_join(refreshThread, NULL);
            
            if (kitchenPid > 0) {
                kill(kitchenPid, SIGTERM);
                waitpid(kitchenPid, NULL, 0);
            }
            
            clearScreen();
            printf("\nThank you for using Restaurant Management System!\n\n");
            

            FILE *lf = fopen(LOG_FILE, "w");
            if (lf) fclose(lf);
            
            break;
        }
    }

    pthread_mutex_destroy(&statsMutex);
    return 0;
}