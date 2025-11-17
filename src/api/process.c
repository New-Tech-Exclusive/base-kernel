/*
 * Enhanced Process Management API Implementation
 * Provides developer-friendly process creation and management
 */

#include "kernel.h"

// ============================================================================
// PROCESS INFORMATION
// ============================================================================

typedef struct {
    pid_t pid;
    const char* name;
    process_state_t state;
    size_t stack_size;
    int priority;
    uint64_t creation_time;
    uint64_t cpu_time;
    size_t memory_used;
} process_info_t;

// High-level process creation
process_t process_create(process_entry_t entry, void* arg, process_attr_t* attr) {
    if (!entry) return -1;

    // Use default attributes if none provided
    process_attr_t default_attr = PROCESS_ATTR_DEFAULT;
    if (!attr) {
        attr = &default_attr;
    }

    // Validate parameters
    if (attr->stack_size < 1024) {
        attr->stack_size = 1024;  // Minimum stack
    }
    if (attr->priority < 0) attr->priority = 0;
    if (attr->priority > 255) attr->priority = 255;

    // Create the process using scheduler
    pid_t pid = scheduler_create_task(entry, arg, attr->stack_size, attr->priority, attr->name);
    if (pid <= 0) {
        return -1;
    }

    return pid;
}

process_t process_create_simple(process_entry_t entry, void* arg) {
    return process_create(entry, arg, NULL);
}

int process_wait(process_t pid) {
    if (pid <= 0) return -1;

    // Wait for process to complete (simplified implementation)
    while (scheduler_get_task_state(pid) != TASK_TERMINATED) {
        schedule_delay(10);  // Brief polling
    }

    return 0;
}

int process_kill(process_t pid) {
    if (pid <= 0) return -1;

    return scheduler_kill_task(pid);
}

int process_get_info(process_t pid, process_info_t* info) {
    if (!info || pid <= 0) return -1;

    // Get information from scheduler
    memset(info, 0, sizeof(process_info_t));

    scheduler_task_info_t sched_info;
    if (scheduler_get_task_info(pid, &sched_info) != 0) {
        return -1;
    }

    // Fill in our process_info structure
    info->pid = sched_info.pid;
    info->name = sched_info.name ? sched_info.name : "unnamed";
    info->state = sched_info.state;
    info->stack_size = sched_info.stack_size;
    info->priority = sched_info.priority;
    info->creation_time = sched_info.creation_time_ms;
    info->cpu_time = sched_info.cpu_time_ms;

    // Approximate memory usage (this would need better tracking)
    info->memory_used = sched_info.stack_size;

    return 0;
}

// ============================================================================
// PROCESS GROUPS
// ============================================================================

typedef struct process_group {
    pgid_t pgid;
    pid_t processes[64];  // Max processes per group
    size_t process_count;
    struct process_group* next;
} process_group_t;

static process_group_t* process_groups = NULL;
static pgid_t next_pgid = 1;

pgid_t process_group_create(void) {
    process_group_t* group = kmalloc_tracked(sizeof(process_group_t), "process_group");
    if (!group) return -1;

    group->pgid = next_pgid++;
    group->process_count = 0;
    memset(group->processes, 0, sizeof(group->processes));
    group->next = process_groups;
    process_groups = group;

    return group->pgid;
}

int process_group_join(pgid_t pgid) {
    if (pgid <= 0) return -1;

    // Find the process group
    process_group_t* group = process_groups;
    while (group) {
        if (group->pgid == pgid) {
            break;
        }
        group = group->next;
    }

    if (!group) return -1; // Group not found
    if (group->process_count >= ARRAY_SIZE(group->processes)) {
        return -1; // Group full
    }

    pid_t current_pid = scheduler_get_current_task_id();

    // Check if process is already in group
    for (size_t i = 0; i < group->process_count; i++) {
        if (group->processes[i] == current_pid) {
            return 0; // Already a member
        }
    }

    group->processes[group->process_count++] = current_pid;
    return 0;
}

int process_group_kill(pgid_t pgid) {
    if (pgid <= 0) return -1;

    // Find the process group
    process_group_t* group = process_groups;
    while (group) {
        if (group->pgid == pgid) {
            break;
        }
        group = group->next;
    }

    if (!group) return -1;

    int killed_count = 0;

    // Kill all processes in group
    for (size_t i = 0; i < group->process_count; i++) {
        pid_t pid = group->processes[i];
        if (pid > 0 && scheduler_get_task_state(pid) != TASK_TERMINATED) {
            if (scheduler_kill_task(pid) == 0) {
                killed_count++;
            }
        }
    }

    // Clean up group
    process_group_remove(pgid);

    return killed_count;
}

static void process_group_remove(pgid_t pgid) {
    process_group_t** current = &process_groups;
    while (*current) {
        if ((*current)->pgid == pgid) {
            process_group_t* to_free = *current;
            *current = (*current)->next;
            kfree_tracked(to_free);
            break;
        }
        current = &(*current)->next;
    }
}

// ============================================================================
// ENVIRONMENT VARIABLES
// ============================================================================

typedef struct env_var {
    char* key;
    char* value;
    struct env_var* next;
} env_var_t;

static env_var_t* env_vars = NULL;

int process_set_env(const char* key, const char* value) {
    if (!key) return -1;

    // Remove existing variable if any
    process_unset_env(key);

    // Create new variable
    env_var_t* var = kmalloc_tracked(sizeof(env_var_t), "env_var");
    if (!var) return -1;

    var->key = str_duplicate(key);
    var->value = value ? str_duplicate(value) : str_duplicate("");

    if (!var->key || !var->value) {
        kfree_tracked(var);
        return -1;
    }

    var->next = env_vars;
    env_vars = var;

    return 0;
}

const char* process_get_env(const char* key) {
    if (!key) return NULL;

    env_var_t* current = env_vars;
    while (current) {
        if (str_compare(current->key, (char*)key) == 0) {
            return current->value;
        }
        current = current->next;
    }

    return NULL;
}

static int process_unset_env(const char* key) {
    if (!key) return -1;

    env_var_t** current = &env_vars;
    while (*current) {
        if (str_compare((*current)->key, (char*)key) == 0) {
            env_var_t* to_free = *current;
            *current = (*current)->next;

            kfree_tracked(to_free->key);
            kfree_tracked(to_free->value);
            kfree_tracked(to_free);
            return 0;
        }
        current = &(*current)->next;
    }

    return -1; // Not found
}
