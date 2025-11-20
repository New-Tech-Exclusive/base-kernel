/*
 * Enhanced Event System API Implementation
 * Provides high-level event handling with queues and loops
 */

#include "kernel.h"
#include "api.h"

// ============================================================================
// EVENT QUEUE IMPLEMENTATION
// ============================================================================

typedef struct event_listeners {
    pid_t process_id;
    event_queue_t* queue;
    struct event_listeners* next;
} event_listener_t;

struct event_queue {
    pid_t owner_pid;
    event_t* events;          // Circular buffer
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    event_listener_t* listeners;  // For broadcasting
    struct event_queue* next_global; // For global queue list
};

static event_queue_t* global_queues = NULL;

// Event queue management
event_queue_t* event_queue_create(void) {
    event_queue_t* queue = kmalloc_tracked(sizeof(event_queue_t), "event_queue");
    if (!queue) return NULL;

    queue->owner_pid = scheduler_get_current_task_id();
    queue->capacity = 256;  // Default capacity
    queue->events = kmalloc_tracked(sizeof(event_t) * queue->capacity, "event_queue_buffer");
    if (!queue->events) {
        kfree_tracked(queue);
        return NULL;
    }

    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->listeners = NULL;

    // Add to global list for broadcasting
    queue->next_global = global_queues;
    global_queues = queue;

    return queue;
}

void event_queue_destroy(event_queue_t* queue) {
    if (!queue) return;

    // Remove from global list
    event_queue_t** current = &global_queues;
    while (*current) {
        if (*current == queue) {
            *current = queue->next_global;
            break;
        }
        current = &(*current)->next_global;
    }

    // Clean up listeners
    event_listener_t* listener = queue->listeners;
    while (listener) {
        event_listener_t* next = listener->next;
        kfree_tracked(listener);
        listener = next;
    }

    if (queue->events) {
        kfree_tracked(queue->events);
    }
    kfree_tracked(queue);
}

static bool event_queue_resize(event_queue_t* queue, size_t new_capacity) {
    event_t* new_events = kmalloc_tracked(sizeof(event_t) * new_capacity, "event_queue_resize");
    if (!new_events) return false;

    // Copy existing events
    size_t copy_count = 0;
    size_t index = queue->head;
    while (copy_count < queue->count && copy_count < new_capacity) {
        new_events[copy_count] = queue->events[index];
        index = (index + 1) % queue->capacity;
        copy_count++;
    }

    kfree_tracked(queue->events);
    queue->events = new_events;
    queue->capacity = new_capacity;
    queue->head = 0;
    queue->tail = copy_count;
    queue->count = copy_count;

    return true;
}

