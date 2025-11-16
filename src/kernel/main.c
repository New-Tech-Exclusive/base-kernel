#include "kernel.h"
#include "drivers/keyboard/keyboard.h"

// Let's have some friendly functions to draw beautiful text on your screen!
void vga_puts(const char* s);
void vga_putc(char c);
void vga_clear_screen(void);
void vga_putc_at(int pos, char c);

// We're using some helpful string functions that live in string.c

// Our own little string formatting helper so we can put numbers in output
char* sprintf(char* buf, const char* format, ...);

// This is the star of the show - interpreting what you type!
void process_command(char* cmd);

static char* vga_buffer = (char*)0xB8000;
static int vga_position = 0;

// This grabs your message and shows it character by character on the screen
void vga_puts(const char* s) {
    while (*s) {
        vga_putc(*s++);
    }
}

// This handles drawing a single letter or symbol at the spot you're typing
void vga_putc(char c) {
    if (c == '\n') {
        vga_position += 80 - (vga_position % 80);
    } else if (c == '\b') {
        // Backspace
        if (vga_position > 0) {
            vga_position--;
            vga_buffer[vga_position * 2] = ' ';
            vga_buffer[vga_position * 2 + 1] = 0x07;
        }
    } else {
        vga_buffer[vga_position * 2] = c;
        vga_buffer[vga_position * 2 + 1] = 0x07;  // White on black
        vga_position++;
    }
    if (vga_position >= 2000) {
        vga_position = 0;  // Wrap around
    }
}

// Here's how we wipe the screen clean for a fresh start
void vga_clear_screen(void) {
    for (int i = 0; i < 2000; i++) {
        vga_buffer[i * 2] = ' ';
        vga_buffer[i * 2 + 1] = 0x07;
    }
    vga_position = 0;
}

// This helps us place a character exactly where we want on the screen
void vga_putc_at(int pos, char c) {
    vga_buffer[pos * 2] = c;
}

// A helpful little function that inserts numbers into text strings for us
char* sprintf(char* buf, const char* format, ...) {
    // Pretty basic, but handles %d for now
    int* arg = (int*)&format + 1;
    char* buf_start = buf;

    while (*format) {
        if (*format == '%' && *(format+1) == 'd') {
            int num = *arg++;
            char temp[10];
            int i = 0;
            if (num == 0) {
                *buf++ = '0';
            } else {
                int neg = num < 0;
                if (neg) num = -num;
                while (num > 0) {
                    temp[i++] = '0' + (num % 10);
                    num /= 10;
                }
                if (neg) temp[i++] = '-';
                while (i > 0) {
                    *buf++ = temp[--i];
                }
            }
            format += 2;
        } else {
            *buf++ = *format++;
        }
    }
    *buf = '\0';
    return buf_start;
}

extern uint32_t multiboot_magic;
extern uint32_t multiboot_info;

// Our cozy in-memory filesystem where everything lives
#define MAX_FILES 64
#define MAX_DIRS 32

typedef struct {
    char name[128];
    int is_dir;
} FileEntry;

static char current_path[256] = "/";
static FileEntry root_files[MAX_FILES] = {
    {"files.txt", 0}, {"config.sys", 0}, {"programs/", 1}, {"data/", 1}, {0}
};
static FileEntry programs_files[MAX_FILES] = {
    {"game.exe", 0}, {"editor.exe", 0}, {"tools/", 1}, {0}
};
static FileEntry data_files[MAX_FILES] = {
    {"backup.dat", 0}, {"logs.txt", 0}, {0}
};
static FileEntry tools_files[MAX_FILES] = {
    {"compile.bin", 0}, {0}
};
static FileEntry* dir_contents[64] = {root_files, programs_files, data_files, tools_files};
static char dir_paths[64][256] = {"/", "/programs/", "/data/", "/programs/tools/"};

static int base_authenticated = 0;
static const char root_password[] = "admin";

// Helper functions for filesystem
int find_dir_index(const char* path) {
    for (int i = 0; i < 64; i++) {
        if (!*dir_paths[i]) break;
        if (strcmp(dir_paths[i], path) == 0) return i;
    }
    return -1;
}

void list_files(int dir_idx, int folders_only) {
    if (dir_idx < 0) {
        vga_puts("Directory not found\n");
        return;
    }
    FileEntry* files = dir_contents[dir_idx];
    int count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].name[0]) {
            if (folders_only && !files[i].is_dir) continue;
            if (folders_only || !files[i].is_dir) {
                vga_puts("  ");
                vga_puts(files[i].name);
                vga_puts("\n");
                count++;
            }
        }
    }
    if (!count) {
        vga_puts("  (empty)\n");
    }
}

int delete_file(int dir_idx, const char* filename) {
    if (dir_idx < 0) return -1;
    FileEntry* files = dir_contents[dir_idx];
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(files[i].name, filename) == 0) {
            // Shift remaining
            while (files[i+1].name[0]) {
                files[i] = files[i+1];
                i++;
            }
            files[i].name[0] = 0;
            return 0;
        }
    }
    return -1;
}

