#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <threads/malloc.h>
#include <lib/kernel/list.h>

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

struct thread_alarm {
    struct list_elem elem;
    struct thread* thread;
    uint32_t alarm_time;
};

struct list alarm_list;

static struct lock* alarm_lock;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;

static bool too_many_loops(unsigned loops);

static void busy_wait(int64_t loops);

static void real_time_sleep(int64_t num, int32_t denom);

static void real_time_delay(int64_t num, int32_t denom);

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void timer_init(void) {
    pit_configure_channel(0, 2, TIMER_FREQ);
    intr_register_ext(0x20, timer_interrupt, "8254 Timer");
    alarm_lock = malloc(sizeof(struct lock));
    lock_init(alarm_lock);
    list_init(&alarm_list);
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate(void) {
    unsigned high_bit, test_bit;

    ASSERT (intr_get_level() == INTR_ON);
    printf("Calibrating timer...  ");

    /* Approximate loops_per_tick as the largest power-of-two
       still less than one timer tick. */
    loops_per_tick = 1u << 10;
    while (!too_many_loops(loops_per_tick << 1)) {
        loops_per_tick <<= 1;
        ASSERT (loops_per_tick != 0);
    }

    /* Refine the next 8 bits of loops_per_tick. */
    high_bit = loops_per_tick;
    for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
        if (!too_many_loops(high_bit | test_bit))
            loops_per_tick |= test_bit;

    printf("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t timer_ticks(void) {
    enum intr_level old_level = intr_disable();
    int64_t t = ticks;
    intr_set_level(old_level);
    return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t timer_elapsed(int64_t then) {
    return timer_ticks() - then;
}

/* Sleeps for approximately TICKS timer sleep_ticks.  Interrupts must
   be turned on. */
void timer_sleep(int64_t sleep_ticks) {
    if (sleep_ticks <= 0) {
        return;
    }

    // store our pid and current tick and sleep_ticks somewhere
    // set the thread state to blocked
    int64_t start = timer_ticks();

    struct thread_alarm* new_alarm = malloc(sizeof(struct thread_alarm));
    new_alarm->alarm_time = start + sleep_ticks;
    new_alarm->thread = thread_current();

    lock_acquire(alarm_lock);
    list_push_back(&alarm_list, new_alarm);
    lock_release(alarm_lock);

    intr_set_level(INTR_OFF);
    thread_block();
    intr_set_level(INTR_ON);

    lock_acquire(alarm_lock);
    list_remove(&new_alarm->elem);
    lock_release(alarm_lock);
    free(new_alarm);
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep(int64_t ms) {
    real_time_sleep(ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep(int64_t us) {
    real_time_sleep(us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep(int64_t ns) {
    real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay(int64_t ms) {
    real_time_delay(ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay(int64_t us) {
    real_time_delay(us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay(int64_t ns) {
    real_time_delay(ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats(void) {
    printf("Timer: %"PRId64" ticks\n", timer_ticks());
}

void check_alarm(struct thread* t, void* aux) {
    struct list_elem* curr;
    for (curr = list_begin (&alarm_list); curr != list_end (&alarm_list);
         curr = list_next (curr))
    {
        struct thread_alarm *alarm = list_entry(curr, struct thread_alarm, elem);
        if (alarm->thread->tid == t->tid && alarm->alarm_time <= ticks && t->status == THREAD_BLOCKED) {
            thread_unblock(t);
        }
    }
}

/* Timer interrupt handler. */
static void timer_interrupt(struct intr_frame *args UNUSED) {
    ticks++;
    thread_foreach(check_alarm, NULL);
    thread_tick();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool too_many_loops(unsigned loops) {
    /* Wait for a timer tick. */
    int64_t start = ticks;
    while (ticks == start)
            barrier ();

    /* Run LOOPS loops. */
    start = ticks;
    busy_wait(loops);

    /* If the tick count changed, we iterated too long. */
    barrier ();
    return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait(int64_t loops) {
    while (loops-- > 0)
            barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep(int64_t num, int32_t denom) {
    /* Convert NUM/DENOM seconds into timer ticks, rounding down.

          (NUM / DENOM) s
       ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
       1 s / TIMER_FREQ ticks
    */
    int64_t ticks = num * TIMER_FREQ / denom;

    ASSERT (intr_get_level() == INTR_ON);
    if (ticks > 0) {
        /* We're waiting for at least one full timer tick.  Use
           timer_sleep() because it will yield the CPU to other
           processes. */
        timer_sleep(ticks);
    } else {
        /* Otherwise, use a busy-wait loop for more accurate
           sub-tick timing. */
        real_time_delay(num, denom);
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay(int64_t num, int32_t denom) {
    /* Scale the numerator and denominator down by 1000 to avoid
       the possibility of overflow. */
    ASSERT (denom % 1000 == 0);
    busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}
