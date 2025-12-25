#ifndef RESTAURANT_H
#define RESTAURANT_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#define QUEUE_KEY_PATH "/tmp" 
#define PROJECT_ID 'R'
#define SHM_PROJECT_ID 'K'

#define NUM_CHEFS 3
#define NUM_CUSTOMERS 5
#define MAX_TABLES 15

#define STATS_FILE "/tmp/restaurant_stats.dat"
#define ORDERS_LOG_FILE "/tmp/restaurant_orders.log"
#define LOG_FILE "/tmp/restaurant_activity.log"
#define MAX_ORDERS 100

struct OrderMsg {
    long type;
    int table_id;
    int dish_id;    
    int amount;
    long timestamp; 
};

struct OrderTrack {
    int table_id;
    long start_time;
    long end_time;
    int dish_id;
    int order_type;
};

struct OrderTrackingData {
    int table_id;
    int dish_id;
    int order_type;
    long timestamp;
    long completion_time;
    int is_completed;
    int oven_lock_status; 
    long delivery_time;
};

struct RestaurantStats {
    int orders_received;
    int orders_completed;
    int vip_orders;
    int regular_orders;
    long total_completion_time;
    long min_time;
    long max_time;
    struct OrderTrackingData orders[MAX_ORDERS];
};

struct KitchenStatus {
    int is_running;
};

#endif