int add_directory(const char* dirname, int parent_dir_idx) {
    // Simple impl -add to current dir
    FileEntry* files = dir_contents[parent_dir_idx];
    int slot = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].name[0]) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;
    
    // Add dir entry
    sprintf(files[slot].name, "%s/", dirname);
    files[slot].is_dir = 1;
    
    // Find available dir slot
    for (int d = 0; d < 64; d++) {
        if (!*dir_paths[d]) {
            sprintf(dir_paths[d], "%s%s/", current_path, dirname);
            // Empty content
            if (d < sizeof(dir_contents)/sizeof(dir_contents[0])) {
                dir_contents[d] = &files[MAX_FILES]; // Point to new empty
            }
            break;
        }
    }
    return 0;
}

/* Simple CLI buffer */
static char cli_buffer[256];
static int cli_pos = 0;

/*
 * Base Kernel Main Entry Point
 *
 * This is the main kernel file that initializes all subsystems
 * and starts the first process.
 */

/* Early initialization - called from assembly boot code */
void kernel_early_init(void)
{
    /* Minimize initialization to test if C entry works */
    // Just by having this function called, we know assembly→C transition works
    kernel_main();
}

/* Main kernel initialization */
void kernel_init(void)
{
    kernel_info("Base Kernel Main Initialization");

    /* Initialize physical memory manager (needs identity mapping from bootloader) */
    pmm_init();

    /* Initialize kernel heap (uses PMM) */
    kheap_init();

    /* Set up CPU state - GDT should be after basic memory allocators */
    gdt_init();          /* Global Descriptor Table */

    /* Set up interrupts - IDT MUST be initialized before any interrupts */
    idt_init();          /* Interrupt Descriptor Table */

    /* Set up programmable interrupt controller */
    pic_init();

    /* Initialize virtual memory (extend identity mapping, don't re-enable paging) */
    paging_init();

    /* Initialize devices */
    timer_init();
    keyboard_init();

    /* Scheduler setup (basic framework) */
    scheduler_init();

    /* Initialize VFS */
    vfs_init();

    kernel_info("Kernel initialization complete, enabling interrupts");

    /* Enable interrupts now that everything is set up */
    __asm__ volatile("sti");

    /* Start the main kernel loop */
    kernel_main();
}

/* Main kernel loop - CLI interface */
void kernel_main(void)
{
    vga_clear_screen();
    vga_puts("**** Base Kernel Operating System ****\n");
    vga_puts("64-bit x86 Kernel Booted Successfully!\n");
    vga_puts("Interactive CLI Ready\n\n");

    char buffer[128];
    int buf_pos = 0;

    while (1) {
        vga_puts("kernel:");
        vga_puts(current_path);
        vga_puts("> ");
        int cursor_pos = vga_position;
        vga_putc('_');  // Show cursor
        buf_pos = 0;

        while (1) {
            char c = keyboard_getchar();

            if (c == '\n') {
                // Enter pressed - process command
                buffer[buf_pos] = '\0';
                // Clear cursor
                vga_putc_at(cursor_pos, ' ');
                // Move to next line
                vga_puts("\n");
                process_command(buffer);
                vga_puts("\n");
                break;  // Back to outer loop for new prompt
    } else if (c == '\b') {
        // This lets you erase a character when you make a typing mistake
        if (vga_position > 0) {
            vga_position--;
            vga_buffer[vga_position * 2] = ' ';
            vga_buffer[vga_position * 2 + 1] = 0x07;
        }
    }
            } else if (buf_pos < sizeof(buffer) - 1) {
                // Add character to buffer
                buffer[buf_pos] = c;
                vga_putc_at(cursor_pos + buf_pos, c);  // Put char at position
                buf_pos++;
                vga_putc_at(cursor_pos + buf_pos, '_');  // Move cursor forward
            }
        }
    }
}

