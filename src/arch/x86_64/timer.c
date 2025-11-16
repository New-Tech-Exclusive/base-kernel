#include "kernel.h"

/*
 * Timer system using Programmable Interval Timer (PIT)
 * Provides system ticks at a configurable frequency
 */

// PIT I/O ports
#define PIT_DATA0       0x40
#define PIT_DATA1       0x41
#define PIT_DATA2       0x42
#define PIT_COMMAND     0x43

// PIT command register bits
#define PIT_CHANNEL_0   0x00
#define PIT_CHANNEL_1   0x40
#define PIT_CHANNEL_2   0x80
#define PIT_READBACK    0xC0

#define PIT_LATCH_COUNT 0x00
#define PIT_ACCESS_LO   0x10
#define PIT_ACCESS_HI   0x20
#define PIT_ACCESS_BOTH 0x30

#define PIT_MODE_0      0x00    // Interrupt on terminal count
#define PIT_MODE_1      0x02    // Hardware retriggerable one-shot
#define PIT_MODE_2      0x04    // Rate generator
#define PIT_MODE_3      0x06    // Square wave generator
#define PIT_MODE_4      0x08    // Software triggered strobe
#define PIT_MODE_5      0x0A    // Hardware triggered strobe

#define PIT_BINARY      0x00
#define PIT_BCD         0x01

// Configuration
#define TIMER_FREQUENCY     100     // 100 Hz
#define PIT_BASE_FREQUENCY  1193182 // 1.193182 MHz

// Timer state
static uint64_t timer_ticks = 0;

// Initialize the timer
void timer_init(void)
{
    KINFO("Initializing timer at %u Hz...", TIMER_FREQUENCY);

    // Calculate divisor for desired frequency
    uint16_t divisor = PIT_BASE_FREQUENCY / TIMER_FREQUENCY;

    // Send command byte (channel 0, access both, mode 3, binary)
    uint8_t command = PIT_CHANNEL_0 | PIT_ACCESS_BOTH | PIT_MODE_3 | PIT_BINARY;
    outb(PIT_COMMAND, command);

    // Send divisor (low byte first, then high byte)
    outb(PIT_DATA0, divisor & 0xFF);
    io_wait();
    outb(PIT_DATA0, (divisor >> 8) & 0xFF);

    KINFO("Timer initialized: divisor=%u", divisor);
}

// Handle timer tick (called by interrupt handler)
void timer_tick(void)
{
    timer_ticks++;

    // Call scheduler tick for context switching
    scheduler_tick();
}

// Get current timer ticks
uint64_t timer_get_ticks(void)
{
    return timer_ticks;
}

// Sleep for specified number of milliseconds
void timer_sleep(uint32_t milliseconds)
{
    uint64_t start_ticks = timer_ticks;
    uint64_t target_ticks = start_ticks + (milliseconds * TIMER_FREQUENCY / 1000);

    // Busy wait (not efficient, but simple)
    while (timer_ticks < target_ticks) {
        __asm__ volatile("nop");
    }
}

// Sleep for specified number of ticks
void timer_sleep_ticks(uint32_t ticks)
{
    uint64_t target_ticks = timer_ticks + ticks;

    // Busy wait
    while (timer_ticks < target_ticks) {
        __asm__ volatile("nop");
    }
}