bool event_queue_poll(event_queue_t* queue, event_t* event) {
    if (!queue || !event) return false;

    if (queue->count == 0) return false;

    // Copy event to user buffer
    *event = queue->events[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    return true;
}

bool event_queue_wait(event_queue_t* queue, event_t* event, uint32_t timeout_ms) {
    if (!queue || !event) return false;

    uint64_t start_time = time_monotonic_ms();
    uint64_t timeout_point = start_time + timeout_ms;

    // Poll until event available or timeout
    while (queue->count == 0) {
        schedule_delay(1);  // Brief delay

        if (time_monotonic_ms() >= timeout_point) {
            return false;  // Timeout
        }
    }

    return event_queue_poll(queue, event);
}

bool event_queue_push(event_queue_t* queue, const event_t* event) {
    if (!queue || !event) return false;

    // Resize if necessary (simple growth strategy)
    if (queue->count >= queue->capacity - 1) {
        size_t new_capacity = queue->capacity * 2;
        if (!event_queue_resize(queue, new_capacity)) {
            return false;
        }
    }

    // Add event to queue
    queue->events[queue->tail] = *event;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    return true;
}

// ============================================================================
// GLOBAL EVENT HANDLING
// ============================================================================

void event_send_global(const event_t* event) {
    if (!event) return;

    // Send to all event queues
    event_queue_t* queue = global_queues;
    while (queue) {
        event_queue_push(queue, event);
        queue = queue->next_global;
    }
}

void event_send_to_process(pid_t pid, const event_t* event) {
    if (!event || pid <= 0) return;

    // Find all queues owned by this process
    event_queue_t* queue = global_queues;
    while (queue) {
        if (queue->owner_pid == pid) {
            event_queue_push(queue, event);
            // Continue searching for other queues belonging to this process
        }
        queue = queue->next_global;
    }
}

void event_send_to_window(window_id_t window, const event_t* event) {
    if (!event || window == 0) return;

    // Find owning process and send event
    pid_t owner_pid = 0;

    // Search through window manager
    extern wm_window_t wm_windows[];  // From display server


    for (int i = 0; i < MAX_WM_WINDOWS; i++) {
        if (wm_windows[i].window_id == window) {
            owner_pid = wm_windows[i].owner_pid;
            break;
        }
    }

    if (owner_pid > 0) {
        event_send_to_process(owner_pid, event);
    }
}

// ============================================================================
// EVENT LOOP
// ============================================================================

static bool event_loop_running = false;
static event_handler_t current_handler = NULL;
static void* current_user_data = NULL;

void event_loop_run(event_handler_t handler, void* user_data) {
    if (!handler || event_loop_running) return;

    current_handler = handler;
    current_user_data = user_data;
    event_loop_running = true;

    // Create default event queue for this process if none exists
    static event_queue_t* default_queue = NULL;
    if (!default_queue) {
        default_queue = event_queue_create();
    }

    KINFO("Starting event loop for process %d", scheduler_get_current_task_id());

    event_t event;
    while (event_loop_running) {
        // Poll for events
        if (event_queue_poll(default_queue, &event)) {
            // Handle the event
            bool continue_loop = handler(&event, user_data);
            if (!continue_loop) {
                break;
            }
        }

        // Yield CPU to other processes
        scheduler_yield();

        // Prevent busy waiting
        schedule_delay(1);
    }

    KINFO("Event loop exited for process %d", scheduler_get_current_task_id());
}

void event_loop_quit(void) {
    event_loop_running = false;
}

// ============================================================================
// SYSTEM EVENT GENERATION
// ============================================================================

// Convert low-level input to high-level events
void event_from_keyboard(uint32_t keycode, uint32_t modifiers, uint32_t state) {
    event_t event = {
        .type = EVENT_TYPE_KEYBOARD,
        .timestamp = time_monotonic_ms(),
        .data.keyboard = {
            .keycode = keycode,
            .modifiers = modifiers,
            .state = state
        }
    };

    event_send_global(&event);
}

void event_from_mouse(int32_t x, int32_t y, uint32_t buttons, int32_t wheel) {
    event_t event = {
        .type = EVENT_TYPE_MOUSE,
        .timestamp = time_monotonic_ms(),
        .data.mouse = {
            .x = x,
            .y = y,
            .buttons = buttons,
            .wheel = wheel
        }
    };

    event_send_global(&event);
}

void event_from_window(window_id_t window_id, int action) {
    event_t event = {
        .type = EVENT_TYPE_WINDOW,
        .timestamp = time_monotonic_ms()
        // Window events would need additional fields in kernel.h event_t
    };
    (void)window_id;  // Unused for now
    (void)action;     // Unused for now

    event_send_to_window(window_id, &event);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void log_message(log_level_t level, const char* format, ...) {
    // Would integrate with logging system
    // For now, just print to serial
    (void)level;

    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Write each character
    for (size_t i = 0; buffer[i]; i++) {
        serial_write(buffer[i]);
    }
}

void log_set_level(log_level_t level) {
    // Would set minimum log level
    (void)level;
}

// ============================================================================
// SYSTEM TIME UTILITIES
// ============================================================================

uint64_t time_realtime_ms(void) {
    // Would be different from monotonic if RTC supported
    return time_monotonic_ms();
}

void sleep_ms(uint32_t milliseconds) {
    schedule_delay(milliseconds);
}

void sleep_us(uint32_t microseconds) {
    // Convert to ms, minimum 1ms granularity
    uint32_t ms = (microseconds + 999) / 1000;  // Round up
    schedule_delay(ms);
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

static uint32_t rand_state = 0xDEADBEEF;

uint32_t random_uint32(void) {
    // Simple LCG: Xn+1 = (a*Xn + c) mod m
    rand_state = rand_state * 1103515245 + 12345;
    return rand_state;
}

void random_bytes(void* buffer, size_t size) {
    uint8_t* bytes = (uint8_t*)buffer;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = random_uint32() & 0xFF;
    }
}

// ============================================================================
// STRING UTILITIES
// ============================================================================

size_t str_copy(char* dst, const char* src, size_t max_len) {
    if (!dst || !src || max_len == 0) return 0;

    size_t i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';

    return i;
}

int str_compare(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    while (*s1 && *s2) {
        if (*s1 != *s2) {
            return *s1 - *s2;
        }
        s1++;
        s2++;
    }

    return *s1 - *s2;
}

size_t str_length(const char* str) {
    if (!str) return 0;

    size_t len = 0;
    while (str[len]) len++;
    return len;
}

char* str_duplicate(const char* str) {
    if (!str) return NULL;

    size_t len = str_length(str);
    char* copy = kmalloc_tracked(len + 1, "string_duplicate");
    if (!copy) return NULL;

    str_copy(copy, str, len + 1);
    return copy;
}