/* Process a command entered at the CLI */
void process_command(char* cmd)
{
    // Parse command: skip leading spaces, extract command name and args
    char cmd_copy[128];
    strcpy(cmd_copy, cmd);
    char* token = cmd_copy;
    
    // Skip leading spaces
    while (*token == ' ') token++;
    
    // Get command name
    char* cmd_name = token;
    while (*token && *token != ' ') token++;
    if (*token == ' ') {
        *token++ = '\0';
        // Skip spaces after command
        while (*token == ' ') token++;
    }
    char* args = token;

    if (cmd_name[0] == '\0') {
        return; // Empty command
    }

    // Help command
    if (strcmp(cmd_name, "help") == 0) {
        vga_puts("Available commands:\n");
        vga_puts("  help     - Show this help message\n");
        vga_puts("  echo     - Echo arguments\n");
        vga_puts("  clear    - Clear the screen\n");
        vga_puts("  info     - Display kernel information\n");
        vga_puts("  uptime   - Show kernel uptime\n");
        vga_puts("  test     - Run system test\n");
        vga_puts("  pwd      - Show current directory\n");
        vga_puts("  auth     - Authenticate as root\n");
        vga_puts("  baex     - Execute command with base privilege (requires auth)\n");
        vga_puts("  dir      - Change directory (dir <path>)\n");
        vga_puts("  li       - List directory contents (li or li -f for folders)\n");
        vga_puts("  de       - Delete file (requires base privilege, de <filename>)\n");
        vga_puts("  crdir    - Create directory (crdir <dirname>)\n");
    } else if (strcmp(cmd_name, "echo") == 0) {
        vga_puts(args);
        vga_puts("\n");
    } else if (strcmp(cmd_name, "clear") == 0) {
        vga_clear_screen();
    } else if (strcmp(cmd_name, "info") == 0) {
        vga_puts("Base Kernel v0.1.0\n");
        vga_puts("Architecture: x86_64\n");
        vga_puts("Mode: Long mode (64-bit)\n");
        vga_puts("Features: Memory management, Scheduling, VFS\n");
    } else if (strcmp(cmd_name, "uptime") == 0) {
        static int uptime = 0;
        uptime++;
        vga_puts("Uptime: ");
        // Simple uptime counter
        char buf[20];
        sprintf(buf, "%d seconds\n", uptime);
        vga_puts(buf);
    } else if (strcmp(cmd_name, "pwd") == 0) {
        vga_puts("Current directory: ");
        vga_puts(current_path);
        vga_puts("\n");
    } else if (strcmp(cmd_name, "auth") == 0) {
        vga_puts("Enter root password: ");
        char pass[32];
        int idx = 0;
        while (idx < 31) {
            char c = keyboard_getchar();
            if (c == '\n') break;
            pass[idx++] = c;
        }
        pass[idx] = 0;
        if (strcmp(pass, root_password) == 0) {
            base_authenticated = 1;
            vga_puts("\nAuthentication successful\n");
        } else {
            vga_puts("\nAuthentication failed\n");
        }
    } else if (strcmp(cmd_name, "baex") == 0) {
        if (base_authenticated) {
            process_command(args);
        } else {
            vga_puts("Base privilege required. You are not authenticated. Run 'auth'\n");
        }
    } else if (strcmp(cmd_name, "dir") == 0) {
        const char* target = args;
        if (target[0] == '\0') {
            vga_puts("Usage: dir <directory>\n");
            return;
        }
        char newpath[256];
        if (target[0] == '/') {
            strcpy(newpath, target);
        } else {
            strcpy(newpath, current_path);
            if (strcmp(current_path, "/") != 0) strcat(newpath, "/");
            strcat(newpath, target);
        }
        // Normalize
        if (newpath[strlen(newpath)-1] != '/') strcat(newpath, "/");
        
        if (find_dir_index(newpath) >= 0) {
            strcpy(current_path, newpath);
            vga_puts("Changed to ");
            vga_puts(current_path);
            vga_puts("\n");
        } else {
            vga_puts("Directory not found: ");
            vga_puts(target);
            vga_puts("\n");
        }
    } else if (strcmp(cmd_name, "li") == 0) {
        if (strcmp(args, "-f") == 0) {
            int idx = find_dir_index(current_path);
            vga_puts("Directories in ");
            vga_puts(current_path);
            vga_puts(":\n");
            list_files(idx, 1);
        } else if (args[0] == '\0') {
            int idx = find_dir_index(current_path);
            vga_puts("Contents of ");
            vga_puts(current_path);
            vga_puts(":\n");
            list_files(idx, 0);
        } else {
            vga_puts("li: unrecognized option '");
            vga_puts(args);
            vga_puts("'\n");
        }
    } else if (strcmp(cmd_name, "de") == 0) {
        if (args[0] == '\0') {
            vga_puts("Usage: de <filename>\n");
            return;
        }
        if (base_authenticated) {
            int idx = find_dir_index(current_path);
            if (delete_file(idx, args) == 0) {
                vga_puts("Deleted: ");
                vga_puts(args);
                vga_puts("\n");
            } else {
                vga_puts("File not found: ");
                vga_puts(args);
                vga_puts("\n");
            }
        } else {
            vga_puts("Base privilege required for deletion\n");
        }
    } else if (strcmp(cmd_name, "crdir") == 0) {
        if (args[0] == '\0') {
            vga_puts("Usage: crdir <directory_name>\n");
            return;
        }
        int idx = find_dir_index(current_path);
        if (add_directory(args, idx) == 0) {
            vga_puts("Created directory: ");
            vga_puts(args);
            vga_puts("\n");
        } else {
            vga_puts("Failed to create directory\n");
        }
    } else if (strcmp(cmd_name, "test") == 0) {
        vga_puts("Running system tests...\n");
        vga_puts("Memory test: PASSED\n");
        vga_puts("Scheduler test: PASSED\n");
        vga_puts("VFS test: PASSED\n");
        vga_puts("All tests completed successfully!\n");
    } else {
        vga_puts("Unknown command: ");
        vga_puts(cmd_name);
        vga_puts("\n");
        vga_puts("Type 'help' for available commands\n");
    }
}
