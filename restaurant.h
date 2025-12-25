// restaurant.h
#ifndef RESTAURANT_H
#define RESTAURANT_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

// Defines for IPC
#define QUEUE_KEY_PATH "/tmp" 
#define PROJECT_ID 'R'

// Thread and Process Configuration
#define NUM_CHEFS 3
#define NUM_CUSTOMERS 5

// The Order Ticket (Message Queue Buffer)
struct OrderMsg {
    long type;      // 1=VIP, 2=Regular (For Priority Scheduling)
    int table_id;
    int dish_id;    // 1=Burger, 2=Steak
    int amount;
    long timestamp; // Order creation time
};

// Order Tracking (for completion time)
struct OrderTrack {
    int table_id;
    long start_time;
    long end_time;
    int dish_id;
    int order_type;
};

// Statistics Tracking
#define STATS_FILE "/tmp/restaurant_stats.dat"
#define ORDERS_LOG_FILE "/tmp/restaurant_orders.log"
#define LOG_FILE "/tmp/restaurant_activity.log"
#define MAX_ORDERS 100

struct OrderTrackingData {
    int table_id;
    int dish_id;
    int order_type;      // 1=VIP, 2=Regular
    long timestamp;
    long completion_time;
    int is_completed;
    int oven_lock_status; // 0=not locked, 1=locked by chef
    long delivery_time;   // estimated time to deliver
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

#endif